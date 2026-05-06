#pragma once
// ============================================================
// pdcp_rx_procedure.h — Strategy interface for PDCP RX procedures
//
// Ref: TS 36.323 §5.1.2.1
//
// Three concrete procedures (one per spec section):
//   §5.1.2.1.2  PdcpRxAmNoReorder   — DRBs on RLC AM, no reordering
//   §5.1.2.1.3  PdcpRxUmNoReorder   — DRBs on RLC UM, no reordering (stub)
//   §5.1.2.1.4  PdcpRxWithReorder   — with t-Reordering timer       (stub)
//
// PdcpEntity owns one unique_ptr<IPdcpRxProcedure> and delegates
// all RX operations to it.  Only §5.1.2.1.2 is fully implemented;
// the other two return Status::NOT_IMPLEMENTED until filled in.
// ============================================================

#include "common_types.h"
#include "buffer_pool.h"
#include "pdcp_rohc.h"
#include "pdcp_security.h"
#include "metrics_collector.h"

#include <cstdint>
#include <cstddef>
#include <map>
#include <vector>
#include <functional>

namespace lte {

// ------------------------------------------------------------
// RxClassification — 5-case branching per TS 36.323 §5.1.2.1.2
//
// Used by classifyRx() to map each received SN to exactly one
// spec branch.  Named enumerators match the spec's language to
// allow side-by-side code review against the standard.
// ------------------------------------------------------------
enum class RxClassification : uint8_t {
    OutsideWindow,    // Case A: out-of-window → decipher+decompress, discard
    WrapAhead,        // Case B: Next – received > Reordering_Window → HFN+1
    LateFromPrevHfn,  // Case C: received – Next >= Reordering_Window → HFN-1
    ForwardInWindow,  // Case D: received >= Next (normal forward path)
    BehindSameHfn     // Case E: received < Next (in-order late arrival)
};

// ------------------------------------------------------------
// RxStoreContainer — reordering / re-establishment buffer
//
// Key   = COUNT (= HFN × SN_modulus + SN), NOT raw SN.
//         Two PDUs with the same SN but different HFN (wrap-around
//         boundary) must be distinguishable for correct ascending-
//         COUNT delivery required by the spec.
// Value = decoded SDU bytes (after decipher + ROHC decompress).
//
// std::map chosen because:
//   1. Spec requires delivery in ascending COUNT order →
//      ordered iteration is a natural fit.
//   2. At steady state the buffer is almost always empty, so
//      the log-n overhead vs O(1) is negligible.
//   3. Range-erase (lower_bound + erase) is O(log n + k) which
//      is efficient for batch flush.
//
// Can be swapped to a flat_map or pooled allocator later without
// touching procedure logic — change the alias here only.
// ------------------------------------------------------------
using RxStoreContainer = std::map<uint32_t, std::vector<uint8_t>>;

// ------------------------------------------------------------
// SduDeliverCallback — forward declaration alias
// (matches the one in pdcp_entity.h; repeated here so this
//  header is self-contained for the procedure classes)
// ------------------------------------------------------------
using RxDeliverCallback = std::function<void(const uint8_t* sdu, size_t len)>;

// ------------------------------------------------------------
// IPdcpRxProcedure — abstract base for all RX procedures
// ------------------------------------------------------------
class IPdcpRxProcedure {
public:
    // Dependencies injected by PdcpEntity at construction time.
    // Procedure holds non-owning references; entity must outlive
    // all procedure objects (enforced by unique_ptr ownership).
    struct Deps {
        PdcpSecurity&     security;
        PdcpRohc&         rohc;
        MetricsCollector& metrics;
        BufferPool&       pool;
        RxDeliverCallback deliver_cb;   // hand SDU up to upper layer
        BearerType        bearer;       // needed for SN modulus / window
    };

    virtual ~IPdcpRxProcedure() = default;

    // ----------------------------------------------------------
    // rxPdu — process one PDCP Data PDU arriving from lower layer
    //
    // raw_pdu / raw_len : serialised PDU bytes from RLC
    // due_to_reestablishment :
    //   true  → PDU belongs to the first batch after RLC re-
    //            establishment (handover, RLF recovery).
    //            §5.1.2.1.2 / §5.1.2.1.3 apply special delivery
    //            rules: only deliver if SN == Last_Submitted + 1
    //            (or wrap equivalent); otherwise buffer and wait.
    //   false → normal path, deliver aggressively (all stored
    //            SDUs with COUNT < received + consecutive run).
    //
    // Default is false so existing callers compile unchanged.
    // ----------------------------------------------------------
    virtual Status rxPdu(const uint8_t* raw_pdu,
                         size_t         raw_len,
                         bool           due_to_reestablishment = false) = 0;

    // ----------------------------------------------------------
    // reestablish — reset procedure state per TS 36.323 §5.2.2
    //
    // Called by upper layer (RRC) when a re-establishment event
    // occurs.  Each procedure handles it differently:
    //   §5.1.2.1.2 (AM no-reorder): do NOT reset SN/HFN state
    //     unless upper layer supplies stored UE AS context.
    //     Always reset ROHC downlink, clear rx_store_.
    //   §5.1.2.1.3 (UM no-reorder): reset Next_PDCP_RX_SN and
    //     RX_HFN to 0 per §5.2.2.2.
    //   §5.1.2.1.4 (with reorder): stop t-Reordering, flush
    //     buffer, reset state.
    // ----------------------------------------------------------
    virtual void reestablish() = 0;

    // ----------------------------------------------------------
    // State accessors — used by tests and PdcpEntity pass-through
    // ----------------------------------------------------------
    virtual SN_t     rxNext()  const = 0;   // Next_PDCP_RX_SN
    virtual uint32_t rxHfn()   const = 0;   // RX_HFN
    virtual SN_t     rxDeliv() const = 0;   // Last_Submitted_PDCP_RX_SN
};

} // namespace lte
