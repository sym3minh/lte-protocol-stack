#include <gtest/gtest.h>
#include "byte_buffer.h"

#include <vector>
#include <numeric>
#include <cstring>

using namespace lte;

// ============================================================
// Test fixture
// ============================================================
class ByteBufferTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    // block_size=256, 32 blocks, generous for all tests
    pool = std::make_unique<BufferPool>(256, 32);
  }

  std::unique_ptr<BufferPool> pool;
};

// ============================================================
// allocate / valid
// ============================================================

TEST_F(ByteBufferTest, AllocateReturnsValidBuffer)
{
  auto buf = ByteBuffer::allocate(*pool, 128, 16);
  EXPECT_TRUE(buf.valid());
  EXPECT_TRUE(static_cast<bool>(buf));
}

TEST_F(ByteBufferTest, DefaultConstructedIsInvalid)
{
  ByteBuffer buf;
  EXPECT_FALSE(buf.valid());
  EXPECT_EQ(buf.data(), nullptr);
  EXPECT_EQ(buf.size(), 0u);
}

TEST_F(ByteBufferTest, AllocateFailsWhenPoolExhausted)
{
  // Fill the pool (32 blocks)
  std::vector<ByteBuffer> bufs;
  for (int i = 0; i < 32; ++i)
  {
    bufs.push_back(ByteBuffer::allocate(*pool, 128, 16));
  }
  // 33rd allocation must fail
  auto extra = ByteBuffer::allocate(*pool, 128, 16);
  EXPECT_FALSE(extra.valid());
}

TEST_F(ByteBufferTest, AllocateFailsWhenDataCapacityExceedsBlockSize)
{
  // pool block is 256 bytes; request more than that
  auto buf = ByteBuffer::allocate(*pool, 300, 0);
  EXPECT_FALSE(buf.valid());
}

TEST_F(ByteBufferTest, AllocateFailsWhenHeadRoomPlusDataExceedsBlock)
{
  // head_room + data_capacity must fit within blockSize() (256)
  auto buf = ByteBuffer::allocate(*pool, 250, 16);
  EXPECT_FALSE(buf.valid());
}

// ============================================================
// Initial state after allocate
// ============================================================

TEST_F(ByteBufferTest, InitiallyEmpty)
{
  auto buf = ByteBuffer::allocate(*pool, 100, 16);
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_EQ(buf.headroom(), 16u);
  EXPECT_EQ(buf.initial_headroom(), 16u);
  EXPECT_EQ(buf.capacity(), 256u); // pool block size
}

// ============================================================
// append
// ============================================================

TEST_F(ByteBufferTest, AppendWritesData)
{
  auto buf = ByteBuffer::allocate(*pool, 64, 16);

  const uint8_t src[] = {0x01, 0x02, 0x03, 0x04};
  EXPECT_TRUE(buf.append(src, sizeof(src)));
  EXPECT_EQ(buf.size(), 4u);
  EXPECT_EQ(std::memcmp(buf.data(), src, sizeof(src)), 0);
}

TEST_F(ByteBufferTest, AppendAccumulates)
{
  auto buf = ByteBuffer::allocate(*pool, 64, 16);

  const uint8_t a[] = {0xAA, 0xBB};
  const uint8_t b[] = {0xCC, 0xDD};
  EXPECT_TRUE(buf.append(a, sizeof(a)));
  EXPECT_TRUE(buf.append(b, sizeof(b)));
  EXPECT_EQ(buf.size(), 4u);

  EXPECT_EQ(buf.data()[0], 0xAA);
  EXPECT_EQ(buf.data()[1], 0xBB);
  EXPECT_EQ(buf.data()[2], 0xCC);
  EXPECT_EQ(buf.data()[3], 0xDD);
}

TEST_F(ByteBufferTest, AppendZeroLengthIsNoOpOnValidBuffer)
{
  auto buf = ByteBuffer::allocate(*pool, 64, 0);
  EXPECT_TRUE(buf.append(nullptr, 0));
  EXPECT_EQ(buf.size(), 0u);
}

TEST_F(ByteBufferTest, AppendOnInvalidBufferFails)
{
  ByteBuffer buf; // invalid
  const uint8_t b = 0xAB;
  EXPECT_FALSE(buf.append(&b, 1));
  // Even zero-length must report failure on an invalid buffer
  EXPECT_FALSE(buf.append(nullptr, 0));
}

// ============================================================
// prepend
// ============================================================

