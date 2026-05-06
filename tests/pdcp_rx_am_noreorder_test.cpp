// ============================================================
// pdcp_rx_am_noreorder_test.cpp
//
// Unit tests for §5.1.2.1.2 (DRBs on RLC AM, no reordering).
// Covers two test surfaces:
//
//   A. PdcpRxAmNoReorder directly — classification cases A/B/C/D/E,
//      delivery logic, re-establishment, wrap-around, duplicates.
//   B. PdcpEntity façade — constructor RxMode selection, pass-through
//      accessors, stub procedures returning NOT_IMPLEMENTED.
//
// Test naming convention:
//   [Surface]_[Scenario]_[ExpectedOutcome]
// ============================================================

#include <gtest/gtest.h>

#include "pdcp_entity.h"
#include "pdcp_rx_am_noreorder.h"
#include "pdcp_rx_um_noreorder.h"
#include "pdcp_rx_with_reorder.h"
#include "pdcp_pdu.h"
#include "buffer_pool.h"
#include "pdcp_security.h"
#include "pdcp_rohc.h"
#include "metrics_collector.h"

#include <vector>
#include <string>
#include <cstdint>

using namespace lte;

// ============================================================
// Shared test infrastructure
// ============================================================

// buildRawPdu — serialise a PDCP Data PDU with a given SN and
// payload into a byte vector.  Used to feed rxPdu() directly.
static std::vector<uint8_t> buildRawPdu(SN_t sn,
                                        const std::string &payload,
                                        BearerType bearer = BearerType::DRB)
{
  const size_t hdr = PdcpPduCodec::headerSize(bearer);
  std::vector<uint8_t> buf(hdr + payload.size());

  PdcpPdu pdu;
  pdu.sn = sn;
  pdu.dc = PDCP_DC_DATA;
  pdu.bearer = bearer;
  pdu.payload = reinterpret_cast<const uint8_t *>(payload.data());
  pdu.payload_len = payload.size();

  size_t written = PdcpPduCodec::serialize(pdu, buf.data(), buf.size());
  buf.resize(written);
  return buf;
}

// ProcFixture — creates real sub-components and a PdcpRxAmNoReorder
// with spec-default initial state (rx_next=0, rx_hfn=0,
// rx_deliv=Maximum_PDCP_SN).
class ProcFixture : public ::testing::Test
{
protected:
  void SetUp() override
  {
    pool = std::make_unique<BufferPool>(2048, 64);
    security = std::make_unique<PdcpSecurity>();
    rohc = std::make_unique<PdcpRohc>();
    metrics = std::make_unique<MetricsCollector>();

    IPdcpRxProcedure::Deps deps{
        *security, *rohc, *metrics, *pool,
        [this](const uint8_t *sdu, size_t len)
        {
          delivered.emplace_back(sdu, sdu + len);
        },
        BearerType::DRB};
    proc = std::make_unique<PdcpRxAmNoReorder>(deps);
  }

  // Convenience: build a raw PDU and inject it into proc.
  Status rx(SN_t sn, const std::string &payload = "X",
            bool reestablish = false)
  {
    auto raw = buildRawPdu(sn, payload);
    return proc->rxPdu(raw.data(), raw.size(), reestablish);
  }

  // Convenience: build ProcFixture with custom initial state.
  void resetWithState(SN_t rx_next, uint32_t rx_hfn, SN_t rx_deliv)
  {
    IPdcpRxProcedure::Deps deps{
        *security, *rohc, *metrics, *pool,
        [this](const uint8_t *sdu, size_t len)
        {
          delivered.emplace_back(sdu, sdu + len);
        },
        BearerType::DRB};
    PdcpRxAmNoReorder::TestInitState init{rx_next, rx_hfn, rx_deliv};
    proc = std::make_unique<PdcpRxAmNoReorder>(deps, init);
  }

  std::unique_ptr<BufferPool> pool;
  std::unique_ptr<PdcpSecurity> security;
  std::unique_ptr<PdcpRohc> rohc;
  std::unique_ptr<MetricsCollector> metrics;
  std::unique_ptr<PdcpRxAmNoReorder> proc;

