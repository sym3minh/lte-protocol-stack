#include "byte_buffer.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace lte {

// ============================================================
// Private constructor
// ============================================================
ByteBuffer::ByteBuffer(BufferPool& pool,
                        uint8_t*    block,
                        size_t      capacity,
                        size_t      head_room)
    : pool_             (&pool)
    , block_            (block)
    , capacity_         (capacity)
    , head_             (head_room)   // data region starts after head_room
    , tail_             (head_room)   // initially empty: no payload bytes
    , initial_head_room_(head_room)
{
    assert(block != nullptr);
    assert(head_room <= capacity);
}

// ============================================================
// allocate
// ============================================================
ByteBuffer ByteBuffer::allocate(BufferPool& pool,
                                  size_t      data_capacity,
                                  size_t      head_room)
{
    const size_t total = head_room + data_capacity;

    if (total > pool.blockSize()) {
        // Requested size exceeds pool block size — return invalid buffer.
        // Caller must check valid().
        return ByteBuffer{};
    }

    uint8_t* block = pool.allocate();
    if (!block) {
        return ByteBuffer{};
    }

    return ByteBuffer(pool, block, pool.blockSize(), head_room);
}

// ============================================================
// Move constructor
// ============================================================
ByteBuffer::ByteBuffer(ByteBuffer&& other) noexcept
    : pool_             (other.pool_)
    , block_            (other.block_)
    , capacity_         (other.capacity_)
    , head_             (other.head_)
    , tail_             (other.tail_)
    , initial_head_room_(other.initial_head_room_)
{
    // Null out the source so its destructor doesn't deallocate
    other.pool_              = nullptr;
    other.block_             = nullptr;
    other.capacity_          = 0;
    other.head_              = 0;
    other.tail_              = 0;
    other.initial_head_room_ = 0;
}

// ============================================================
// Move assignment
// ============================================================
ByteBuffer& ByteBuffer::operator=(ByteBuffer&& other) noexcept
{
    if (this != &other) {
        release_block();          // return current block, if any

        pool_              = other.pool_;
        block_             = other.block_;
        capacity_          = other.capacity_;
        head_              = other.head_;
        tail_              = other.tail_;
        initial_head_room_ = other.initial_head_room_;

        other.pool_              = nullptr;
        other.block_             = nullptr;
        other.capacity_          = 0;
        other.head_              = 0;
        other.tail_              = 0;
        other.initial_head_room_ = 0;
    }
    return *this;
}

// ============================================================
// Destructor helper
// ============================================================
void ByteBuffer::release_block()
{
    if (block_ && pool_) {
        pool_->deallocate(block_);
        block_             = nullptr;
        pool_              = nullptr;
        capacity_          = 0;
        head_              = 0;
        tail_              = 0;
        initial_head_room_ = 0;
    }
}

// ============================================================
// prepend
//
// Moves head_ backward by hdr_len and writes hdr_data there.
// Typical use: prepend RLC/PDCP header bytes before the payload
// that was already written via append().
// ============================================================
bool ByteBuffer::prepend(const uint8_t* hdr_data, size_t hdr_len)
{
    if (!block_)        return false;       // invalid buffer — fail loud
    if (hdr_len == 0)   return true;        // no-op on a valid buffer
    if (hdr_len > head_) return false;      // not enough headroom

    head_ -= hdr_len;
    std::memcpy(block_ + head_, hdr_data, hdr_len);
    return true;
}

bool ByteBuffer::prepend_byte(uint8_t b)
{
    return prepend(&b, 1);
}

// ============================================================
// append
// ============================================================
bool ByteBuffer::append(const uint8_t* src, size_t len)
{
    if (!block_)         return false;      // invalid buffer
    if (len == 0)        return true;       // no-op on a valid buffer
    if (len > tailroom()) return false;     // not enough space at tail

    std::memcpy(block_ + tail_, src, len);
    tail_ += len;
    return true;
}

bool ByteBuffer::append_byte(uint8_t b)
{
    return append(&b, 1);
}

// ============================================================
// consume — advance head_ forward (skip parsed bytes)
// ============================================================
bool ByteBuffer::consume(size_t len)
{
    if (len > size()) return false;
    head_ += len;
    return true;
}

// ============================================================
// view — zero-copy sub-window
// ============================================================
BufferView ByteBuffer::view(size_t offset, size_t len) const
{
    if (!block_) return {};
    if (offset > size() || len > size() - offset) return {};  // out of bounds

    return BufferView{ block_ + head_ + offset, len };
}

BufferView ByteBuffer::view_all() const
{
    if (!block_) return {};
    return BufferView{ block_ + head_, size() };
}

// ============================================================
// reset_data — rewind cursors to initial head_room position.
//
// Discards all payload but keeps the reserved head_room intact,
// so a subsequent prepend() of header bytes still has space.
// Memory is NOT zeroed.
// ============================================================
void ByteBuffer::reset_data()
{
    head_ = initial_head_room_;
    tail_ = initial_head_room_;
}

} // namespace lte
