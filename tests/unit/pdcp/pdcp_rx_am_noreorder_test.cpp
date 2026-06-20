// ============================================================
// pdcp_rx_am_noreorder_test.cpp
//
// Unit tests for §5.1.2.1.2 (DRBs on RLC AM, no reordering).
// Two test surfaces, each with its own fixture:
//
//   A. RxAmNoReorderTest — drives PdcpEntity via raw PDUs built
//      with buildRawPduBuffer(). Allows seeding arbitrary RX state
//      via resetWithState(). Covers classification cases A/B/C/D/E,
//      delivery logic, re-establishment, wrap-around, and duplicates.
//
//   B. AmNoReorderLoopbackTest — drives PdcpEntity end-to-end through
//      txSdu(ByteBuffer) → MockRlcSap → rxPdu(ByteBuffer).
//      Used for façade-level assertions.
//
// Refactored for zero-copy SAP:
//   - PdcpConfig struct replaces positional constructor args
//   - rxPdu(ByteBuffer) replaces rxPdu(raw*, len, bool)
//   - txSdu(ByteBuffer) replaces txSdu(raw*, len)
//   - MockRlcSap replaces lastTxPdu()
//   - ByteBuffer allocated from shared pool
// ============================================================

#include <gtest/gtest.h>

#include "pdcp_entity.h"
#include "pdcp_pdu.h"
#include "buffer_pool.h"
#include "test_helpers.h"

#include <vector>
#include <string>
#include <cstdint>

using namespace lte;
using namespace lte::test;

// ============================================================
// Shared config builder
// ============================================================
static PdcpConfig makeAmDrbConfig()
{
  PdcpConfig cfg;
  cfg.lcid = 1;
  cfg.bearer = BearerType::DRB;
  cfg.pdu_type = PdcpPduType::DRB_12bitSn;
  cfg.rlc_mode = RlcMode::AM;
  cfg.reordering_enabled = false;
  return cfg;
}

// ============================================================
// Surface A — RxAmNoReorderTest
// ============================================================

class RxAmNoReorderTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    pool_ = std::make_unique<BufferPool>(2048, 64);
    entity_ = std::make_unique<PdcpEntity>(makeAmDrbConfig());

    entity_->setDeliverCallback([this](const uint8_t *sdu, size_t len)
                                { delivered.emplace_back(sdu, sdu + len); });
  }

  // Build raw PDU as ByteBuffer and inject into entity
  Status rx(SN_t sn, const std::string &payload = "X",
            bool reestablish = false)
  {
    auto buf = buildRawPduBuffer(*pool_, sn, payload);
    EXPECT_TRUE(buf.valid());
    return entity_->rxPdu(std::move(buf), reestablish);
  }

  // Rebuild entity with custom initial RX state
  void resetWithState(SN_t rx_next, uint32_t rx_hfn, SN_t rx_deliv)
  {
    delivered.clear();
    PdcpEntity::TestInitState init{};
    init.rx_next = rx_next;
    init.rx_hfn = rx_hfn;
    init.rx_deliv = rx_deliv;
    init.tx_next = 0;
    init.tx_hfn = 0;

    entity_ = std::make_unique<PdcpEntity>(makeAmDrbConfig(), init);

    entity_->setDeliverCallback([this](const uint8_t *sdu, size_t len)
                                { delivered.emplace_back(sdu, sdu + len); });
  }

  std::unique_ptr<BufferPool> pool_;
  std::unique_ptr<PdcpEntity> entity_;

  std::vector<std::vector<uint8_t>> delivered;
};

// ============================================================
// Surface B — AmNoReorderLoopbackTest
// ============================================================

class AmNoReorderLoopbackTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    pool_ = std::make_unique<BufferPool>(2048, 64);
    entity_ = std::make_unique<PdcpEntity>(makeAmDrbConfig());
    entity_->set_lower_layer_sap(&mock_rlc_);

    entity_->setDeliverCallback([this](const uint8_t *sdu, size_t len)
                                { delivered.emplace_back(sdu, sdu + len); });
  }

  // Transmit one SDU, capture the PDU bytes from MockRlcSap
  std::vector<uint8_t> makePdu(const std::string &payload)
  {
    auto sdu = makeSduBuffer(*pool_, payload);
    entity_->txSdu(std::move(sdu));
    return mock_rlc_.last_sdu;
  }

  // Build N PDUs, return them without feeding to Rx side
  std::vector<std::vector<uint8_t>> makeNPdus(int n,
                                              const std::string &base = "p")
  {
    std::vector<std::vector<uint8_t>> pdus;
    for (int i = 0; i < n; ++i)
      pdus.push_back(makePdu(base + std::to_string(i)));
    return pdus;
  }

  // Re-inject captured PDU bytes into rxPdu as ByteBuffer
  Status reinjectPdu(const std::vector<uint8_t> &pdu_bytes,
                     bool reestablish = false)
  {
    auto buf = ByteBuffer::allocate(*pool_, pdu_bytes.size(), DEFAULT_HEADROOM);
    buf.append(pdu_bytes.data(), pdu_bytes.size());
    return entity_->rxPdu(std::move(buf), reestablish);
  }

  std::unique_ptr<BufferPool> pool_;
  MockRlcSap mock_rlc_;
  std::unique_ptr<PdcpEntity> entity_;
  std::vector<std::vector<uint8_t>> delivered;
};

// ============================================================
// A. PdcpEntity (AmNoReorder mode) tests — Surface A
// ============================================================

// ------------------------------------------------------------
// A1. Initial state — §5.2.2.1
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, InitialState_MatchesSpec)
{
  EXPECT_EQ(entity_->rxNext(), 0u);
  EXPECT_EQ(entity_->rxHfn(), 0u);
  EXPECT_EQ(entity_->rxDeliv(), static_cast<SN_t>(SN_MAX_12BIT - 1)); // 4095
}

// ------------------------------------------------------------
// A2. Case D — ForwardInWindow: normal single-packet delivery
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, CaseD_NormalForward_DeliveredImmediately)
{
  ASSERT_EQ(rx(0, "hello"), Status::OK);
  ASSERT_EQ(delivered.size(), 1u);
  EXPECT_EQ(std::string(delivered[0].begin(), delivered[0].end()), "hello");
  EXPECT_EQ(entity_->rxNext(), 1u);
  EXPECT_EQ(entity_->rxDeliv(), 0u);
}

// ------------------------------------------------------------
// A3. Case E — BehindSameHfn: late arrival, same HFN
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, CaseE_BehindSameHfn_UsesCorrectHfn)
{
  resetWithState(/*rx_next*/ 4, /*rx_hfn*/ 0, /*rx_deliv*/ 1);
  ASSERT_EQ(rx(2, "p2"), Status::OK);

  ASSERT_EQ(delivered.size(), 1u);
  EXPECT_EQ(entity_->rxDeliv(), SN_t{2});
  EXPECT_EQ(entity_->rxNext(), SN_t{4});
  EXPECT_EQ(entity_->rxHfn(), 0u);
}

// ------------------------------------------------------------
// A4. Case A — OutsideWindow (vere 2): SN == rx_deliv_ → discard
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, CaseA_Vere2_SnEqualsRxDeliv_Discarded)
{
  ASSERT_EQ(rx(4095, "dup"), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);
  EXPECT_EQ(entity_->rxNext(), 0u);
  EXPECT_EQ(entity_->rxDeliv(), SN_t{SN_MAX_12BIT - 1});
  EXPECT_EQ(entity_->rxHfn(), 0u);
}

// ------------------------------------------------------------
// A5. Case A — OutsideWindow (vere 2): duplicate same PDU
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, CaseA_Vere2_DuplicatePdu_Discarded)
{
  ASSERT_EQ(rx(0, "first"), Status::OK);
  ASSERT_EQ(delivered.size(), 1u);

  ASSERT_EQ(rx(0, "dup"), Status::OK);
  EXPECT_EQ(delivered.size(), 1u);
  EXPECT_EQ(entity_->rxDeliv(), SN_t{0});
  EXPECT_EQ(entity_->rxNext(), SN_t{1});
}

// ------------------------------------------------------------
// A6. Case A — OutsideWindow (vere 2): SN just inside boundary
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, CaseA_Vere2_Boundary_SnAtRxDelivPlusWindow_Discarded)
{
  ASSERT_EQ(rx(0), Status::OK);
  ASSERT_EQ(rx(2048), Status::OK);
  EXPECT_EQ(entity_->rxDeliv(), SN_t{2048});
}