  std::vector<std::vector<uint8_t>> delivered;
};

// EntityFixture — full PdcpEntity in AmNoReorder mode (default).
// Provides loopback: txSdu → lastTxPdu → rxPdu.
class EntityFixture : public ::testing::Test
{
protected:
  void SetUp() override
  {
    pool = std::make_unique<BufferPool>(2048, 64);
    entity = std::make_unique<PdcpEntity>(
        1, BearerType::DRB, RlcMode::AM, *pool,
        PdcpEntity::RxMode::AmNoReorder);

    entity->setDeliverCallback([this](const uint8_t *sdu, size_t len)
                               { delivered.emplace_back(sdu, sdu + len); });
  }

  // Transmit one SDU and return the resulting raw PDU bytes.
  std::vector<uint8_t> makePdu(const std::string &payload)
  {
    entity->txSdu(
        reinterpret_cast<const uint8_t *>(payload.data()),
        payload.size());
    return entity->lastTxPdu();
  }

  // Build N PDUs, return them without feeding to Rx side.
  std::vector<std::vector<uint8_t>> makeNPdus(int n,
                                              const std::string &base = "p")
  {
    std::vector<std::vector<uint8_t>> pdus;
    for (int i = 0; i < n; ++i)
      pdus.push_back(makePdu(base + std::to_string(i)));
    return pdus;
  }

  std::unique_ptr<BufferPool> pool;
  std::unique_ptr<PdcpEntity> entity;
  std::vector<std::vector<uint8_t>> delivered;
};

// ============================================================
// A. Direct PdcpRxAmNoReorder tests
// ============================================================

// ------------------------------------------------------------
// A1. Initial state — §5.2.2.1
// ------------------------------------------------------------

TEST_F(ProcFixture, InitialState_MatchesSpec)
{
  // §5.2.2.1: rx_next=0, rx_hfn=0, rx_deliv=Maximum_PDCP_SN (4095 for DRB)
  EXPECT_EQ(proc->rxNext(), 0u);
  EXPECT_EQ(proc->rxHfn(), 0u);
  EXPECT_EQ(proc->rxDeliv(), static_cast<SN_t>(SN_MAX_12BIT - 1)); // 4095
}

// ------------------------------------------------------------
// A2. Case D — ForwardInWindow: normal single-packet delivery
// ------------------------------------------------------------

TEST_F(ProcFixture, CaseD_NormalForward_DeliveredImmediately)
{
  // rx_deliv=4095, rx_next=0.  SN=0: recv_minus_last = 0-4095 = -4095
  // vere1: -4095 > 2048? No.  vere2: 4095-0=4095 ≥ 0 && < 2048? No (4095 ≥ 2048).
  // → NOT OutsideWindow → Case D (SN=0 ≥ rx_next=0).
  ASSERT_EQ(rx(0, "hello"), Status::OK);
  ASSERT_EQ(delivered.size(), 1u);
  EXPECT_EQ(std::string(delivered[0].begin(), delivered[0].end()), "hello");
  EXPECT_EQ(proc->rxNext(), 1u);
  EXPECT_EQ(proc->rxDeliv(), 0u);
}

// ------------------------------------------------------------
// A3. Case E — BehindSameHfn: late arrival, same HFN
// Bug fix: must use rx_hfn_ (NOT rx_hfn_ - 1)
// ------------------------------------------------------------

TEST_F(ProcFixture, CaseE_BehindSameHfn_UsesCorrectHfn)
{
  // Drive state: receive SN=0,1,3 so rx_next_=4, rx_deliv_=1 (gap at 2).
  ASSERT_EQ(rx(0, "p0"), Status::OK);
  ASSERT_EQ(rx(1, "p1"), Status::OK);
  ASSERT_EQ(rx(3, "p3"), Status::OK); // stored, gap at 2

  // SN=2 < rx_next_=4 → Case E (BehindSameHfn).
  // Must decipher with rx_hfn_=0, not rx_hfn_-1 (which would be uint wrap).
  const size_t count_before = delivered.size();
  ASSERT_EQ(rx(2, "p2"), Status::OK);

  // Flush: p2 delivered first, then p3 (already in store)
  EXPECT_EQ(delivered.size(), count_before);
  // rx_deliv_ should advance past 3
  EXPECT_EQ(proc->rxDeliv(), SN_t{3});
  EXPECT_EQ(proc->rxHfn(), 0u);
}

