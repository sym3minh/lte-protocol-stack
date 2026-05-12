#pragma once
// ============================================================
// byte_buffer.h — Buffer abstraction layer over BufferPool
//
// Ref: RLC_ARCHITECTURE_DECISIONS.md §3
//
// ByteBuffer wraps a single block from BufferPool and adds:
//   1. head_room  — bytes reserved at the front for header prepend
//   2. prepend()  — write an RLC/PDCP header without copying payload
//   3. append()   — add data after current tail
//   4. view()     — zero-copy read-only window into the buffer
//
// Ownership model (Phase 1 — pure copy strategy):
//   ByteBuffer is move-only.  It owns exactly one block from the
//   pool and returns it on destruction.
//
//   Phase 2 (AM ARQ) will add shared ownership via a separate
//   SharedByteBuffer / slice type without changing this class.
//
// Memory layout of the underlying block:
//
//   ┌────────────┬──────────────────────┬──────────────┐
//   │  head_room │       data           │   tail room  │
//   │  (reserved)│  [head_, tail_)      │  (available) │
//   └────────────┴──────────────────────┴──────────────┘
//   ^            ^                      ^              ^
//   block_       head_                  tail_          block_ + capacity_
//
//   prepend() moves head_ left (into head_room area).
//   append()  moves tail_ right (into tail room area).
//
// Note on capacity:
//   The block size is fixed by BufferPool. allocate(pool, data_cap,
//   head_room) treats (head_room + data_cap) as a MINIMUM size
//   requirement: it fails if that exceeds blockSize(), otherwise
//   the ByteBuffer is allowed to use the full blockSize() for
//   tail room. data_cap is therefore a precondition check, not a
//   hard tailroom cap — callers that need a strict cap should
//   verify tailroom() before each append.
// ============================================================

#include "buffer_pool.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>

namespace lte {

// ============================================================
// BufferView — non-owning read-only window (C++17 compatible)
//
// Equivalent to std::span<const uint8_t> (C++20).
// Caller must not outlive the ByteBuffer that produced this view.
// ============================================================
struct BufferView {
    const uint8_t* data = nullptr;
    size_t         len  = 0;

    bool        empty()  const { return len == 0; }
    bool        valid()  const { return data != nullptr; }
    const uint8_t* begin() const { return data; }
    const uint8_t* end()   const { return data + len; }
};

// ============================================================
// ByteBuffer
// ============================================================
class ByteBuffer {
public:
    // ----------------------------------------------------------
    // Factory — allocate one block from pool.
    //
    // data_capacity : MINIMUM bytes required for payload after
    //                 head_room. Used as a precondition check
    //                 against pool block size.
    // head_room     : bytes reserved at the front for future
    //                 prepend (default 16 bytes — covers most
    //                 RLC/PDCP headers).
    //
    // Returns an invalid ByteBuffer (valid() == false) if
    //   (head_room + data_capacity) > pool.blockSize(), or
    //   the pool is exhausted.
    // Always check valid() before use.
    // ----------------------------------------------------------
    static ByteBuffer allocate(BufferPool& pool,
                                size_t      data_capacity,
                                size_t      head_room = 16);

    // ----------------------------------------------------------
    // Default-constructed: invalid / empty (no block held).
    // ----------------------------------------------------------
    ByteBuffer() = default;

    // ----------------------------------------------------------
    // Destructor: returns block to pool automatically.
    // ----------------------------------------------------------
    ~ByteBuffer() { release_block(); }

    // ----------------------------------------------------------
    // Move semantics — ByteBuffer is move-only (not copyable).
    // ----------------------------------------------------------
    ByteBuffer(ByteBuffer&& other) noexcept;
    ByteBuffer& operator=(ByteBuffer&& other) noexcept;

    ByteBuffer(const ByteBuffer&)            = delete;
    ByteBuffer& operator=(const ByteBuffer&) = delete;

