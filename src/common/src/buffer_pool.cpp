#include "buffer_pool.h"
#include <stdexcept>
#include <cstring>

namespace lte {

BufferPool::BufferPool(size_t block_size, size_t num_blocks)
    : block_size_(block_size)
    , num_blocks_(num_blocks)
    , memory_(nullptr)
    , free_list_(nullptr)
    , free_top_(0)
{
    if (block_size == 0 || num_blocks == 0) {
        throw std::invalid_argument("BufferPool: block_size and num_blocks must be > 0");
    }

    // Allocate the entire slab up front — this is the ONLY system
    // allocation this class ever makes.
    memory_    = new uint8_t[block_size_ * num_blocks_];
    free_list_ = new uint8_t*[num_blocks_];

    // Zero-initialise memory (helps with debugging)
    std::memset(memory_, 0, block_size_ * num_blocks_);

    // Populate free list: each slot points to a block in the slab
    for (size_t i = 0; i < num_blocks_; ++i) {
        free_list_[i] = memory_ + i * block_size_;
    }
    free_top_ = num_blocks_;   // stack is full
}

BufferPool::~BufferPool() {
    delete[] memory_;
    delete[] free_list_;
}

uint8_t* BufferPool::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (free_top_ == 0) {
        return nullptr;   // pool exhausted — caller must handle this
    }
    // Pop from top of free-list stack
    return free_list_[--free_top_];
}

void BufferPool::deallocate(uint8_t* ptr) {
    if (ptr == nullptr) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Debug-mode bounds check
    assert(isValidPtr(ptr) && "BufferPool::deallocate: ptr not from this pool");
    assert(free_top_ < num_blocks_ && "BufferPool::deallocate: pool overflow");

    // Zero-out returned block to catch use-after-free bugs early
#ifndef NDEBUG
    std::memset(ptr, 0xDD, block_size_);
#endif

    free_list_[free_top_++] = ptr;
}

size_t BufferPool::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return free_top_;
}

bool BufferPool::isExhausted() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return free_top_ == 0;
}

bool BufferPool::isValidPtr(const uint8_t* ptr) const {
    if (ptr < memory_ || ptr >= memory_ + block_size_ * num_blocks_) {
        return false;
    }
    // Must be aligned to a block boundary
    size_t offset = static_cast<size_t>(ptr - memory_);
    return (offset % block_size_) == 0;
}

} // namespace lte
