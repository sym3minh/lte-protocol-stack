#pragma once
// ============================================================
// pdcp_entity.h — one PDCP entity per radio bearer (façade)
//
// Ref: TS 36.323 §5 (procedures), §7 (state variables)
//
// Responsibilities:
//   TX path : SN assignment → RoHC compress → cipher → PDU build
//   RX path : delegate entirely to IPdcpRxProcedure strategy object
//
// RX mode is selected at construction time via RxMode enum and
// cannot be changed at runtime (mode is fixed by RRC DRB config).
//
// TX state (tx_next_, tx_hfn_) and all sub-components
// (pool_, rohc_, security_, metrics_) remain here.
// RX state variables (rx_next_, rx_hfn_, rx_deliv_) have been
// moved to the concrete procedure class; entity exposes them
// through pass-through accessors.
// ============================================================

#include "common_types.h"
#include "buffer_pool.h"
#include "pdcp_pdu.h"
#include "pdcp_rohc.h"
#include "pdcp_security.h"
#include "metrics_collector.h"
#include "pdcp_rx_procedure.h"

#include <cstdint>
#include <cstddef>
#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include <memory>

namespace lte {

// Callback types so the entity can hand data to adjacent layers
// without circular includes.
using SduDeliverCallback = std::function<void(const uint8_t* sdu, size_t len)>;
using PduForwardCallback = std::function<void(const uint8_t* pdu, size_t len)>;

class PdcpEntity {
public:
    // ----------------------------------------------------------
    // RxMode — selects which §5.1.2.1.x procedure to instantiate
    //
    // AmNoReorder  §5.1.2.1.2  DRBs on RLC AM, no reordering  [IMPLEMENTED]
    // UmNoReorder  §5.1.2.1.3  DRBs on RLC UM, no reordering  [STUB]
    // WithReorder  §5.1.2.1.4  with t-Reordering timer         [STUB]
    // ----------------------------------------------------------
    enum class RxMode : uint8_t {
        AmNoReorder,   // §5.1.2.1.2
        UmNoReorder,   // §5.1.2.1.3
        WithReorder    // §5.1.2.1.4
    };

    // lcid    : Logical Channel ID
    // bearer  : SRB1 / SRB2 / DRB
    // rlc_mode: RLC mode below (affects SN size / discard behaviour)
    // pool    : shared buffer pool (must outlive this entity)
    // rx_mode : which RX procedure to use (default = AmNoReorder)
    PdcpEntity(LCID_t     lcid,
               BearerType bearer,
               RlcMode    rlc_mode,
               BufferPool& pool,
               RxMode     rx_mode = RxMode::AmNoReorder);

    ~PdcpEntity();

    // Non-copyable — owns pool references and unique_ptr state
    PdcpEntity(const PdcpEntity&)             = delete;
    PdcpEntity& operator=(const PdcpEntity&)  = delete;

    // ----------------------------------------------------------
    // Callbacks (set before first Tx/Rx)
    // ----------------------------------------------------------

    // Called when a PDU is ready to be sent down to RLC
    void setTxCallback(PduForwardCallback cb) { tx_cb_ = std::move(cb); }

    // Called when an SDU is ready to be delivered up to IP/RRC
    void setDeliverCallback(SduDeliverCallback cb);

    // ----------------------------------------------------------
    // Transmit path (upper → lower)  — unchanged from prior revision
    // ----------------------------------------------------------

    // Accept an SDU from RRC/IP layer, wrap it in a PDCP PDU,
    // and invoke the tx_callback with the serialised PDU bytes.
    // Returns Status::OK on success, Status::POOL_EXHAUSTED if
    // no buffer is available.
    Status txSdu(const uint8_t* sdu, size_t sdu_len);

    // ----------------------------------------------------------
    // Receive path (lower → upper)  — delegates to rx_proc_
    // ----------------------------------------------------------

    // Accept a raw PDU from RLC.
    // due_to_reestablishment: set true for the first PDU batch
    // after an RLC re-establishment event (handover / RLF).
    // Default false keeps existing callers compile-compatible.
    Status rxPdu(const uint8_t* raw_pdu,
                 size_t         raw_len,
                 bool           due_to_reestablishment = false)
    {
        return rx_proc_->rxPdu(raw_pdu, raw_len, due_to_reestablishment);
    }