TEST_F(ByteBufferTest, PrependWritesBeforeData)
{
  auto buf = ByteBuffer::allocate(*pool, 64, 4); // 4 bytes headroom

  // Append payload first
  const uint8_t payload[] = {0x10, 0x20, 0x30};
  buf.append(payload, sizeof(payload));

  // Prepend 2-byte header
  const uint8_t hdr[] = {0xAA, 0xBB};
  EXPECT_TRUE(buf.prepend(hdr, sizeof(hdr)));

  EXPECT_EQ(buf.size(), 5u);
  EXPECT_EQ(buf.data()[0], 0xAA);
  EXPECT_EQ(buf.data()[1], 0xBB);
  EXPECT_EQ(buf.data()[2], 0x10);
  EXPECT_EQ(buf.data()[3], 0x20);
  EXPECT_EQ(buf.data()[4], 0x30);
}

TEST_F(ByteBufferTest, PrependUsesHeadroomWithoutCopyingPayload)
{
  auto buf = ByteBuffer::allocate(*pool, 64, 16);

  const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
  buf.append(payload, sizeof(payload));

  const uint8_t* data_ptr_before = buf.data(); // points into block

  const uint8_t hdr[] = {0xF1, 0xF2};
  buf.prepend(hdr, sizeof(hdr));

  // data() moved back by 2, but the payload bytes should be the
  // same memory (no copy of payload occurred)
  EXPECT_EQ(buf.data() + 2, data_ptr_before);
}

TEST_F(ByteBufferTest, PrependReturnsFalseWhenInsufficientHeadroom)
{
  auto buf = ByteBuffer::allocate(*pool, 64, 2); // only 2 bytes headroom

  const uint8_t hdr[3] = {0x11, 0x22, 0x33};
  EXPECT_FALSE(buf.prepend(hdr, sizeof(hdr)));
}

TEST_F(ByteBufferTest, PrependZeroLengthIsNoOpOnValidBuffer)
{
  auto buf = ByteBuffer::allocate(*pool, 64, 4);
  EXPECT_TRUE(buf.prepend(nullptr, 0));
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_EQ(buf.headroom(), 4u);
}

TEST_F(ByteBufferTest, PrependOnInvalidBufferFails)
{
  ByteBuffer buf; // invalid
  const uint8_t b = 0xAB;
  EXPECT_FALSE(buf.prepend(&b, 1));
  EXPECT_FALSE(buf.prepend(nullptr, 0));
}

// ============================================================
// consume
// ============================================================

TEST_F(ByteBufferTest, ConsumeAdvancesHead)
{
  auto buf = ByteBuffer::allocate(*pool, 64, 0);
  const uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
  buf.append(data, sizeof(data));

  EXPECT_TRUE(buf.consume(2));
  EXPECT_EQ(buf.size(), 3u);
  EXPECT_EQ(buf.data()[0], 0x03);
}

TEST_F(ByteBufferTest, ConsumeTooMuchReturnsFalse)
{
  auto buf = ByteBuffer::allocate(*pool, 16, 0);
  const uint8_t b = 0x99;
  buf.append(&b, 1);
  EXPECT_FALSE(buf.consume(2));
  EXPECT_EQ(buf.size(), 1u); // unchanged
}

// ============================================================
// view
// ============================================================

TEST_F(ByteBufferTest, ViewAllReturnsEntireData)
{
  auto buf = ByteBuffer::allocate(*pool, 64, 0);
  const uint8_t data[] = {0xAA, 0xBB, 0xCC};
  buf.append(data, sizeof(data));

  auto v = buf.view_all();
  EXPECT_TRUE(v.valid());
  EXPECT_EQ(v.len, 3u);
  EXPECT_EQ(v.data[0], 0xAA);
  EXPECT_EQ(v.data[1], 0xBB);
  EXPECT_EQ(v.data[2], 0xCC);
}

TEST_F(ByteBufferTest, ViewSubRange)
{
  auto buf = ByteBuffer::allocate(*pool, 64, 0);
  const uint8_t data[] = {0x10, 0x20, 0x30, 0x40, 0x50};
  buf.append(data, sizeof(data));

  // View bytes [1, 3) → {0x20, 0x30}
  auto v = buf.view(1, 2);
  EXPECT_TRUE(v.valid());
  EXPECT_EQ(v.len, 2u);
  EXPECT_EQ(v.data[0], 0x20);
  EXPECT_EQ(v.data[1], 0x30);
}

TEST_F(ByteBufferTest, ViewOutOfBoundsReturnsInvalid)
{
  auto buf = ByteBuffer::allocate(*pool, 64, 0);
  const uint8_t b = 0xFF;
  buf.append(&b, 1);

  auto v = buf.view(0, 5); // len=5 but size=1
  EXPECT_FALSE(v.valid());
  EXPECT_EQ(v.data, nullptr);
}

// ============================================================
// reset_data
// ============================================================

