#pragma once
// ============================================================
// radio_bearer.h — RadioBearer: top-level ownership and wiring
//
// Represents one DRB/SRB at a single side (UE or eNB).
// Owns PdcpEntity + IRlcTxEntity + IRlcRxEntity and wires
// all four SAP connections:
//
//   1. PDCP TX → RLC TX  : pdcp_->set_lower_layer_sap(rlc_tx_.get())
//   2. RLC RX → PDCP RX  : rlc_rx_->set_upper_data_notifier(adapter)
//      via PdcpRlcRxAdapter (nested private class)
//
// Usage:
//   auto bearer_A = RadioBearer(cfg, pool, clk);
//   auto bearer_B = RadioBearer(cfg, pool, clk);
//
//   MacStub mac_A(mac_cfg, clk); mac_A.attach_rlc_tx(bearer_A.rlc_tx());
//   MacStub mac_B(mac_cfg, clk); mac_B.attach_rlc_tx(bearer_B.rlc_tx());
//   mac_A.pair(mac_B);
//
//   bearer_A.setDeliverCallback([](const uint8_t* d, size_t n){ ... });
//   bearer_A.txSdu(buf);  // downward
//   mac_A.tick();         // drive MAC
//
// Design decisions (per guide):
//   - RadioBearer does NOT have tick() — MAC is driven externally.
//     In a real eNB, one MAC scheduler drives many bearers.
//   - BufferPool is a reference — app/test retains ownership.
//   - PdcpRlcRxAdapter is a nested private class — not exposed.
//   - No raw new: all owned objects via make_unique.
// ============================================================

#include "pdcp_entity.h"
#include "rlc_entity.h"
#include "rlc_tm.h"
#include "rlc_sap.h"
#include "buffer_pool.h"
#include "clock.h"
#include "common_types.h"

#include <memory>
#include <functional>

namespace lte
{

// ============================================================
// RadioBearerConfig
// ============================================================
struct RadioBearerConfig
{
    PdcpConfig  pdcp;
    RlcMode     rlc_mode = RlcMode::TM;

    // RLC mode-specific config:
    RlcTmConfig tm_cfg;
    // (UM / AM configs added in future milestones)
};

// ============================================================
// RadioBearer
// ============================================================
class RadioBearer
{
public:
    RadioBearer(RadioBearerConfig cfg, BufferPool& pool, Clock& clk);

    // Non-copyable (owns unique_ptrs to entities)
    RadioBearer(const RadioBearer&)            = delete;
    RadioBearer& operator=(const RadioBearer&) = delete;

    // ── Application interface ────────────────────────────────

    // Push an SDU down from the application layer.
    // ByteBuffer must have been allocated from the shared pool.
    Status txSdu(ByteBuffer sdu)
    {
        return pdcp_->txSdu(std::move(sdu));
    }

    // Register callback for SDUs delivered up from PDCP to the app.
    // Signature: void(const uint8_t* data, size_t len)
    void setDeliverCallback(SduDeliverCallback cb)
    {
        pdcp_->setDeliverCallback(std::move(cb));
    }

    // ── Accessors for MAC wiring ─────────────────────────────

    IRlcTxEntity* rlc_tx() { return rlc_tx_.get(); }
    IRlcRxEntity* rlc_rx() { return rlc_rx_.get(); }
    PdcpEntity*   pdcp()   { return pdcp_.get(); }

    // ── Metrics ─────────────────────────────────────────────
    PdcpMetrics pdcpMetrics() const { return pdcp_->getMetrics(); }

private:
    // ── PdcpRlcRxAdapter ─────────────────────────────────────
    //
    // Bridges the RLC Rx notifier interface to PdcpEntity::rxPdu().
    //
    // Why nested private?
    //   - Only used internally by RadioBearer.
    //   - Avoids polluting namespace lte with a glue class.
    //   - Lifetime is tied to RadioBearer (owned via unique_ptr).
    //   - If PDCP later directly implements the notifier interface,
    //     just remove this adapter and add the base class — local change.
    class PdcpRlcRxAdapter final : public rlc_rx_upper_layer_data_notifier
    {
    public:
        explicit PdcpRlcRxAdapter(PdcpEntity* p) : pdcp_(p) {}

        void on_new_pdu(ByteBuffer pdu) override
        {
            // Forward RLC PDU up to PDCP Rx path.
            // For TM: pdu is a raw PDCP PDU (no RLC header).
            // For UM/AM: pdu is reassembled PDCP PDU (RLC header stripped by RLC).
            pdcp_->rxPdu(std::move(pdu));
        }

    private:
        PdcpEntity* pdcp_; // non-owning pointer; RadioBearer owns pdcp_
    };

    // ── Members ─────────────────────────────────────────────
    RadioBearerConfig cfg_;
    BufferPool&       pool_;
    Clock&            clk_;

    std::unique_ptr<PdcpEntity>           pdcp_;
    std::unique_ptr<IRlcTxEntity>         rlc_tx_;
    std::unique_ptr<IRlcRxEntity>         rlc_rx_;
    std::unique_ptr<PdcpRlcRxAdapter>     rx_adapter_;
};

} // namespace lte