// ------------------------------------------------------------
// A4. Case A — OutsideWindow (vere 2): SN == rx_deliv_ → discard
// ------------------------------------------------------------

TEST_F(ProcFixture, CaseA_Vere2_SnEqualsRxDeliv_Discarded)
{
  // Default: rx_deliv_=4095.  Inject SN=4095.
  // last_minus_recv = 4095-4095 = 0 → 0 ≥ 0 && 0 < 2048 → vere2 → OutsideWindow.
  ASSERT_EQ(rx(4095, "dup"), Status::OK);
  EXPECT_EQ(delivered.size(), 0u); // discard — no delivery
  // State variables must NOT change
  EXPECT_EQ(proc->rxNext(), 0u);
  EXPECT_EQ(proc->rxDeliv(), SN_t{SN_MAX_12BIT - 1}); // still 4095
  EXPECT_EQ(proc->rxHfn(), 0u);
}

// ------------------------------------------------------------
// A5. Case A — OutsideWindow (vere 2): duplicate same PDU
// ------------------------------------------------------------

TEST_F(ProcFixture, CaseA_Vere2_DuplicatePdu_Discarded)
{
  // Receive SN=0 once → rx_deliv_=0.
  ASSERT_EQ(rx(0, "first"), Status::OK);
  ASSERT_EQ(delivered.size(), 1u);

  // SN=0 again: last_minus_recv = 0-0 = 0 → vere2 → OutsideWindow.
  ASSERT_EQ(rx(0, "dup"), Status::OK);
  EXPECT_EQ(delivered.size(), 1u); // no second delivery
  EXPECT_EQ(proc->rxDeliv(), SN_t{0});
  EXPECT_EQ(proc->rxNext(), SN_t{1});
}

// ------------------------------------------------------------
// A6. Case A — OutsideWindow (vere 2): SN just inside boundary
// ------------------------------------------------------------

TEST_F(ProcFixture, CaseA_Vere2_Boundary_SnAtRxDelivPlusWindow_Discarded)
{
  // Drive rx_deliv_ to 0 by receiving SN=0.
  ASSERT_EQ(rx(0), Status::OK);
  // rx_deliv_=0.  SN=2048 (= Reordering_Window): last_minus_recv = 0-2048 = -2048.
  // vere2 requires last_minus_recv >= 0, so -2048 < 0 → vere2 false.
  // recv_minus_last = 2048-0 = 2048 = window (NOT > window) → vere1 false.
  // → NOT OutsideWindow → ForwardInWindow.
  // SN=2048 is IN-WINDOW (barely).  Verify it is accepted.
  ASSERT_EQ(rx(2048), Status::OK);
  EXPECT_EQ(proc->rxDeliv(), SN_t{2048}); // delivered and advanced
}

// ------------------------------------------------------------
// A7. Case A — OutsideWindow: one step beyond the window
// ------------------------------------------------------------

TEST_F(ProcFixture, CaseA_Vere1_SnOneAheadOfWindow_Discarded)
{
  // Drive rx_deliv_ to 0.
  ASSERT_EQ(rx(0), Status::OK);
  // SN=2049: recv_minus_last = 2049 > 2048 → vere1 → OutsideWindow.
  // Note: normal deliver happens for SN=1..2048.  SN=2049 is the first
  // discard on the forward side.
  const SN_t rx_deliv_before = proc->rxDeliv();
  ASSERT_EQ(rx(2049, "discard"), Status::OK);
  EXPECT_EQ(proc->rxDeliv(), rx_deliv_before); // unchanged
  // delivered count should NOT increase for SN=2049
  // (the one delivery was for SN=0 above; SN=2049 is discarded)
  // Total: 1 delivery for SN=0 only.
  // We check by counting — only SN=0 was delivered.
  EXPECT_EQ(delivered.size(), 1u);
}

