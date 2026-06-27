// ============================================================
// pdcp_rlc_tm_loopback_test.cpp
//
// End-to-end loopback test for the pipeline:
//
//   App_A → PDCP_A → RLC_TM_Tx_A → MAC_A ─link─ MAC_B
//         → RLC_TM_Rx_B → PDCP_B → App_B
//
// And reverse:
//
//   App_B → PDCP_B → RLC_TM_Tx_B → MAC_B ─link─ MAC_A
//         → RLC_TM_Rx_A → PDCP_A → App_A
//
// Test cases:
//   1. SinglePdu_LosslessDelivery
//   2. Bidirectional_FullDuplex
//   3. MultiplePdus_FifoOrder
//   4. LossRate_BurstLoss_SomeDrop         (Gilbert-Elliott burst)
//   5. Delay_3tti_DeliveryAfterDelay
//   6. GrantTooSmall_SduStaysQueued
//   7. LargeBurst_ThroughputMeasured
//
// Notes:
//   - PDCP config: DRB_12bitSn + RlcMode::UM + reordering=false.
//     PDCP does not care about the RLC mode below it when reordering
//     is disabled — this is the pragmatic approach from the guide.
//   - RLC config below PDCP: TM (no header, no SN).
// ============================================================

#include <gtest/gtest.h>

#include "radio_bearer.h"
#include "mac_stub.h"
#include "buffer_pool.h"
#include "clock.h"
#include "test_helpers.h"

#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>

using namespace lte;
using namespace lte::test;

// ============================================================
// Shared config builders
// ============================================================

static RadioBearerConfig makeBearerConfig(LCID_t lcid = 1)
{
  RadioBearerConfig cfg;

  // PDCP: DRB 12-bit SN, UM mode (no reordering) — see note above
  cfg.pdcp.lcid = lcid;
  cfg.pdcp.bearer = BearerType::DRB;
  cfg.pdcp.pdu_type = PdcpPduType::DRB_12bitSn;
  cfg.pdcp.rlc_mode = RlcMode::UM;
  cfg.pdcp.reordering_enabled = false;

  // RLC: TM
  cfg.rlc_mode = RlcMode::TM;
  cfg.tm_cfg.lcid = lcid;
  cfg.tm_cfg.lc = LogicalChannel::DTCH;

  return cfg;
}

static MacStubConfig makeMacConfig(size_t tb_size = 1024,
                                   uint32_t delay_tti = 0,
                                   GilbertElliottConfig ge = GilbertElliottConfig::lossless(),
                                   uint32_t seed = 42)
{
  MacStubConfig cfg;
  cfg.tb_size_bytes = tb_size;
  cfg.tti_ms = 1;
  cfg.loss = ge;
  cfg.delay_tti = delay_tti;
  cfg.rng_seed = seed;
  return cfg;
}

// ============================================================
// Fixture — PdcpRlcTmLoopbackTest
// ============================================================

class PdcpRlcTmLoopbackTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    pool_ = std::make_unique<BufferPool>(2048, 1024);
    clk_ = std::make_unique<MockClock>();
  }

  // Build and wire a full loopback harness with the given configs.
  void buildHarness(MacStubConfig mac_cfg,
                    LCID_t lcid = 1)
  {
    delivered_A_.clear();
    delivered_B_.clear();

    bearer_A_ = std::make_unique<RadioBearer>(makeBearerConfig(lcid), *pool_, *clk_);
    bearer_B_ = std::make_unique<RadioBearer>(makeBearerConfig(lcid), *pool_, *clk_);

    mac_A_ = std::make_unique<MacStub>(mac_cfg, *clk_);
    mac_B_ = std::make_unique<MacStub>(mac_cfg, *clk_);

    mac_A_->attach_rlc_tx(bearer_A_->rlc_tx());
    mac_A_->attach_rlc_rx(bearer_A_->rlc_rx());
    mac_B_->attach_rlc_tx(bearer_B_->rlc_tx());
    mac_B_->attach_rlc_rx(bearer_B_->rlc_rx());
    mac_A_->pair(*mac_B_);

    // Hook deliver callbacks on both PDCP sides.
    bearer_A_->setDeliverCallback(
        [this](const uint8_t *d, size_t n)
        {
          delivered_A_.emplace_back(d, d + n);
        });
    bearer_B_->setDeliverCallback(
        [this](const uint8_t *d, size_t n)
        {
          delivered_B_.emplace_back(d, d + n);
        });
  }

  // Helper: make an SDU ByteBuffer from a string
  ByteBuffer makeSdu(const std::string &payload)
  {
    return makeSduBuffer(*pool_, payload);
  }

  // Drive one TTI: advance clock by 1 ms, tick both MACs.
  void runOneTti()
  {
    clk_->advance_ms(1);
    mac_A_->tick();
    mac_B_->tick();
  }

  // Run TTIs until both RLC Tx queues are empty or max_iter reached.
  void runUntilQuiescent(int max_iter = 200)
  {
    for (int i = 0; i < max_iter; ++i)
    {
      runOneTti();
      if (bearer_A_->rlc_tx()->bufferOccupancy() == 0 && bearer_B_->rlc_tx()->bufferOccupancy() == 0)
      {
        break;
      }
    }
    // One extra tick to drain any final delay-queue entries.
    runOneTti();
  }

  std::unique_ptr<BufferPool> pool_;
  std::unique_ptr<MockClock> clk_;
  std::unique_ptr<RadioBearer> bearer_A_;
  std::unique_ptr<RadioBearer> bearer_B_;
  std::unique_ptr<MacStub> mac_A_;
  std::unique_ptr<MacStub> mac_B_;

  // SDUs received by each side
  std::vector<std::vector<uint8_t>> delivered_A_;
  std::vector<std::vector<uint8_t>> delivered_B_;
};