    // ----------------------------------------------------------
    // State queries
    // ----------------------------------------------------------
    bool valid()  const { return block_ != nullptr; }
    explicit operator bool() const { return valid(); }
    bool empty()  const { return head_ == tail_; }

    // ----------------------------------------------------------
    // Data access
    // ----------------------------------------------------------

    // Pointer to the start of the valid data region [head_, tail_)
    uint8_t*       data()       { return block_ ? block_ + head_ : nullptr; }
    const uint8_t* data() const { return block_ ? block_ + head_ : nullptr; }

    // Number of valid bytes in [head_, tail_)
    size_t size() const { return tail_ - head_; }

    // ----------------------------------------------------------
    // Capacity queries
    // ----------------------------------------------------------

    // Bytes available in front (for prepend)
    size_t headroom() const { return head_; }

    // Bytes available at back (for append)
    size_t tailroom() const { return block_ ? capacity_ - tail_ : 0; }

    // Total block capacity (head_room + data + tail room)
    size_t capacity() const { return capacity_; }

    // Initial head_room reserved at allocation time. reset_data()
    // restores the cursors to this position.
    size_t initial_headroom() const { return initial_head_room_; }

    // ----------------------------------------------------------
    // prepend — add header bytes BEFORE current data
    //
    // Moves head_ backward by hdr_len bytes and writes hdr_data.
    // Returns false if the buffer is invalid or if headroom is
    // insufficient (head_ would go below 0). Caller must ensure
    // adequate head_room at allocate.
    // ----------------------------------------------------------
    bool prepend(const uint8_t* hdr_data, size_t hdr_len);

    // Convenience: prepend a single byte (e.g. a flag byte)
    bool prepend_byte(uint8_t b);

    // ----------------------------------------------------------
    // append — add bytes AFTER current data
    //
    // Moves tail_ forward by len bytes.
    // Returns false if the buffer is invalid or tailroom is
    // insufficient.
    // ----------------------------------------------------------
    bool append(const uint8_t* src, size_t len);

    // Convenience: append a single byte
    bool append_byte(uint8_t b);

    // ----------------------------------------------------------
    // consume — discard len bytes from the FRONT
    //
    // Advances head_ forward.  Used when parsing: after reading
    // a header field, consume() skips past it.
    // Returns false if len > size().
    // ----------------------------------------------------------
    bool consume(size_t len);

    // ----------------------------------------------------------
    // view — zero-copy read-only sub-window
    //
    // offset : byte offset relative to data() (i.e. from head_)
    // len    : number of bytes in the view
    //
    // Returns an invalid BufferView {nullptr, 0} if out of bounds.
    // The returned view is only valid as long as this ByteBuffer
    // is alive and unmodified.
    // ----------------------------------------------------------
    BufferView view(size_t offset, size_t len) const;

    // View of the entire valid data region
    BufferView view_all() const;

    // ----------------------------------------------------------
    // reset_data — discard payload, restore the initial head_room.
    //
    // After reset_data():
    //   headroom() == initial_headroom()
    //   size()     == 0
    //   tailroom() == capacity() - initial_headroom()
    //
    // The block is NOT returned to the pool and the bytes are NOT
    // zeroed — only the cursors are rewound. Useful for buffer
    // reuse during PDU rebuild.
    // ----------------------------------------------------------
    void reset_data();

private:
    // Private constructor called by allocate()
    ByteBuffer(BufferPool& pool,
               uint8_t*    block,
               size_t      capacity,
               size_t      head_room);

    // Return block to pool if we own one
    void release_block();

    BufferPool* pool_              = nullptr;
    uint8_t*    block_             = nullptr;
    size_t      capacity_          = 0;
    size_t      head_              = 0;  // offset to start of valid data
    size_t      tail_              = 0;  // offset to end of valid data (exclusive)
    size_t      initial_head_room_ = 0;  // head_room set at allocate time
};

} // namespace lte