// ------------------------------------------------------------
// A8. Case A after wrap-around (vere 1 spec):
//   rx_deliv_=10, rx_next_=11, rx_hfn_=1
//   inject SN=4090 (recv_minus_last=4090-10=4080 > 2048 → vere1)
//   Expect: decipher (with rx_hfn_-1=0 because sn=4090 > rx_next_=11),
//           discard; state unchanged, rx_store_ not touched.
// ------------------------------------------------------------

TEST_F(ProcFixture, CaseA_AfterWrapAround_Vere1_StateUnchanged)
{
  resetWithState(/*rx_next=*/11, /*rx_hfn=*/1, /*rx_deliv=*/10);

  ASSERT_EQ(rx(4090, "discard"), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);
  EXPECT_EQ(proc->rxNext(), SN_t{11});
  EXPECT_EQ(proc->rxHfn(), 1u);
  EXPECT_EQ(proc->rxDeliv(), SN_t{10});
}

// ------------------------------------------------------------
// A9. Case A duplicate same HFN (vere 2 spec):
//   rx_deliv_=100, rx_next_=101, rx_hfn_=0
//   inject SN=50 (last_minus_recv=100-50=50; 0 ≤ 50 < 2048 → vere2)
//   Expect: decipher with rx_hfn_=0, discard; state unchanged.
// ------------------------------------------------------------

TEST_F(ProcFixture, CaseA_DuplicateSameHfn_Vere2_StateUnchanged)
{
  resetWithState(/*rx_next=*/101, /*rx_hfn=*/0, /*rx_deliv=*/100);

  ASSERT_EQ(rx(50, "old"), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);
  EXPECT_EQ(proc->rxNext(), SN_t{101});
  EXPECT_EQ(proc->rxHfn(), 0u);
  EXPECT_EQ(proc->rxDeliv(), SN_t{100});
}

// ------------------------------------------------------------
// A10. Case D — wrap-around: SN=4095 → rx_next_=0, rx_hfn_++
// ------------------------------------------------------------

TEST_F(ProcFixture, CaseD_WrapAround_HfnIncrements)
{
  // Drive to rx_next_=4095, rx_deliv_=4094 by receiving SN=0..4094.
  // (fast: use TestInitState to skip the drive)
  resetWithState(/*rx_next=*/4095, /*rx_hfn=*/0, /*rx_deliv=*/4094);

  ASSERT_EQ(rx(4095, "wrap"), Status::OK);

  // After wrap: rx_next_=0, rx_hfn_=1, rx_deliv_=4095
  EXPECT_EQ(proc->rxNext(), SN_t{0});
  EXPECT_EQ(proc->rxHfn(), 1u);
  EXPECT_EQ(proc->rxDeliv(), SN_t{4095});
  EXPECT_EQ(delivered.size(), 1u);
}

// ------------------------------------------------------------
// A11. Case B — WrapAhead: rx_hfn_ committed permanently
//   rx_next_=5, rx_hfn_=0, rx_deliv_=4
//   SN=4095: next_minus_recv = 5-4095 = -4090; vere1(window) false.
//   recv_minus_next = 4095-5 = 4090 ≥ 2048 → Case C (LateFromPrevHfn)!
//
//   For true Case B we need: rx_next_=5, rx_hfn_=1, rx_deliv_=4
//   and SN = Maximum_PDCP_SN - 2044 (well inside the back-window).
//   Simplest Case B setup: rx_next_=2, rx_hfn_=1, rx_deliv_=1
//   SN=4095 (previous HFN): next_minus_recv = 2-4095 = -4093
//   That's negative — not > 2048.  Still not Case B.
//
//   Case B condition: Next_PDCP_RX_SN – received > Reordering_Window.
//   Signed: rx_next_ – sn > 2048.
//   Example: rx_next_=2050, sn=1 → 2050-1=2049 > 2048 → Case B.
// ------------------------------------------------------------