TEST_F(ByteBufferTest, ResetDataPreservesInitialHeadRoom)
{
  auto buf = ByteBuffer::allocate(*pool, 64, 8);
  EXPECT_EQ(buf.headroom(), 8u);

  // Write some payload + a prepended header
  const uint8_t payload[] = {0x01, 0x02, 0x03};
  buf.append(payload, sizeof(payload));
  const uint8_t hdr[] = {0xAA, 0xBB};
  buf.prepend(hdr, sizeof(hdr));

  // Sanity: headroom shrank, size grew
  EXPECT_EQ(buf.headroom(), 6u);
  EXPECT_EQ(buf.size(), 5u);

  buf.reset_data();

  // After reset: cursors back at the initial head_room boundary
  EXPECT_EQ(buf.headroom(), 8u);
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_TRUE(buf.empty());

  // We can still prepend a fresh header now
  EXPECT_TRUE(buf.prepend(hdr, sizeof(hdr)));
  EXPECT_EQ(buf.headroom(), 6u);
}

TEST_F(ByteBufferTest, ResetDataAllowsReuseForRebuild)
{
  // Simulates RLC AM rebuild path: same buffer reused for a new PDU
  auto buf = ByteBuffer::allocate(*pool, 64, 4);

  const uint8_t pdu1[] = {0x11, 0x22, 0x33};
  buf.append(pdu1, sizeof(pdu1));
  EXPECT_EQ(buf.size(), 3u);

  buf.reset_data();
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_EQ(buf.headroom(), 4u);

  const uint8_t pdu2[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
  EXPECT_TRUE(buf.append(pdu2, sizeof(pdu2)));
  EXPECT_EQ(buf.size(), 5u);
  EXPECT_EQ(buf.data()[0], 0xAA);
  EXPECT_EQ(buf.data()[4], 0xEE);
}

// ============================================================
// Move semantics
// ============================================================

TEST_F(ByteBufferTest, MoveConstructorTransfersOwnership)
{
  auto buf = ByteBuffer::allocate(*pool, 64, 0);
  const uint8_t b[] = {0x11, 0x22};
  buf.append(b, sizeof(b));

  const size_t available_before = pool->available();
  ByteBuffer moved(std::move(buf));

  EXPECT_FALSE(buf.valid()); // source is now invalid
  EXPECT_TRUE(moved.valid());
  EXPECT_EQ(moved.size(), 2u);
  EXPECT_EQ(pool->available(), available_before); // still one block out
}

TEST_F(ByteBufferTest, MoveAssignmentReleasesOldBlock)
{
  auto a = ByteBuffer::allocate(*pool, 64, 0);
  auto b = ByteBuffer::allocate(*pool, 64, 0);

  const size_t before = pool->available();
  a = std::move(b);

  // One block was returned (a's old block)
  EXPECT_EQ(pool->available(), before + 1);
  EXPECT_FALSE(b.valid());
  EXPECT_TRUE(a.valid());
}

TEST_F(ByteBufferTest, MovePreservesInitialHeadRoom)
{
  auto src = ByteBuffer::allocate(*pool, 64, 12);
  EXPECT_EQ(src.initial_headroom(), 12u);

  ByteBuffer dst(std::move(src));
  EXPECT_EQ(dst.initial_headroom(), 12u);
  EXPECT_EQ(src.initial_headroom(), 0u); // moved-from is reset
}

TEST_F(ByteBufferTest, DestructorReturnsBlockToPool)
{
  const size_t before = pool->available();
  {
    auto buf = ByteBuffer::allocate(*pool, 64, 16);
    EXPECT_EQ(pool->available(), before - 1);
  }
  EXPECT_EQ(pool->available(), before);
}

// ============================================================
// Typical RLC header prepend workflow
// ============================================================

TEST_F(ByteBufferTest, RlcStylePrependWorkflow)
{
  // Simulates building an RLC UMD PDU:
  //   1. allocate with 4-byte headroom
  //   2. append SDU payload
  //   3. prepend 2-byte UMD header (SN + FI fields)
  //   4. final buffer = header + payload, contiguous, no copies

  const uint8_t sdu_payload[] = {
      0x48, 0x65, 0x6C, 0x6C, 0x6F // "Hello"
  };
  const uint8_t rlc_header[] = {0x00, 0x05}; // mock SN=5 header

  auto buf = ByteBuffer::allocate(*pool, 64, 4);
  ASSERT_TRUE(buf.valid());

  buf.append(sdu_payload, sizeof(sdu_payload));
  buf.prepend(rlc_header, sizeof(rlc_header));

  EXPECT_EQ(buf.size(), 7u);
  EXPECT_EQ(buf.data()[0], 0x00); // header byte 0
  EXPECT_EQ(buf.data()[1], 0x05); // header byte 1
  EXPECT_EQ(buf.data()[2], 0x48); // 'H'
  EXPECT_EQ(buf.data()[6], 0x6F); // 'o'

  // Verify BufferView matches
  auto v = buf.view_all();
  EXPECT_EQ(v.len, 7u);
}