// ------------------------------------------------------------
// A7. Case A — OutsideWindow: one step beyond the window
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, CaseA_Vere1_SnOneAheadOfWindow_Discarded)
{
  ASSERT_EQ(rx(0), Status::OK);
  const SN_t rx_deliv_before = entity_->rxDeliv();
  ASSERT_EQ(rx(2049, "discard"), Status::OK);
  EXPECT_EQ(entity_->rxDeliv(), rx_deliv_before);
  EXPECT_EQ(delivered.size(), 1u);
}

// ------------------------------------------------------------
// A8. Case A after wrap-around (vere 1 spec)
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, CaseA_AfterWrapAround_Vere1_StateUnchanged)
{
  resetWithState(/*rx_next=*/11, /*rx_hfn=*/1, /*rx_deliv=*/10);

  ASSERT_EQ(rx(4090, "discard"), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);
  EXPECT_EQ(entity_->rxNext(), SN_t{11});
  EXPECT_EQ(entity_->rxHfn(), 1u);
  EXPECT_EQ(entity_->rxDeliv(), SN_t{10});
}

// ------------------------------------------------------------
// A9. Case A duplicate same HFN (vere 2 spec)
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, CaseA_DuplicateSameHfn_Vere2_StateUnchanged)
{
  resetWithState(/*rx_next=*/101, /*rx_hfn=*/0, /*rx_deliv=*/100);

  ASSERT_EQ(rx(50, "old"), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);
  EXPECT_EQ(entity_->rxNext(), SN_t{101});
  EXPECT_EQ(entity_->rxHfn(), 0u);
  EXPECT_EQ(entity_->rxDeliv(), SN_t{100});
}

// ------------------------------------------------------------
// A10. Case D — wrap-around: SN=4095 → rx_next_=0, rx_hfn_++
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, CaseD_WrapAround_HfnIncrements)
{
  resetWithState(/*rx_next=*/4095, /*rx_hfn=*/0, /*rx_deliv=*/4094);

  ASSERT_EQ(rx(4095, "wrap"), Status::OK);

  EXPECT_EQ(entity_->rxNext(), SN_t{0});
  EXPECT_EQ(entity_->rxHfn(), 1u);
  EXPECT_EQ(entity_->rxDeliv(), SN_t{4095});
  EXPECT_EQ(delivered.size(), 1u);
}

// ------------------------------------------------------------
// A11. Case B — WrapAhead: rx_hfn_ committed permanently
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, CaseB_WrapAhead_HfnCommittedToState)
{
  resetWithState(/*rx_next=*/2050, /*rx_hfn=*/0, /*rx_deliv=*/2049);

  ASSERT_EQ(rx(1, "wrapB"), Status::OK);

  EXPECT_EQ(entity_->rxHfn(), 1u);
  EXPECT_EQ(entity_->rxNext(), SN_t{2});
  EXPECT_EQ(delivered.size(), 1u);
}

// ------------------------------------------------------------
// A12. Normal delivery — store-then-flush (Nhánh A)
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, NormalDelivery_StoreThenFlush_OldStoredAndCurrent)
{
  resetWithState(/*rx_next=*/4, /*rx_hfn=*/0, /*rx_deliv=*/3);

  ASSERT_EQ(rx(5, "sdu5", /*reestablish=*/true), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);

  ASSERT_EQ(rx(7, "sdu7", /*reestablish=*/false), Status::OK);

  ASSERT_EQ(delivered.size(), 2u);
  EXPECT_EQ(std::string(delivered[0].begin(), delivered[0].end()), "sdu5");
  EXPECT_EQ(std::string(delivered[1].begin(), delivered[1].end()), "sdu7");

  EXPECT_EQ(entity_->rxDeliv(), SN_t{7});
}

// ------------------------------------------------------------
// A13. Re-establishment — simple next (is_next true → deliver)
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, Reestablishment_SimpleNext_Delivered)
{
  resetWithState(11, 0, 10);

  ASSERT_EQ(rx(11, "sdu11", /*reestablish=*/true), Status::OK);
  ASSERT_EQ(delivered.size(), 1u);
  EXPECT_EQ(entity_->rxDeliv(), SN_t{11});
}

// ------------------------------------------------------------
// A14. Re-establishment — SN not next → stored, not delivered
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, Reestablishment_NotNext_Stored_NotDelivered)
{
  resetWithState(11, 0, 10);

  ASSERT_EQ(rx(13, "sdu13", /*reestablish=*/true), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);
  EXPECT_EQ(entity_->rxDeliv(), SN_t{10});
}