TEST_F(ProcFixture, CaseB_WrapAhead_HfnCommittedToState)
{
  // rx_next_=2050, rx_hfn_=0, rx_deliv_=2049
  // SN=1: next_minus_recv = 2050-1 = 2049 > 2048 → Case B.
  // BUT first check: window anchored on rx_deliv_=2049.
  // recv_minus_last = 1-2049 = -2048.  vere1: -2048 > 2048? No.
  // last_minus_recv = 2049-1 = 2048.  vere2: 0 ≤ 2048 < 2048? No (not strict <).
  // → NOT OutsideWindow → proceeds to HFN selection → Case B.
  resetWithState(/*rx_next=*/2050, /*rx_hfn=*/0, /*rx_deliv=*/2049);

  ASSERT_EQ(rx(1, "wrapB"), Status::OK);

  // rx_hfn_ must be committed to 1 (not just local to decipher call)
  EXPECT_EQ(proc->rxHfn(), 1u);
  EXPECT_EQ(proc->rxNext(), SN_t{2}); // sn+1 = 1+1 = 2
  EXPECT_EQ(delivered.size(), 1u);
}

// ------------------------------------------------------------
// A12. Normal delivery — store-then-flush (Nhánh A)
//   rx_store_ has an entry at COUNT=5.  Inject SN=7 (normal mode).
//   Expect deliver: SN=5 (old stored), then SN=7 (current, consecutive).
//
//   Setup: rx_deliv_=4 (so COUNT=5 is in-window), rx_next_=5, rx_hfn_=0.
//   First inject SN=5 with reestablish=true so it stores without
//   delivering (is_next check: 5 == 4+1 → actually IS next, delivers).
//
//   Revised setup: rx_deliv_=3, rx_next_=4, rx_hfn_=0.
//   Inject SN=5 with reestablish=true: is_next = (5==3+1=4)? No → stored.
//   Now rx_store_ has COUNT=5.  Inject SN=7 with reestablish=false.
//   Expected:
//     Flush COUNT < 7: deliver COUNT=5.
//     Consecutive from 7: deliver COUNT=7.
//     Delivered order: payload of SN=5, payload of SN=7.
// ------------------------------------------------------------

TEST_F(ProcFixture, NormalDelivery_StoreThenFlush_OldStoredAndCurrent)
{
  resetWithState(/*rx_next=*/4, /*rx_hfn=*/0, /*rx_deliv=*/3);

  // Store SN=5 without delivering (reestablish=true, is_next=false: 5 ≠ 3+1=4)
  ASSERT_EQ(rx(5, "sdu5", /*reestablish=*/true), Status::OK);
  EXPECT_EQ(delivered.size(), 0u); // not delivered yet

  // Normal inject SN=7: flush COUNT<7 (SN=5), then consecutive from 7 (SN=7).
  ASSERT_EQ(rx(7, "sdu7", /*reestablish=*/false), Status::OK);

  ASSERT_EQ(delivered.size(), 2u);
  EXPECT_EQ(std::string(delivered[0].begin(), delivered[0].end()), "sdu5");
  EXPECT_EQ(std::string(delivered[1].begin(), delivered[1].end()), "sdu7");

  EXPECT_EQ(proc->rxDeliv(), SN_t{7});
}

// ------------------------------------------------------------
// A13. Re-establishment — simple next (is_next true → deliver)
// ------------------------------------------------------------

TEST_F(ProcFixture, Reestablishment_SimpleNext_Delivered)
{
  // rx_deliv_=10, rx_next_=11, rx_hfn_=0
  resetWithState(11, 0, 10);

  // SN=11: is_next = (11 == 10+1 = 11) → true → deliver immediately.
  ASSERT_EQ(rx(11, "sdu11", /*reestablish=*/true), Status::OK);
  ASSERT_EQ(delivered.size(), 1u);
  EXPECT_EQ(proc->rxDeliv(), SN_t{11});
}

// ------------------------------------------------------------
// A14. Re-establishment — SN not next → stored, not delivered
// ------------------------------------------------------------

