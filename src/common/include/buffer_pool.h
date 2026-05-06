#pragma once
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <vector>
#include <cassert>

namespace lte {

// ============================================================
// BufferPool — Fixed-size slab allocator
//
// Why not new/delete?
//   - new/delete have non-deterministic latency (heap fragmentation)
//   - In telecom stacks, worst-case allocation time must be bounded
//   - This pool pre-allocates all memory at startup; allocate()
//     is O(1) and never calls into the system allocator at runtime
//
// Thread safety: allocate/deallocate are mutex-protected
// ============================================================
class BufferPool {
public:
    // block_size : bytes per block (e.g. 2048 for max PDCP SDU)
    // num_blocks : total pre-allocated blocks
    BufferPool(size_t block_size, size_t num_blocks);
    ~BufferPool();

    // Non-copyable, non-movable (owns raw memory)
    BufferPool(const BufferPool&)            = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    // Allocate one block. Returns nullptr if pool is exhausted.
    // O(1) — pops from free-list head
    uint8_t* allocate();

    // Return a block to the pool.
    // Precondition: ptr must have been returned by allocate()
    // O(1) — pushes to free-list head
    void deallocate(uint8_t* ptr);

    // Stats
    size_t blockSize()    const { return block_size_; }
    size_t totalBlocks()  const { return num_blocks_; }
    size_t available()    const;  // free blocks right now
    bool   isExhausted()  const;

private:
    const size_t block_size_;
    const size_t num_blocks_;

    uint8_t* memory_;          // contiguous slab: num_blocks * block_size
    uint8_t** free_list_;      // stack of free block pointers
    size_t    free_top_;       // index of next free slot

    mutable std::mutex mutex_;

    bool isValidPtr(const uint8_t* ptr) const;
};

// ============================================================
// RAII guard — automatically returns block on scope exit
// ============================================================
class BufferGuard {
public:
    BufferGuard(BufferPool& pool)
        : pool_(pool), ptr_(pool.allocate()) {}

    ~BufferGuard() {
        if (ptr_) pool_.deallocate(ptr_);
    }

    uint8_t* get()   const { return ptr_; }
    bool     valid() const { return ptr_ != nullptr; }

    // Release ownership (caller must deallocate manually)
    uint8_t* release() {
        uint8_t* p = ptr_;
        ptr_ = nullptr;
        return p;
    }

    // Non-copyable
    BufferGuard(const BufferGuard&)            = delete;
    BufferGuard& operator=(const BufferGuard&) = delete;

private:
    BufferPool& pool_;
    uint8_t*    ptr_;
};

} // namespace lte