// ============================================================
// Test 1 — SinglePdu_LosslessDelivery
// ============================================================
// A sends "hello" → B must receive exactly "hello".
TEST_F(PdcpRlcTmLoopbackTest, SinglePdu_LosslessDelivery)
{
  buildHarness(makeMacConfig(/*tb_size=*/1024));

  bearer_A_->txSdu(makeSdu("hello"));

  runUntilQuiescent();

  ASSERT_EQ(delivered_B_.size(), 1u);
  std::string received(reinterpret_cast<const char *>(delivered_B_[0].data()),
                       delivered_B_[0].size());
  EXPECT_EQ(received, "hello");
  EXPECT_TRUE(delivered_A_.empty()); // A sent, should not receive its own packet
}

// ============================================================
// Test 2 — Bidirectional_FullDuplex
// ============================================================
// A → "from_A", B → "from_B". Both must arrive at the opposite side.
TEST_F(PdcpRlcTmLoopbackTest, Bidirectional_FullDuplex)
{
  buildHarness(makeMacConfig(1024));

  bearer_A_->txSdu(makeSdu("from_A"));
  bearer_B_->txSdu(makeSdu("from_B"));

  runUntilQuiescent();

  ASSERT_EQ(delivered_B_.size(), 1u);
  ASSERT_EQ(delivered_A_.size(), 1u);

  std::string b_got(reinterpret_cast<const char *>(delivered_B_[0].data()),
                    delivered_B_[0].size());
  std::string a_got(reinterpret_cast<const char *>(delivered_A_[0].data()),
                    delivered_A_[0].size());

  EXPECT_EQ(b_got, "from_A");
  EXPECT_EQ(a_got, "from_B");
}

// ============================================================
// Test 3 — MultiplePdus_FifoOrder
// ============================================================
// A pushes 5 SDUs with distinct payloads. B must receive all 5
// in the same FIFO order.
TEST_F(PdcpRlcTmLoopbackTest, MultiplePdus_FifoOrder)
{
  buildHarness(makeMacConfig(1024));

  const std::vector<std::string> payloads = {
      "pkt_0", "pkt_1", "pkt_2", "pkt_3", "pkt_4"};

  for (const auto &p : payloads)
  {
    bearer_A_->txSdu(makeSdu(p));
  }

  runUntilQuiescent();

  ASSERT_EQ(delivered_B_.size(), payloads.size());

  for (size_t i = 0; i < payloads.size(); ++i)
  {
    std::string got(reinterpret_cast<const char *>(delivered_B_[i].data()),
                    delivered_B_[i].size());
    EXPECT_EQ(got, payloads[i]) << "Mismatch at index " << i;
  }
}

// ============================================================
// Test 4 — BurstLoss_SomeDrop (Gilbert-Elliott)
// ============================================================
// Use a burst-loss channel (G-E model): p_burst_entry=0.1,
// avg_burst_len=4, loss_in_burst=1.0.
// A pushes 200 SDUs. B must receive noticeably fewer than 200.
// We verify that drops happened (> 0) and not everything was
// dropped (< 200), and that MAC metrics are consistent.
TEST_F(PdcpRlcTmLoopbackTest, BurstLoss_SomeDrop)
{
  // Burst loss: ~10% burst entry, avg 4 PDUs per burst → ~28% steady-state loss
  auto ge = GilbertElliottConfig::burst(/*p_entry=*/0.1,
                                        /*avg_len=*/4.0,
                                        /*loss_in_burst=*/1.0);
  buildHarness(makeMacConfig(1024, /*delay=*/0, ge, /*seed=*/42));

  const int N = 200;
  for (int i = 0; i < N; ++i)
  {
    bearer_A_->txSdu(makeSdu("x"));
  }

  runUntilQuiescent(N + 50);

  const uint64_t sent = mac_A_->pdus_sent();
  const uint64_t dropped = mac_A_->pdus_dropped();
  const uint64_t delivered_count = static_cast<uint64_t>(delivered_B_.size());

  // Sanity: sent ≈ N (could be slightly less if some SDUs still queued)
  EXPECT_GE(sent, static_cast<uint64_t>(N) * 80 / 100); // at least 80% sent

  // Some drops must have occurred
  EXPECT_GT(dropped, 0u) << "Expected some burst drops";

  // Not everything was dropped
  EXPECT_GT(delivered_count, 0u) << "Expected some deliveries";

  // Consistency: sent = dropped + delivered (from MAC perspective)
  EXPECT_EQ(sent, dropped + mac_B_->pdus_delivered());
}

