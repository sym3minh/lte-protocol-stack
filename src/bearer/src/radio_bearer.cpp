// ============================================================
// radio_bearer.cpp — RadioBearer implementation
// ============================================================

#include "radio_bearer.h"

#include <cassert>
#include <stdexcept>

namespace lte
{

// ============================================================
// Constructor — wiring sequence (4 steps, order matters)
// ============================================================
RadioBearer::RadioBearer(RadioBearerConfig cfg, BufferPool& pool, Clock& clk)
    : cfg_(std::move(cfg))
    , pool_(pool)
    , clk_(clk)
{
    // ── Step 1: Create PDCP entity ───────────────────────────
    pdcp_ = std::make_unique<PdcpEntity>(cfg_.pdcp);

    // ── Step 2: Create RLC entities by mode ──────────────────
    switch (cfg_.rlc_mode)
    {
    case RlcMode::TM:
        rlc_tx_ = std::make_unique<RlcTmTxEntity>(cfg_.tm_cfg, pool_, clk_);
        rlc_rx_ = std::make_unique<RlcTmRxEntity>(cfg_.tm_cfg, clk_);
        break;

    case RlcMode::UM:
        // TODO: Milestone 3 — RlcUmTxEntity / RlcUmRxEntity
        assert(false && "RadioBearer: RlcMode::UM not implemented yet (Milestone 3)");
        break;

    case RlcMode::AM:
        // Out of scope per project plan.
        assert(false && "RadioBearer: RlcMode::AM is out of scope");
        break;

    default:
        assert(false && "RadioBearer: unknown RlcMode");
        break;
    }

    // ── Step 3: Wire PDCP TX → RLC TX ────────────────────────
    // After this, PdcpEntity::txSdu() will call
    //   rlc_tx_->handle_sdu(ByteBuffer, pdcp_sn)
    // directly via the rlc_tx_upper_layer_data_sap pointer.
    pdcp_->set_lower_layer_sap(rlc_tx_.get());

    // ── Step 4: Wire RLC RX → PDCP RX via adapter ────────────
    // PdcpRlcRxAdapter::on_new_pdu(buf) calls pdcp_->rxPdu(buf).
    // rlc_rx_->set_upper_data_notifier() stores the raw pointer;
    // lifetime is safe because rx_adapter_ is owned by this object
    // and pdcp_ outlives the adapter within this class.
    rx_adapter_ = std::make_unique<PdcpRlcRxAdapter>(pdcp_.get());
    rlc_rx_->set_upper_data_notifier(rx_adapter_.get());
}

} // namespace lte
