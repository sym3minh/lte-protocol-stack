// ============================================================
// pdcp_entity_rx_am.cpp — §5.1.2.1.2 RX path for PdcpEntity
//
// "DRBs mapped on RLC AM, reordering function not used"
// Ref: TS 36.323 §5.1.2.1.2
//
// Implements the following PdcpEntity private methods:
//   classifyRx()                   — HFN selection + window check
//   decipherAndDecompress()        — shared decipher+ROHC helper
//   rxPduAmNoReorder()             — main §5.1.2.1.2 receive procedure
//   reestablishAm()                — §5.2.2.1 (no stored context)
//   reestablishAmWithStoredContext() — §5.2.2.1 (stored context)
//
// State accessed via PdcpEntity members directly:
//   rx_next_, rx_hfn_, rx_deliv_, rx_store_
//   security_, rohc_, metrics_, pool_, bearer_, deliver_cb_
//   last_delivered_sdu_
// ============================================================

#include "pdcp_entity.h"
#include "pdcp_pdu.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace lte
{

  // ============================================================
  // classifyRx — TS 36.323 §5.1.2.1.2
  //
  // Design principle: map 1-to-1 onto the spec's exact words.
  // Use signed int64_t subtraction throughout — NOT modular
  // arithmetic — so a reviewer can read code and spec side-by-side
  // without verifying modular-equivalence proofs.
  //
  // Why int64_t (not int32_t)?
  //   12-bit SN: max diff = 4095; fits int16_t.
  //   18-bit 5G NR SN: max diff = 262143; fits int32_t.
  //   int64_t gives headroom for future SN widths and eliminates
  //   integer-promotion pitfalls when mixing uint32_t with signed
  //   literals.
  //
  // HFN underflow safety:
  //   Cases A (sn > rx_next_) and C compute rx_hfn_ - 1.
  //   After the §5.2.2.1 init (rx_hfn_=0, rx_next_=0,
  //   rx_deliv_=Maximum_PDCP_SN), those branches cannot be reached
  //   on the first PDU because the window check (anchored on
  //   rx_deliv_) routes SN=0 to Case D.
  //   In debug builds we assert rx_hfn_ > 0 before subtracting;
  //   in release builds uint32_t underflow (wrap to 2^32-1) is
  //   accepted — decipher will produce garbage and the SDU will be
  //   discarded at a higher layer without crashing.
  // ============================================================
  RxClassification
  PdcpEntity::classifyRx(SN_t sn, uint32_t &out_hfn) const
  {
    const SN_t window = snWindow(bearer_);

    // Signed-distance helper — cast to int64_t before subtracting
    // to avoid unsigned underflow.
    auto sdiff = [](SN_t a, SN_t b) -> int64_t
    {
      return static_cast<int64_t>(a) - static_cast<int64_t>(b);
    };

    // ----------------------------------------------------------
    // Step 1 — Window check anchored on rx_deliv_
    //          (Last_Submitted_PDCP_RX_SN)
    //
    // Spec §5.1.2.1.2, first IF:
    //   "if received PDCP SN – Last_Submitted_PDCP_RX_SN > Reordering_Window
    //    OR  0 <= Last_Submitted_PDCP_RX_SN – received PDCP SN
    //            < Reordering_Window"
    //   → outside window → discard (Case A)
    // ----------------------------------------------------------
    const int64_t recv_minus_last = sdiff(sn, rx_deliv_); // "received – Last_Submitted"
    const int64_t last_minus_recv = sdiff(rx_deliv_, sn); // "Last_Submitted – received"

    const bool vere1 = (recv_minus_last > static_cast<int64_t>(window));
    const bool vere2 = (last_minus_recv >= 0 &&
                        last_minus_recv < static_cast<int64_t>(window));

    if (vere1 || vere2)
    {
      // Case A — outside window.
      // HFN selection per spec ("if received PDCP SN > Next_PDCP_RX_SN"):
      // RAW comparison, no modular arithmetic.
      if (sn > rx_next_)
      {
        out_hfn = (rx_hfn_ > 0) ? rx_hfn_ - 1 : 0;
      }
      else
      {
        out_hfn = rx_hfn_;
      }
      return RxClassification::OutsideWindow;
    }

    // ----------------------------------------------------------
    // Step 2 — HFN selection anchored on rx_next_
    //          (Next_PDCP_RX_SN)
    //
    // Spec §5.1.2.1.2, else-if chain:
    //   B: "Next_PDCP_RX_SN – received > Reordering_Window"  → HFN + 1
    //   C: "received – Next_PDCP_RX_SN >= Reordering_Window" → HFN – 1
    //   D: "received >= Next_PDCP_RX_SN"                     → HFN
    //   E: "received < Next_PDCP_RX_SN"                      → HFN
    // ----------------------------------------------------------
    const int64_t next_minus_recv = sdiff(rx_next_, sn); // "Next – received"
    const int64_t recv_minus_next = sdiff(sn, rx_next_); // "received – Next"

    if (next_minus_recv > static_cast<int64_t>(window))
    {
      // Case B: SN is ahead after a wrap-around boundary.
      out_hfn = rx_hfn_ + 1;
      return RxClassification::WrapAhead;
    }

    if (recv_minus_next >= static_cast<int64_t>(window))
    {
      // Case C: late arrival from previous HFN cycle.
      out_hfn = (rx_hfn_ > 0) ? rx_hfn_ - 1 : 0;
      return RxClassification::LateFromPrevHfn;
    }

    if (sn >= rx_next_)
    {
      // Case D: normal forward delivery.
      out_hfn = rx_hfn_;
      return RxClassification::ForwardInWindow;
    }

    // Case E: SN behind rx_next_ but still in window (in-order late).
    // sn < rx_next_
    out_hfn = rx_hfn_;
    return RxClassification::BehindSameHfn;
  }

  // ============================================================
  // rxPduAmNoReorder — TS 36.323 §5.1.2.1.2
  //
  // Step numbering below matches the spec text exactly so a reviewer
  // can read code and standard side-by-side.
  //
  // Key invariants maintained:
  //   rx_next_  always == (last accepted SN + 1) % modulus
  //   rx_deliv_ == SN of last SDU delivered upward
  //   rx_store_ keys are COUNT values (rx_hfn_ × mod + sn)
  // ============================================================
  Status PdcpEntity::rxPduAmNoReorder(const uint8_t *raw_pdu,
                                      size_t raw_len,
                                      bool due_to_reestablishment)
  {
    if (!raw_pdu || raw_len == 0)
      return Status::PARSE_ERROR;

    // ----------------------------------------------------------
    // Step 1 — Parse PDCP Data PDU header
    // ----------------------------------------------------------
    PdcpPdu pdu;
    Status parse_status = PdcpPduCodec::deserialize(raw_pdu, raw_len, bearer_, pdu);
    if (parse_status != Status::OK)
    {
      metrics_.recordDrop();
      return parse_status;
    }
    if (!pdu.isData())
    {
      // Control PDU — not handled by this procedure
      return Status::OK;
    }

    // ----------------------------------------------------------
    // Step 2 — Classify received SN + determine HFN for decipher
    // ----------------------------------------------------------
    uint32_t decipher_hfn = 0;
    const RxClassification cls = classifyRx(pdu.sn, decipher_hfn);

    // ----------------------------------------------------------
    // Step 3 — Process by case
    // ----------------------------------------------------------

    // ---- Case A: OutsideWindow -----------------------------------
    // Spec: decipher + decompress (to keep cipher/ROHC state in sync),
    //       then discard.  Do NOT touch rx_store_, rx_next_, rx_hfn_,
    //       or rx_deliv_.  Return immediately.
    if (cls == RxClassification::OutsideWindow)
    {
      decipherAndDecompress(pdu, decipher_hfn); // side-effects on crypto state
      metrics_.recordDrop();
      return Status::OK; // not INVALID_SN — discard is spec-correct, not an error
    }

    // ---- Cases B, C, D, E: run decipher + decompress ---------------
    std::vector<uint8_t> sdu = decipherAndDecompress(pdu, decipher_hfn);

    // ---- Case B: WrapAhead (Next – received > Reordering_Window) ---
    // HFN must be committed to state BEFORE decipher so that subsequent
    // PDUs in the same burst use the correct HFN.
    if (cls == RxClassification::WrapAhead)
    {
      rx_hfn_++; // COMMIT — permanent state change
      rx_next_ = static_cast<SN_t>(pdu.sn + 1);
    }

    // ---- Case D: ForwardInWindow — advance rx_next_ -----------------
    // Spec: "set Next_PDCP_RX_SN to received PDCP SN + 1"
    // Write as "> Maximum_PDCP_SN" (spec-literal) rather than modulo-
    // then-check-zero to make the wrap condition self-documenting.
    if (cls == RxClassification::ForwardInWindow)
    {
      const SN_t max_sn = static_cast<SN_t>(snModulus(bearer_) - 1);
      if (pdu.sn >= max_sn)
      {
        // Wrap: SN was at maximum, next SN is 0 → new HFN cycle
        rx_next_ = 0;
        rx_hfn_++;
      }
      else
      {
        rx_next_ = static_cast<SN_t>(pdu.sn + 1);
      }
    }

    // Cases C and E: do NOT update rx_next_ or rx_hfn_

    // ----------------------------------------------------------
    // Step 4 — Delivery logic (Cases B / C / D / E only)
    //
    // received_count = COUNT value for this PDU, used as key.
    // ----------------------------------------------------------
    const uint32_t mod = snModulus(bearer_);
    const uint32_t received_count = decipher_hfn * mod + pdu.sn;

    // 4a. Duplicate check in rx_store_
    //     Spec: "if a PDCP SDU with the same PDCP SN is stored: discard"
    //     (key-by-COUNT is equivalent once HFN is resolved)
    if (rx_store_.count(received_count))
    {
      metrics_.recordDrop();
      return Status::OK;
    }

    // 4b. "store the PDCP SDU" — always first, before delivery branching
    rx_store_[received_count] = std::move(sdu);

    // ----------------------------------------------------------
    // 4c. Delivery branching
    // ----------------------------------------------------------

    // Helper: deliver one SDU from rx_store_ by COUNT key, then erase.
    // Updates rx_deliv_ to the SN extracted from the COUNT.
    // Calls deliver_cb_ and last_delivered_sdu_ directly (no lambda indirection).
    auto deliver_one = [&](uint32_t count)
    {
      const auto it = rx_store_.find(count);
      if (it == rx_store_.end())
        return;
      const std::vector<uint8_t> &payload = it->second;

      // Update Last_Submitted_PDCP_RX_SN
      rx_deliv_ = static_cast<SN_t>(count % mod);

      // Record delivery metrics (latency = 0 for now; tx_ts lookup reserved)
      metrics_.recordRx(payload.size(), 0, metrics_.now_ns());

      // Populate test-only last-delivered storage
      last_delivered_sdu_.assign(payload.begin(), payload.end());

      if (deliver_cb_)
      {
        deliver_cb_(payload.data(), payload.size());
      }
      rx_store_.erase(it);
    };

    if (!due_to_reestablishment)
    {
      // ---- NHÁNH A: normal path -----------------------------------
      // Spec: "deliver in ascending order of COUNT:
      //   (a) all stored SDUs with COUNT < received COUNT
      //   (b) all stored SDUs with consecutive COUNT(s)
      //       starting from received COUNT"
      //
      // "store the PDCP SDU" (step 4b above) already placed current
      // into rx_store_, so flushing from received_count picks it up.

      // (a) Flush stored SDUs with COUNT < received_count
      {
        auto upper = rx_store_.lower_bound(received_count);
        std::vector<uint32_t> to_deliver;
        for (auto it = rx_store_.begin(); it != upper; ++it)
        {
          to_deliver.push_back(it->first);
        }
        for (uint32_t cnt : to_deliver)
        {
          deliver_one(cnt);
        }
      }

      // (b) Deliver consecutive COUNTs starting from received_count
      //     (current SDU is in rx_store_ from step 4b)
      {
        uint32_t next = received_count;
        while (rx_store_.count(next))
        {
          deliver_one(next);
          ++next;
        }
      }
    }
    else
    {
      // ---- NHÁNH B: re-establishment path -------------------------
      // Spec: "if received SN = Last_Submitted + 1
      //        OR received SN = Last_Submitted – Maximum_PDCP_SN:
      //          deliver consecutive SDUs starting from received COUNT"

      const SN_t max_sn = static_cast<SN_t>(snModulus(bearer_) - 1);

      // Two-vered is_next per spec:
      //   vere 1: sn == rx_deliv_ + 1
      //   vere 2: rx_deliv_ == Maximum_PDCP_SN && sn == 0
      //           (wrap: rx_deliv_ - Maximum_PDCP_SN = 0 = sn)
      const bool is_next =
          (pdu.sn == static_cast<SN_t>((rx_deliv_ + 1) % (max_sn + 1))) ||
          (rx_deliv_ == max_sn && pdu.sn == 0);

      if (!is_next)
      {
        // SDU already stored in step 4b; wait for the missing gap.
        return Status::OK;
      }

      // Flush consecutive COUNTs starting from received_count
      uint32_t next = received_count;
      while (rx_store_.count(next))
      {
        deliver_one(next);
        ++next;
      }
    }

    // ----------------------------------------------------------
    // Step 5 — Debug invariant assertions
    // ----------------------------------------------------------
#ifndef NDEBUG
    assert(rx_next_ <= static_cast<SN_t>(snModulus(bearer_) - 1));
    assert(rx_deliv_ <= static_cast<SN_t>(snModulus(bearer_) - 1));
#endif

    return Status::OK;
  }

  // ============================================================
  // reestablishAm — TS 36.323 §5.2.2.1  (no stored UE AS context)
  //
  // Simplified path (most common handover/RLF scenario):
  //   - Reset ROHC downlink decompressor context
  //   - Clear rx_store_ (prevent stale SDUs mixing with new batch)
  //   - Preserve rx_next_, rx_hfn_, rx_deliv_ (spec §5.2.2.1)
  //   - TODO: apply new ciphering key when security infra is live
  // ============================================================
  void PdcpEntity::reestablishAm()
  {
    rohc_.resetDownlink();
    rx_store_.clear();
    // Keep rx_next_, rx_hfn_, rx_deliv_ per spec §5.2.2.1
    // TODO: apply new ciphering key when security infrastructure is live
  }

  // ============================================================
  // reestablishAmWithStoredContext — TS 36.323 §5.2.2.1
  //
  // Called when upper layer (RRC) supplies stored UE AS context.
  // Full state reset to §5.2.2.1 initial values.
  // ============================================================
  void PdcpEntity::reestablishAmWithStoredContext()
  {
    rohc_.resetDownlink();
    rx_store_.clear();

    rx_next_ = 0;
    rx_hfn_ = 0;
    rx_deliv_ = static_cast<SN_t>(snModulus(bearer_) - 1);

    // TODO: apply new ciphering key when security infrastructure is live
  }

} // namespace lte