// ------------------------------------------------------------
// A15. Re-establishment — store-then-flush consecutive
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, Reestablishment_StoreThenFlushConsecutive)
{
  resetWithState(11, 0, 10);

  ASSERT_EQ(rx(13, "sdu13", true), Status::OK);
  ASSERT_EQ(rx(14, "sdu14", true), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);

  ASSERT_EQ(rx(11, "sdu11", true), Status::OK);
  EXPECT_EQ(delivered.size(), 1u);
  EXPECT_EQ(entity_->rxDeliv(), SN_t{11});

  ASSERT_EQ(rx(12, "sdu12", true), Status::OK);
  ASSERT_EQ(delivered.size(), 4u);
  EXPECT_EQ(std::string(delivered[1].begin(), delivered[1].end()), "sdu12");
  EXPECT_EQ(std::string(delivered[2].begin(), delivered[2].end()), "sdu13");
  EXPECT_EQ(std::string(delivered[3].begin(), delivered[3].end()), "sdu14");
  EXPECT_EQ(entity_->rxDeliv(), SN_t{14});
}

// ------------------------------------------------------------
// A16. Re-establishment — wrap-around: rx_deliv_=4095, SN=0
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, Reestablishment_WrapAround_Vere2_Delivered)
{
  const SN_t max_sn = static_cast<SN_t>(SN_MAX_12BIT - 1);
  resetWithState(/*rx_next=*/0, /*rx_hfn=*/1, /*rx_deliv=*/max_sn);

  ASSERT_EQ(rx(0, "sdu0", /*reestablish=*/true), Status::OK);
  ASSERT_EQ(delivered.size(), 1u);
  EXPECT_EQ(entity_->rxDeliv(), SN_t{0});
}

// ------------------------------------------------------------
// A17. Re-establishment — duplicate in store
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, Reestablishment_DuplicateInStore_Discarded)
{
  resetWithState(11, 0, 10);

  ASSERT_EQ(rx(13, "first", true), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);

  ASSERT_EQ(rx(13, "dup", true), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);
  EXPECT_EQ(entity_->rxDeliv(), SN_t{10});
}

// ------------------------------------------------------------
// A18. Normal delivery — empty buffer: single PDU delivered immediately
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, NormalDelivery_EmptyBuffer_ImmediateDeliver)
{
  ASSERT_EQ(rx(0, "only"), Status::OK);
  ASSERT_EQ(delivered.size(), 1u);
  EXPECT_EQ(std::string(delivered[0].begin(), delivered[0].end()), "only");
}

// ------------------------------------------------------------
// A19. Reestablish() — clears rx_store_, ROHC reset
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, Reestablish_ClearsStore)
{
  resetWithState(11, 0, 10);

  ASSERT_EQ(rx(13, "stored", true), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);

  entity_->reestablish();

  ASSERT_EQ(rx(11, "new11", true), Status::OK);
  EXPECT_EQ(delivered.size(), 1u);
}

// ------------------------------------------------------------
// A20. Reestablish() with stored context — resets all state
// ------------------------------------------------------------

TEST_F(RxAmNoReorderTest, Reestablish_StoredContext_ResetsState)
{
  resetWithState(11, 0, 10);

  resetWithState(/*rx_next=*/0, /*rx_hfn=*/0,
                 /*rx_deliv=*/static_cast<SN_t>(SN_MAX_12BIT - 1));

  EXPECT_EQ(entity_->rxNext(), SN_t{0});
  EXPECT_EQ(entity_->rxHfn(), 0u);
  EXPECT_EQ(entity_->rxDeliv(), static_cast<SN_t>(SN_MAX_12BIT - 1));
}

// ============================================================
// B. PdcpEntity façade tests — Surface B
// ============================================================

// ------------------------------------------------------------
// B1. Constructor — initial rxDeliv matches §5.2.2.1
// ------------------------------------------------------------

TEST_F(AmNoReorderLoopbackTest, Constructor_AmNoReorder_InitialStateCorrect)
{
  EXPECT_EQ(entity_->rxNext(), SN_t{0});
  EXPECT_EQ(entity_->rxHfn(), 0u);
  EXPECT_EQ(entity_->rxDeliv(), static_cast<SN_t>(SN_MAX_12BIT - 1));
}

// ------------------------------------------------------------
// B2. Constructor with reordering enabled — rxPdu returns NOT_IMPLEMENTED
// ------------------------------------------------------------