// ============================================================
// Test 5 — Delay_3tti_DeliveryAfterDelay
// ============================================================
// With delay_tti=3, B must NOT have received the packet after
// 1 and 2 ticks, but MUST have received it after 3 ticks.
TEST_F(PdcpRlcTmLoopbackTest, Delay_3tti_DeliveryAfterDelay)
{
  buildHarness(makeMacConfig(/*tb_size=*/1024, /*delay=*/3));

  bearer_A_->txSdu(makeSdu("delayed_pkt"));

  // Tick 1: MAC_A pulls PDU and pushes to MAC_B's delay queue
  runOneTti();
  EXPECT_TRUE(delivered_B_.empty()) << "Should not arrive after 1 TTI";

  // Tick 2: still in delay queue
  runOneTti();
  EXPECT_TRUE(delivered_B_.empty()) << "Should not arrive after 2 TTI";

  // Tick 3: still maturing
  runOneTti();
  EXPECT_TRUE(delivered_B_.empty()) << "Should not arrive after 3 TTIs (delay fires at next drain)";

  // Tick 4: drain_delivery_queue fires in mac_B_.tick(), now >= deliver_at_ns
  runOneTti();
  ASSERT_EQ(delivered_B_.size(), 1u) << "Should arrive after delay expires";

  std::string got(reinterpret_cast<const char *>(delivered_B_[0].data()),
                  delivered_B_[0].size());
  EXPECT_EQ(got, "delayed_pkt");
}

// ============================================================
// Test 6 — GrantTooSmall_SduStaysQueued
// ============================================================
// tb_size=10 bytes, but SDU = 50 bytes.
// After many TTIs, A's queue must still show the SDU because
// grant is always too small for TM to transmit.
// B must receive nothing.
TEST_F(PdcpRlcTmLoopbackTest, GrantTooSmall_SduStaysQueued)
{
  buildHarness(makeMacConfig(/*tb_size=*/10));

  // PDCP adds a 2-byte header → RLC payload = 52 bytes > 10-byte grant
  bearer_A_->txSdu(makeSdu(std::string(50, 'Z')));

  for (int i = 0; i < 10; ++i)
  {
    runOneTti();
  }

  // RLC Tx queue must still hold the SDU (PDCP PDU = payload + header)
  EXPECT_GT(bearer_A_->rlc_tx()->bufferOccupancy(), 0u)
      << "SDU should remain queued when grant is too small";

  // B must have received nothing
  EXPECT_TRUE(delivered_B_.empty());
}

// ============================================================
// Test 7 — LargeBurst_ThroughputMeasured
// ============================================================
// A pushes 500 SDUs of 100 bytes each. Lossless channel with
// large grant. Verify total bytes_sent approximately matches
// total payload + PDCP header overhead.
TEST_F(PdcpRlcTmLoopbackTest, LargeBurst_ThroughputMeasured)
{
  buildHarness(makeMacConfig(/*tb_size=*/8192));

  const int N = 500;
  const size_t sdu_size = 100;

  for (int i = 0; i < N; ++i)
  {
    bearer_A_->txSdu(makeSdu(std::string(sdu_size, static_cast<char>('A' + (i % 26)))));
  }

  runUntilQuiescent(N + 50);

  // All must be delivered to B
  EXPECT_EQ(delivered_B_.size(), static_cast<size_t>(N));

  // MAC sent == delivered (no loss)
  EXPECT_EQ(mac_A_->pdus_dropped(), 0u);
  EXPECT_EQ(mac_A_->pdus_sent(), mac_B_->pdus_delivered());

  // Total bytes sent = N * (sdu_size + PDCP header).
  // DRB_12bitSn header = 2 bytes → 102 bytes per PDU.
  // With large grant, multiple SDUs may fit in one TB, but TM
  // submits one SDU per grant, so PDU count = N.
  const uint64_t expected_bytes = static_cast<uint64_t>(N) * (sdu_size + 2 /*PDCP hdr*/);
  EXPECT_EQ(mac_A_->bytes_sent(), expected_bytes);

  // Pool must not have leaked blocks: all ByteBuffers
  // that went through the pipeline have been released.
  // (pool_.available() check — depends on test_helpers not holding refs.)
}
