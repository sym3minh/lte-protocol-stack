// ============================================================
// rlc_tm_test.cpp
//
// Unit tests for RlcTmTxEntity and RlcTmRxEntity.
//
// Test cases:
//   Tx:
//   1.  TxEmptyQueue_BuildPduReturnsInvalid
//   2.  TxHandleSdu_QueueGrows
//   3.  TxBuildPdu_FifoOrder
//   4.  TxBuildPdu_GrantTooSmall_KeepsInQueue
//   5.  TxBuildPdu_ExactGrant
//   6.  TxReestablish_ClearsQueue
//   10. TxBuildPdu_ZeroCopyVerification
//
//   Rx:
//   7.  RxRxPdu_ForwardsToNotifier
//   8.  RxRxPdu_NoNotifierSet_ReturnsError
//   9.  RxRxPdu_InvalidBuffer_ReturnsError
// ============================================================

#include <gtest/gtest.h>

#include "rlc_tm.h"
#include "buffer_pool.h"
#include "clock.h"
#include "test_helpers.h"

#include <string>
#include <vector>
#include <cstdint>

using namespace lte;
using namespace lte::test;

// ============================================================
// Fixture
// ============================================================
class RlcTmTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        pool_ = std::make_unique<BufferPool>(2048 /*block_size*/, 256 /*num_blocks*/);
        clk_  = std::make_unique<MockClock>();

        cfg_      = RlcTmConfig{};
        cfg_.lcid = 0;
        cfg_.lc   = LogicalChannel::CCCH;
    }

    // Helper: build an SDU ByteBuffer from a string
    ByteBuffer makeSdu(const std::string& payload)
    {
        return makeSduBuffer(*pool_, payload);
    }

    std::unique_ptr<BufferPool> pool_;
    std::unique_ptr<MockClock>  clk_;
    RlcTmConfig                 cfg_;
};

// ============================================================
// Tx tests
// ============================================================

// ── Test 1 ──────────────────────────────────────────────────
// Empty queue → buildPdu with any grant returns invalid buffer
TEST_F(RlcTmTest, TxEmptyQueue_BuildPduReturnsInvalid)
{
    RlcTmTxEntity tx(cfg_, *pool_, *clk_);

    ByteBuffer pdu = tx.buildPdu(1024);
    EXPECT_FALSE(pdu.valid());
    EXPECT_EQ(tx.bufferOccupancy(), 0u);
}

// ── Test 2 ──────────────────────────────────────────────────
// handle_sdu: occupancy grows correctly as SDUs are enqueued
TEST_F(RlcTmTest, TxHandleSdu_QueueGrows)
{
    RlcTmTxEntity tx(cfg_, *pool_, *clk_);

    tx.handle_sdu(makeSdu("abc"),   0); // 3 bytes
    tx.handle_sdu(makeSdu("hello"), 0); // 5 bytes
    tx.handle_sdu(makeSdu("world"), 0); // 5 bytes

    EXPECT_EQ(tx.bufferOccupancy(), 13u);
}

// ── Test 3 ──────────────────────────────────────────────────
// buildPdu returns SDUs in FIFO order
TEST_F(RlcTmTest, TxBuildPdu_FifoOrder)
{
    RlcTmTxEntity tx(cfg_, *pool_, *clk_);

    tx.handle_sdu(makeSdu("AAA"), 0);
    tx.handle_sdu(makeSdu("BBB"), 0);
    tx.handle_sdu(makeSdu("CCC"), 0);

    ByteBuffer p1 = tx.buildPdu(1024);
    ByteBuffer p2 = tx.buildPdu(1024);
    ByteBuffer p3 = tx.buildPdu(1024);
    ByteBuffer p4 = tx.buildPdu(1024); // queue now empty

    ASSERT_TRUE(p1.valid());
    ASSERT_TRUE(p2.valid());
    ASSERT_TRUE(p3.valid());
    EXPECT_FALSE(p4.valid());

    EXPECT_EQ(std::string(reinterpret_cast<const char*>(p1.data()), p1.size()), "AAA");
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(p2.data()), p2.size()), "BBB");
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(p3.data()), p3.size()), "CCC");

    EXPECT_EQ(tx.bufferOccupancy(), 0u);
}

// ── Test 4 ──────────────────────────────────────────────────
// buildPdu with grant < SDU size: returns invalid, SDU stays queued
// Option B from guide: no drop — wait for a bigger grant next TTI.
TEST_F(RlcTmTest, TxBuildPdu_GrantTooSmall_KeepsInQueue)
{
    RlcTmTxEntity tx(cfg_, *pool_, *clk_);

    // SDU is 50 bytes
    std::string payload(50, 'X');
    tx.handle_sdu(makeSdu(payload), 0);

    EXPECT_EQ(tx.bufferOccupancy(), 50u);

    // Issue grant of only 20 bytes — too small
    ByteBuffer pdu = tx.buildPdu(20);
    EXPECT_FALSE(pdu.valid());

    // SDU must still be in queue
    EXPECT_EQ(tx.bufferOccupancy(), 50u);
    EXPECT_EQ(tx.grantTooSmallCount(), 1u);
}