TEST(EntityFacadeTest, Constructor_WithReorder_ReturnsNotImplemented)
{
  BufferPool pool(2048, 8);

  PdcpConfig cfg;
  cfg.lcid = 1;
  cfg.bearer = BearerType::DRB;
  cfg.pdu_type = PdcpPduType::DRB_12bitSn;
  cfg.rlc_mode = RlcMode::AM;
  cfg.reordering_enabled = true;

  PdcpEntity e(cfg);

  // Build a minimal valid PDU as ByteBuffer
  const uint8_t dummy[] = {0x80, 0x00, 'X'};
  auto buf = ByteBuffer::allocate(pool, sizeof(dummy), DEFAULT_HEADROOM);
  buf.append(dummy, sizeof(dummy));

  EXPECT_EQ(e.rxPdu(std::move(buf)), Status::NOT_IMPLEMENTED);
}

// ------------------------------------------------------------
// B3. Pass-through rxPdu — PDU flows through entity to procedure
// ------------------------------------------------------------

TEST_F(AmNoReorderLoopbackTest, Passthrough_RxPdu_DeliveryWorks)
{
  auto pdu = makePdu("loopback");
  ASSERT_EQ(reinjectPdu(pdu), Status::OK);

  const auto &sdu = entity_->lastDeliveredSdu();
  EXPECT_EQ(std::string(sdu.begin(), sdu.end()), "loopback");
}

// ------------------------------------------------------------
// B4. RX accessors — entity returns direct member values
// ------------------------------------------------------------

TEST_F(AmNoReorderLoopbackTest, Passthrough_RxAccessors_MatchProcedureState)
{
  auto pdu = makePdu("state_check");
  ASSERT_EQ(reinjectPdu(pdu), Status::OK);

  EXPECT_EQ(entity_->rxNext(), SN_t{1});
  EXPECT_EQ(entity_->rxDeliv(), SN_t{0});
  EXPECT_EQ(entity_->rxHfn(), 0u);
}

// ------------------------------------------------------------
// B5. Reestablish pass-through
// ------------------------------------------------------------

TEST_F(AmNoReorderLoopbackTest, Reestablish_Passthrough_DelegatesToProcedure)
{
  auto pdu = makePdu("a");
  ASSERT_EQ(reinjectPdu(pdu), Status::OK);

  entity_->reestablish();

  EXPECT_EQ(entity_->rxDeliv(), SN_t{0});
}

// ------------------------------------------------------------
// B6. Due_to_reestablishment flag forwarded correctly
// ------------------------------------------------------------

TEST_F(AmNoReorderLoopbackTest, Passthrough_ReestablishmentFlag_ForwardedToProcedure)
{
  auto pdu = makePdu("reestab");

  ASSERT_EQ(reinjectPdu(pdu, /*reestablish=*/true), Status::OK);
  EXPECT_EQ(delivered.size(), 1u);
}

// ------------------------------------------------------------
// B7. Loopback: 10 in-order PDUs all delivered
// ------------------------------------------------------------

TEST_F(AmNoReorderLoopbackTest, Loopback_TenPackets_AllDelivered)
{
  auto pdus = makeNPdus(10);
  for (auto &p : pdus)
  {
    ASSERT_EQ(reinjectPdu(p), Status::OK);
  }
  EXPECT_EQ(delivered.size(), 10u);
}

// ------------------------------------------------------------
// B8. Loopback: out-of-order delivery
// ------------------------------------------------------------

TEST_F(AmNoReorderLoopbackTest, Loopback_OutOfOrder_DeliveredInSNOrder)
{
  auto p0 = makePdu("p0");
  auto p1 = makePdu("p1");
  auto p2 = makePdu("p2");
  auto p3 = makePdu("p3");

  reinjectPdu(p0);
  reinjectPdu(p1);
  reinjectPdu(p3); // gap at p2
  EXPECT_EQ(reinjectPdu(p2), Status::OK);

  ASSERT_EQ(delivered.size(), 3u);

  EXPECT_EQ(std::string(delivered[0].begin(), delivered[0].end()), "p0");
  EXPECT_EQ(std::string(delivered[1].begin(), delivered[1].end()), "p1");
  EXPECT_EQ(std::string(delivered[2].begin(), delivered[2].end()), "p3");
  EXPECT_EQ(entity_->getMetrics().dropped_packets, 1u);
}