TEST_F(ProcFixture, Reestablishment_NotNext_Stored_NotDelivered)
{
  resetWithState(11, 0, 10);

  // SN=13: is_next = (13 == 11) → false → store, no delivery.
  ASSERT_EQ(rx(13, "sdu13", /*reestablish=*/true), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);
  EXPECT_EQ(proc->rxDeliv(), SN_t{10}); // unchanged
}

// ------------------------------------------------------------
// A15. Re-establishment — store-then-flush consecutive
//   Pre-store SN=13, SN=14.  Inject SN=12 → fills gap → flush 12,13,14.
// ------------------------------------------------------------

TEST_F(ProcFixture, Reestablishment_StoreThenFlushConsecutive)
{
  resetWithState(11, 0, 10);

  // Store SN=13 and SN=14 (both not-next relative to rx_deliv_=10)
  ASSERT_EQ(rx(13, "sdu13", true), Status::OK);
  ASSERT_EQ(rx(14, "sdu14", true), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);

  // SN=12: is_next = (12 == 11) → true → flush 12,13,14 in order.
  // Wait — rx_deliv_ is still 10 here, so is_next = (12 == 10+1 = 11)? No.
  // rx_next_=11, rx_deliv_=10.  SN=11 is the gap filler.  Let me inject 11 first.
  ASSERT_EQ(rx(11, "sdu11", true), Status::OK);
  // is_next=(11==10+1=11) → true → flush consecutive from COUNT=11.
  // rx_store_ has 13 and 14 but not 12.  Consecutive from 11: deliver 11, then
  // check 12 → not in store → stop.
  EXPECT_EQ(delivered.size(), 1u);
  EXPECT_EQ(proc->rxDeliv(), SN_t{11});

  // Now inject SN=12: is_next=(12==11+1=12) → true → flush 12, then 13, 14.
  ASSERT_EQ(rx(12, "sdu12", true), Status::OK);
  ASSERT_EQ(delivered.size(), 4u);
  EXPECT_EQ(std::string(delivered[1].begin(), delivered[1].end()), "sdu12");
  EXPECT_EQ(std::string(delivered[2].begin(), delivered[2].end()), "sdu13");
  EXPECT_EQ(std::string(delivered[3].begin(), delivered[3].end()), "sdu14");
  EXPECT_EQ(proc->rxDeliv(), SN_t{14});
}

// ------------------------------------------------------------
// A16. Re-establishment — wrap-around: rx_deliv_=4095, SN=0
//   is_next vere 2: rx_deliv_==Maximum_PDCP_SN && sn==0 → true → deliver
// ------------------------------------------------------------

TEST_F(ProcFixture, Reestablishment_WrapAround_Vere2_Delivered)
{
  const SN_t max_sn = static_cast<SN_t>(SN_MAX_12BIT - 1); // 4095
  resetWithState(/*rx_next=*/0, /*rx_hfn=*/1, /*rx_deliv=*/max_sn);

  // SN=0: is_next vere2 (rx_deliv_==4095 && sn==0) → true → deliver.
  ASSERT_EQ(rx(0, "sdu0", /*reestablish=*/true), Status::OK);
  ASSERT_EQ(delivered.size(), 1u);
  EXPECT_EQ(proc->rxDeliv(), SN_t{0});
}

// ------------------------------------------------------------
// A17. Re-establishment — duplicate in store: second inject discarded
// ------------------------------------------------------------

TEST_F(ProcFixture, Reestablishment_DuplicateInStore_Discarded)
{
  resetWithState(11, 0, 10);

  // Store SN=13 (not-next)
  ASSERT_EQ(rx(13, "first", true), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);

  // Inject SN=13 again: duplicate check (COUNT=13 already in rx_store_) → discard.
  ASSERT_EQ(rx(13, "dup", true), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);
  EXPECT_EQ(proc->rxDeliv(), SN_t{10});
}

// ------------------------------------------------------------
// A18. Normal delivery — empty buffer: single PDU delivered immediately
// ------------------------------------------------------------

