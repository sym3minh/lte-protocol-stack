#include <gtest/gtest.h>
#include "buffer_pool.h"
#include <thread>
#include <vector>
#include <atomic>
#include <cstring>

using namespace lte;

// ============================================================
// Basic allocation
// ============================================================
TEST(BufferPoolTest, AllocateReturnsNonNull)
{
  BufferPool pool(64, 4);
  uint8_t *p = pool.allocate();
  ASSERT_NE(p, nullptr);
  pool.deallocate(p);
}

TEST(BufferPoolTest, AllocateDecrementsAvailable)
{
  BufferPool pool(64, 4);
  EXPECT_EQ(pool.available(), 4u);

  uint8_t *p1 = pool.allocate();
  EXPECT_EQ(pool.available(), 3u);

  uint8_t *p2 = pool.allocate();
  EXPECT_EQ(pool.available(), 2u);

  pool.deallocate(p1);
  pool.deallocate(p2);
  EXPECT_EQ(pool.available(), 4u);
}

// ============================================================
// Exhaustion
// ============================================================
TEST(BufferPoolTest, ExhaustionReturnsNullptr)
{
  BufferPool pool(64, 3);

  uint8_t *p1 = pool.allocate();
  uint8_t *p2 = pool.allocate();
  uint8_t *p3 = pool.allocate();

  EXPECT_TRUE(pool.isExhausted());
  EXPECT_EQ(pool.allocate(), nullptr); // 4th allocation must fail gracefully

  // Release one → can allocate again
  pool.deallocate(p1);
  EXPECT_FALSE(pool.isExhausted());
  uint8_t *p4 = pool.allocate();
  EXPECT_NE(p4, nullptr);

  pool.deallocate(p2);
  pool.deallocate(p3);
  pool.deallocate(p4);
}

// ============================================================
// Blocks are writable and don't overlap
// ============================================================
TEST(BufferPoolTest, BlocksAreWritable)
{
  const size_t BS = 128;
  BufferPool pool(BS, 4);

  std::vector<uint8_t *> ptrs;
  for (int i = 0; i < 4; ++i)
  {
    uint8_t *p = pool.allocate();
    ASSERT_NE(p, nullptr);
    std::memset(p, static_cast<uint8_t>(i + 1), BS);
    ptrs.push_back(p);
  }

  // Verify each block holds its own pattern (no overlap)
  for (int i = 0; i < 4; ++i)
  {
    for (size_t j = 0; j < BS; ++j)
    {
      EXPECT_EQ(ptrs[i][j], static_cast<uint8_t>(i + 1))
          << "Overlap detected at block " << i << " byte " << j;
    }
  }

  for (auto p : ptrs)
    pool.deallocate(p);
}

// ============================================================
// BufferGuard RAII
// ============================================================
TEST(BufferPoolTest, BufferGuardReleasesOnDestroy)
{
  BufferPool pool(64, 2);
  EXPECT_EQ(pool.available(), 2u);

  {
    BufferGuard g(pool);
    EXPECT_TRUE(g.valid());
    EXPECT_EQ(pool.available(), 1u);
  } // g goes out of scope → block returned automatically

  EXPECT_EQ(pool.available(), 2u);
}

TEST(BufferPoolTest, BufferGuardReleaseTransfersOwnership)
{
  BufferPool pool(64, 2);

  uint8_t *raw = nullptr;
  {
    BufferGuard g(pool);
    raw = g.release(); // caller now owns the block
    EXPECT_FALSE(g.valid());
    EXPECT_EQ(pool.available(), 1u);
  } // g destroyed but does NOT return the block

  EXPECT_EQ(pool.available(), 1u); // still 1 taken
  pool.deallocate(raw);
  EXPECT_EQ(pool.available(), 2u);
}

// ============================================================
// Thread safety — concurrent alloc/dealloc must not crash
// ============================================================
TEST(BufferPoolTest, ConcurrentAllocDeallocSafe)
{
  BufferPool pool(64, 128);
  std::atomic<int> failures{0};
  const int THREADS = 4;
  const int OPS = 200;

  auto worker = [&]()
  {
    for (int i = 0; i < OPS; ++i)
    {
      uint8_t *p = pool.allocate();
      if (p)
      {
        std::memset(p, 0xAB, 64);
        pool.deallocate(p);
      }
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < THREADS; ++i)
    threads.emplace_back(worker);
  for (auto &t : threads)
    t.join();

  // All blocks should be back in the pool after workers finish
  EXPECT_EQ(pool.available(), 128u);
}

// ============================================================
// Invalid constructor arguments
// ============================================================
TEST(BufferPoolTest, ZeroBlockSizeThrows)
{
  EXPECT_THROW(BufferPool(0, 4), std::invalid_argument);
}

TEST(BufferPoolTest, ZeroNumBlocksThrows)
{
  EXPECT_THROW(BufferPool(64, 0), std::invalid_argument);
}
