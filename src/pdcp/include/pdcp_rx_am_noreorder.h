#pragma once
// ============================================================
// pdcp_rx_am_noreorder.h — §5.1.2.1.2 RX procedure
//
// "DRBs mapped on RLC AM, reordering function not used"
// Ref: TS 36.323 §5.1.2.1.2
//
// This is the only fully-implemented RX procedure in the current
// revision.  PdcpRxUmNoReorder (§5.1.2.1.3) and PdcpRxWithReorder
// (§5.1.2.1.4) are compiled stubs.
//
// Lifetime note:
//   This object holds non-owning references to PdcpEntity's
//   sub-components via Deps.  PdcpEntity owns this object through
//   unique_ptr<IPdcpRxProcedure>, so the entity always outlives
//   the procedure and there is no dangling-reference risk.
// ============================================================

#include "pdcp_rx_procedure.h"
#include "pdcp_pdu.h"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace lte {

class PdcpRxAmNoReorder final : public IPdcpRxProcedure {
public:
    // ----------------------------------------------------------
    // Constructor — inject all external dependencies via Deps.
    //
    // RX state is initialised per TS 36.323 §5.2.2.1:
    //   Next_PDCP_RX_SN          = 0
    //   RX_HFN                   = 0
    //   Last_Submitted_PDCP_RX_SN = Maximum_PDCP_SN  (= snModulus - 1)
    // ----------------------------------------------------------
    explicit PdcpRxAmNoReorder(IPdcpRxProcedure::Deps deps);

    // ----------------------------------------------------------
    // TestInitState — used only by the test constructor below.
    // Allows unit tests to seed arbitrary RX state without driving
    // thousands of loopback PDUs to reach wrap-around conditions.
    // NOT intended for production use.
    // ----------------------------------------------------------
    struct TestInitState {
        SN_t     rx_next;   // Next_PDCP_RX_SN
        uint32_t rx_hfn;    // RX_HFN
        SN_t     rx_deliv;  // Last_Submitted_PDCP_RX_SN
    };

    // Test-only constructor: same as the normal constructor but uses
    // caller-provided initial state instead of §5.2.2.1 defaults.
    PdcpRxAmNoReorder(IPdcpRxProcedure::Deps deps, TestInitState init);

    // ----------------------------------------------------------
    // rxPdu — §5.1.2.1.2 main receive procedure
    //
    // Implements the exact 5-case branching from the spec:
    //   Case A (OutsideWindow)   : decipher + decompress, discard, return
    //   Case B (WrapAhead)       : rx_hfn_++, decipher, update rx_next_
    //   Case C (LateFromPrevHfn) : decipher with rx_hfn_-1, no state update
    //   Case D (ForwardInWindow) : decipher, advance rx_next_ (with wrap)
    //   Case E (BehindSameHfn)   : decipher with rx_hfn_ (fixes old bug)
    //
    // After Cases B/C/D/E: duplicate check → store → delivery branching:
    //   Normal (due_to_reestablishment=false):
    //     flush all stored with COUNT < received, then consecutive run
    //   Re-establishment (due_to_reestablishment=true):
    //     only deliver if SN == Last_Submitted + 1 (or wrap), then
    //     consecutive run; otherwise leave in buffer
    // ----------------------------------------------------------
    Status rxPdu(const uint8_t* raw_pdu,
                 size_t         raw_len,
                 bool           due_to_reestablishment = false) override;

    // ----------------------------------------------------------
    // reestablish — per TS 36.323 §5.2.2.1
    //
    // Simplified path (most common):
    //   - Reset ROHC downlink decompressor context
    //   - Clear rx_store_ (prevent stale SDUs mixing with new batch)
    //   - Do NOT reset rx_next_ / rx_hfn_ / rx_deliv_ unless
    //     use_stored_context == true (stored UE AS context path)
    //   - Apply new ciphering key (delegate to security_)
    //
    // When use_stored_context == true (RRC supplies stored context):
    //   rx_next_  = 0
    //   rx_hfn_   = 0
    //   rx_deliv_ = Maximum_PDCP_SN
    // ----------------------------------------------------------
    // reestablish — satisfies IPdcpRxProcedure interface.
    // Normal path: keep SN/HFN state, only clear rx_store_ + reset ROHC.
    // Per TS 36.323 §5.2.2.1 (no stored UE AS context provided).
    void reestablish() override;

    // reestablishWithStoredContext — full state reset to §5.2.2.1 defaults.
    // Call when upper layer supplies stored UE AS context
    // (sets rx_next_=0, rx_hfn_=0, rx_deliv_=Maximum_PDCP_SN).
    void reestablishWithStoredContext();

    // State accessors
    SN_t     rxNext()  const override { return rx_next_;  }
    uint32_t rxHfn()   const override { return rx_hfn_;   }
    SN_t     rxDeliv() const override { return rx_deliv_; }

private:
    // ----------------------------------------------------------
    // classifyRx — TS 36.323 §5.1.2.1.2 window + HFN selection
    //
    // Uses signed int64_t subtraction (NOT modular arithmetic) to
    // map 1-to-1 onto the spec's exact wording.  See implementation
    // file for full rationale.
    //
    // Output: RxClassification + out_hfn (HFN to use for decipher)
    // ----------------------------------------------------------
    RxClassification classifyRx(SN_t sn, uint32_t& out_hfn) const;

    // ----------------------------------------------------------
    // decipherAndDecompress — shared helper for accept + discard paths
    //
    // The spec mandates that even out-of-window PDUs (Case A) must
    // be deciphered and decompressed to keep the cipher stream and
    // ROHC decompressor context in sync with the peer.
    //
    // Returns the decoded SDU bytes.
    // ----------------------------------------------------------
    std::vector<uint8_t> decipherAndDecompress(const PdcpPdu& pdu,
                                               uint32_t       hfn_for_decipher);

    // ----------------------------------------------------------
    // Injected dependencies (non-owning references)
    // ----------------------------------------------------------
    IPdcpRxProcedure::Deps deps_;

    // ----------------------------------------------------------
    // RX state variables (moved here from PdcpEntity)
    // TS 36.323 §7.1
    // ----------------------------------------------------------
    SN_t     rx_next_;   // Next_PDCP_RX_SN
    uint32_t rx_hfn_;    // RX_HFN
    SN_t     rx_deliv_;  // Last_Submitted_PDCP_RX_SN

    // ----------------------------------------------------------
    // Reordering / re-establishment store
    // Key = COUNT (not SN) — see RxStoreContainer rationale in
    //       pdcp_rx_procedure.h
    // ----------------------------------------------------------
    RxStoreContainer rx_store_;
};

} // namespace lte