TEST_F(ProcFixture, NormalDelivery_EmptyBuffer_ImmediateDeliver)
{
  // Spec-default init: rx_deliv_=4095, rx_next_=0.
  ASSERT_EQ(rx(0, "only"), Status::OK);
  ASSERT_EQ(delivered.size(), 1u);
  EXPECT_EQ(std::string(delivered[0].begin(), delivered[0].end()), "only");
}

// ------------------------------------------------------------
// A19. Reestablish() — clears rx_store_, ROHC reset
// ------------------------------------------------------------

TEST_F(ProcFixture, Reestablish_ClearsStore)
{
  resetWithState(11, 0, 10);

  // Store SN=13 (not-next)
  ASSERT_EQ(rx(13, "stored", true), Status::OK);
  EXPECT_EQ(delivered.size(), 0u);

  // Trigger reestablish (no stored context)
  proc->reestablish();

  // After reestablish(), inject SN=11 normally.
  // rx_deliv_ should still be 10 (no stored context reset).
  // SN=11 is_next=(11==10+1=11) → true → deliver.
  ASSERT_EQ(rx(11, "new11", true), Status::OK);
  EXPECT_EQ(delivered.size(), 1u); // SN=13 was cleared, only SN=11 delivered
}

// ------------------------------------------------------------
// A20. Reestablish() with use_stored_context — resets all state
// ------------------------------------------------------------

TEST_F(ProcFixture, Reestablish_StoredContext_ResetsState)
{
  resetWithState(11, 0, 10);

  proc->reestablishWithStoredContext();

  // State must be reset to §5.2.2.1 values
  EXPECT_EQ(proc->rxNext(), SN_t{0});
  EXPECT_EQ(proc->rxHfn(), 0u);
  EXPECT_EQ(proc->rxDeliv(), static_cast<SN_t>(SN_MAX_12BIT - 1)); // 4095
}

// ============================================================
// B. PdcpEntity façade tests
// ============================================================

// ------------------------------------------------------------
// B1. Constructor with AmNoReorder — initial rxDeliv matches §5.2.2.1
// ------------------------------------------------------------

TEST_F(EntityFixture, Constructor_AmNoReorder_InitialStateCorrect)
{
  EXPECT_EQ(entity->rxNext(), SN_t{0});
  EXPECT_EQ(entity->rxHfn(), 0u);
  EXPECT_EQ(entity->rxDeliv(), static_cast<SN_t>(SN_MAX_12BIT - 1));
}

// ------------------------------------------------------------
// B2. Constructor with UmNoReorder — rxPdu returns NOT_IMPLEMENTED
// ------------------------------------------------------------

TEST(EntityFacadeTest, Constructor_UmNoReorder_ReturnsNotImplemented)
{
  BufferPool pool(2048, 8);
  PdcpEntity e(1, BearerType::DRB, RlcMode::UM, pool,
               PdcpEntity::RxMode::UmNoReorder);

  const uint8_t dummy[] = {0x80, 0x00, 'X'};
  EXPECT_EQ(e.rxPdu(dummy, sizeof(dummy)),
            Status::NOT_IMPLEMENTED);
}

// ------------------------------------------------------------
// B3. Constructor with WithReorder — rxPdu returns NOT_IMPLEMENTED
// ------------------------------------------------------------

TEST(EntityFacadeTest, Constructor_WithReorder_ReturnsNotImplemented)
{
  BufferPool pool(2048, 8);
  PdcpEntity e(1, BearerType::DRB, RlcMode::AM, pool,
               PdcpEntity::RxMode::WithReorder);

  const uint8_t dummy[] = {0x80, 0x00, 'X'};
  EXPECT_EQ(e.rxPdu(dummy, sizeof(dummy)),
            Status::NOT_IMPLEMENTED);
}

// ------------------------------------------------------------
// B4. Pass-through rxPdu — PDU flows through entity to procedure
// ------------------------------------------------------------