// ── Test 5 ──────────────────────────────────────────────────
// buildPdu with exact grant == SDU size: returns valid buffer,
// queue becomes empty
TEST_F(RlcTmTest, TxBuildPdu_ExactGrant)
{
    RlcTmTxEntity tx(cfg_, *pool_, *clk_);

    std::string payload(50, 'Y');
    tx.handle_sdu(makeSdu(payload), 0);

    ByteBuffer pdu = tx.buildPdu(50); // exact fit
    ASSERT_TRUE(pdu.valid());
    EXPECT_EQ(pdu.size(), 50u);
    EXPECT_EQ(tx.bufferOccupancy(), 0u);
}

// ── Test 6 ──────────────────────────────────────────────────
// reestablish clears all queued SDUs and resets occupancy
TEST_F(RlcTmTest, TxReestablish_ClearsQueue)
{
    RlcTmTxEntity tx(cfg_, *pool_, *clk_);

    tx.handle_sdu(makeSdu("one"),   0);
    tx.handle_sdu(makeSdu("two"),   0);
    tx.handle_sdu(makeSdu("three"), 0);
    EXPECT_GT(tx.bufferOccupancy(), 0u);

    tx.reestablish();

    EXPECT_EQ(tx.bufferOccupancy(), 0u);

    // After reestablish, buildPdu must return invalid
    ByteBuffer pdu = tx.buildPdu(1024);
    EXPECT_FALSE(pdu.valid());
}

// ── Test 10 ─────────────────────────────────────────────────
// Zero-copy verification: the data pointer of the ByteBuffer
// returned by buildPdu must equal the pointer at handle_sdu time.
// This confirms no memcpy occurs through the queue.
TEST_F(RlcTmTest, TxBuildPdu_ZeroCopyVerification)
{
    RlcTmTxEntity tx(cfg_, *pool_, *clk_);

    ByteBuffer sdu = makeSdu("hello");
    ASSERT_TRUE(sdu.valid());

    // Capture pointer BEFORE moving into handle_sdu
    const uint8_t* before_ptr = sdu.data();

    tx.handle_sdu(std::move(sdu), 0);

    ByteBuffer out = tx.buildPdu(1024);
    ASSERT_TRUE(out.valid());

    // Same block pointer — no copy was made
    EXPECT_EQ(out.data(), before_ptr);
}

// ============================================================
// Rx tests
// ============================================================

// ── Test 7 ──────────────────────────────────────────────────
// rxPdu delivers PDU bytes to the wired notifier
TEST_F(RlcTmTest, RxRxPdu_ForwardsToNotifier)
{
    RlcTmRxEntity rx(cfg_, *clk_);

    MockUpperLayerNotifier notifier;
    rx.set_upper_data_notifier(&notifier);

    std::string payload = "hello_rlc_tm";
    ByteBuffer pdu = makeSdu(payload);
    ASSERT_TRUE(pdu.valid());

    Status s = rx.rxPdu(std::move(pdu));

    EXPECT_EQ(s, Status::OK);
    EXPECT_EQ(notifier.call_count, 1u);

    std::string received(reinterpret_cast<const char*>(notifier.last_pdu.data()),
                         notifier.last_pdu.size());
    EXPECT_EQ(received, payload);
}

// ── Test 8 ──────────────────────────────────────────────────
// rxPdu without notifier wired → PARSE_ERROR, no crash
TEST_F(RlcTmTest, RxRxPdu_NoNotifierSet_ReturnsError)
{
    RlcTmRxEntity rx(cfg_, *clk_);
    // Deliberately do NOT call set_upper_data_notifier()

    ByteBuffer pdu = makeSdu("test");
    Status s = rx.rxPdu(std::move(pdu));
    EXPECT_EQ(s, Status::PARSE_ERROR);
}

// ── Test 9 ──────────────────────────────────────────────────
// rxPdu with invalid (default-constructed) ByteBuffer → PARSE_ERROR
TEST_F(RlcTmTest, RxRxPdu_InvalidBuffer_ReturnsError)
{
    RlcTmRxEntity rx(cfg_, *clk_);

    MockUpperLayerNotifier notifier;
    rx.set_upper_data_notifier(&notifier);

    Status s = rx.rxPdu(ByteBuffer{}); // explicitly invalid
    EXPECT_EQ(s, Status::PARSE_ERROR);
    EXPECT_EQ(notifier.call_count, 0u); // notifier must NOT be called
}