    // ----------------------------------------------------------
    // Re-establishment (TS 36.323 §5.2.2)
    // ----------------------------------------------------------
    void reestablish() { rx_proc_->reestablish(); }

    // ----------------------------------------------------------
    // State variable accessors (TS 36.323 §7.1)
    // ----------------------------------------------------------
    SN_t     txNext()  const { return tx_next_; }   // Next_PDCP_TX_SN
    uint32_t txHfn()   const { return tx_hfn_; }    // TX_HFN

    // RX accessors — pass-through to procedure object
    SN_t     rxNext()  const { return rx_proc_->rxNext();  }  // Next_PDCP_RX_SN
    uint32_t rxHfn()   const { return rx_proc_->rxHfn();   }  // RX_HFN
    SN_t     rxDeliv() const { return rx_proc_->rxDeliv(); }  // Last_Submitted_PDCP_RX_SN

    // Compute the full 32-bit COUNT for any SN + HFN pair
    // COUNT = HFN × SN_modulus + SN  (TS 36.323 §6.3.5)
    uint32_t countValue(uint32_t hfn, SN_t sn) const
    {
        return hfn * snModulus(bearer_) + sn;
    }

    // ----------------------------------------------------------
    // Test helpers (not part of the production interface)
    // ----------------------------------------------------------

    // Last serialised PDU built by txSdu()
    const std::vector<uint8_t>& lastTxPdu() const { return last_tx_pdu_; }

    // Last SDU delivered by rxPdu() (populated by deliver callback)
    const std::vector<uint8_t>& lastDeliveredSdu() const { return last_delivered_sdu_; }

    // ----------------------------------------------------------
    // Control plane
    // ----------------------------------------------------------
    void discardSdu(SN_t sn);           // TS 36.323 §5.2.1
    void triggerStatusReport();         // TS 36.323 §5.5

    // ----------------------------------------------------------
    // Metrics
    // ----------------------------------------------------------
    PdcpMetrics getMetrics() const { return metrics_.snapshot(); }

private:
    // ----------------------------------------------------------
    // Configuration
    // ----------------------------------------------------------
    LCID_t     lcid_;
    BearerType bearer_;
    RlcMode    rlc_mode_;

    // ----------------------------------------------------------
    // TX state variables (TS 36.323 §7.1)
    //
    //   Next_PDCP_TX_SN  — SN to assign to the next outgoing PDU
    //   TX_HFN           — upper part of COUNT on the Tx side
    // ----------------------------------------------------------
    SN_t     tx_next_ = 0;
    uint32_t tx_hfn_  = 0;

    // ----------------------------------------------------------
    // RX procedure — owns one concrete IPdcpRxProcedure instance.
    // RX state (rx_next_, rx_hfn_, rx_deliv_) and RX logic live
    // inside the procedure object; entity exposes them via
    // pass-through accessors above.
    // ----------------------------------------------------------
    std::unique_ptr<IPdcpRxProcedure> rx_proc_;

    // ----------------------------------------------------------
    // Sub-components (owned by entity; shared with rx_proc_ via Deps refs)
    // ----------------------------------------------------------
    BufferPool&      pool_;
    PdcpRohc         rohc_;
    PdcpSecurity     security_;
    MetricsCollector metrics_;

    // ----------------------------------------------------------
    // Latency measurement map: COUNT → tx_timestamp_ns
    //
    // Keyed by COUNT (= HFN × SN_modulus + SN) — globally unique
    // for the bearer lifetime, so no wrap-around collision.
    // Erased when the matching Rx PDU arrives.
    // ----------------------------------------------------------
    std::unordered_map<uint32_t, uint64_t> tx_ts_map_;

    // ----------------------------------------------------------
    // Callbacks
    // ----------------------------------------------------------
    PduForwardCallback tx_cb_;
    SduDeliverCallback deliver_cb_;

    // ----------------------------------------------------------
    // Test-only storage
    // ----------------------------------------------------------
    std::vector<uint8_t> last_tx_pdu_;
    std::vector<uint8_t> last_delivered_sdu_;

    // ----------------------------------------------------------
    // TX helper
    // ----------------------------------------------------------
    SN_t nextTxSn();
};

} // namespace lte