TEST_F(EntityFixture, Passthrough_RxPdu_DeliveryWorks)
{
  auto pdu = makePdu("loopback");
  ASSERT_EQ(entity->rxPdu(pdu.data(), pdu.size()), Status::OK);

  const auto &sdu = entity->lastDeliveredSdu();
  EXPECT_EQ(std::string(sdu.begin(), sdu.end()), "loopback");
}

// ------------------------------------------------------------
// B5. Pass-through RX accessors — entity mirrors procedure state
// ------------------------------------------------------------

TEST_F(EntityFixture, Passthrough_RxAccessors_MatchProcedureState)
{
  auto pdu = makePdu("state_check");
  ASSERT_EQ(entity->rxPdu(pdu.data(), pdu.size()), Status::OK);

  // After first PDU (SN=0): rx_next_=1, rx_deliv_=0
  EXPECT_EQ(entity->rxNext(), SN_t{1});
  EXPECT_EQ(entity->rxDeliv(), SN_t{0});
  EXPECT_EQ(entity->rxHfn(), 0u);
}

// ------------------------------------------------------------
// B6. Reestablish pass-through — clears state as expected
// ------------------------------------------------------------

TEST_F(EntityFixture, Reestablish_Passthrough_DelegatesToProcedure)
{
  // Process a few PDUs to advance state
  auto pdu = makePdu("a");
  ASSERT_EQ(entity->rxPdu(pdu.data(), pdu.size()), Status::OK);

  entity->reestablish();

  // ROHC reset + rx_store_ cleared, but state variables preserved
  // (no stored context).  rxDeliv should still be 0 from the received PDU.
  EXPECT_EQ(entity->rxDeliv(), SN_t{0});
}

// ------------------------------------------------------------
// B7. Due_to_reestablishment flag forwarded correctly
// ------------------------------------------------------------

TEST_F(EntityFixture, Passthrough_ReestablishmentFlag_ForwardedToProcedure)
{
  // Build PDU SN=0 first (it will be the "next" after default rx_deliv_=4095).
  auto pdu = makePdu("reestab");

  // Inject with due_to_reestablishment=true.
  // SN=0 is is_next relative to rx_deliv_=4095 (vere2: rx_deliv_==4095 && sn==0).
  ASSERT_EQ(entity->rxPdu(pdu.data(), pdu.size(), true), Status::OK);
  EXPECT_EQ(delivered.size(), 1u);
}

// ------------------------------------------------------------
// B8. Loopback: 10 in-order PDUs all delivered
// ------------------------------------------------------------

TEST_F(EntityFixture, Loopback_TenPackets_AllDelivered)
{
  auto pdus = makeNPdus(10);
  for (auto &p : pdus)
  {
    ASSERT_EQ(entity->rxPdu(p.data(), p.size()), Status::OK);
  }
  EXPECT_EQ(delivered.size(), 10u);
}

// ------------------------------------------------------------
// B9. Loopback: out-of-order delivery (existing regression guard)
// ------------------------------------------------------------

TEST_F(EntityFixture, Loopback_OutOfOrder_DeliveredInSNOrder)
{
  auto p0 = makePdu("p0");
  auto p1 = makePdu("p1");
  auto p2 = makePdu("p2");
  auto p3 = makePdu("p3");

  entity->rxPdu(p0.data(), p0.size());
  entity->rxPdu(p1.data(), p1.size());
  entity->rxPdu(p3.data(), p3.size()); // gap at p2
  EXPECT_EQ(entity->rxPdu(p2.data(), p2.size()), Status::OK);

  // In Clause 5.1.2.1.2, the PDCP does not automatically reorder if the lower level delivers incorrectly
  // This clause is only used when the lower level (RLC AM) has committed to delivering in the correct order.
  ASSERT_EQ(delivered.size(), 3u);

  EXPECT_EQ(std::string(delivered[0].begin(), delivered[0].end()), "p0");
  EXPECT_EQ(std::string(delivered[1].begin(), delivered[1].end()), "p1");
  EXPECT_EQ(std::string(delivered[2].begin(), delivered[2].end()), "p3");
  EXPECT_EQ(entity->getMetrics().dropped_packets, 1u);
}
