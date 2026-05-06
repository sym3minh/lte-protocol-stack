#pragma once
// ============================================================
// pdcp_rx_with_reorder.h — §5.1.2.1.4 RX procedure (STUB)
//
// "DRBs with reordering function"
// Ref: TS 36.323 §5.1.2.1.4
//
// STATUS: stub — all methods compile but rxPdu returns
//   Status::NOT_IMPLEMENTED.
// TODO: implement TS 36.323 §5.1.2.1.4 in a future revision.
//   Requires:
//     - t-Reordering timer (duration from RRC config)
//     - onReorderingTimerExpiry():
//         deliver all stored SDUs with COUNT < Reordering_PDCP_RX_COUNT
//         then consecutive run from that COUNT
//     - rx_reord_count_ (Reordering_PDCP_RX_COUNT per spec §7.1)
//     - RxStoreContainer with timestamp per entry for timer management
//   reestablish(): stop t-Reordering, flush buffer, reset state.
// ============================================================

#include "pdcp_rx_procedure.h"

namespace lte {

class PdcpRxWithReorder final : public IPdcpRxProcedure {
public:
    explicit PdcpRxWithReorder(IPdcpRxProcedure::Deps deps)
        : deps_(deps)
        , rx_next_(0)
        , rx_hfn_(0)
        , rx_deliv_(0)
        , rx_reord_count_(0)   // Reordering_PDCP_RX_COUNT — reserved for §5.1.2.1.4
    {}

    Status rxPdu(const uint8_t*, size_t, bool) override {
        // TODO: implement TS 36.323 §5.1.2.1.4
        return Status::NOT_IMPLEMENTED;
    }

    void reestablish() override {
        // TODO: stop t-Reordering timer, flush rx_store_, reset state
        //   rx_next_  = 0
        //   rx_hfn_   = 0
        //   rx_deliv_ = Maximum_PDCP_SN
        //   rx_reord_count_ = 0
    }

    SN_t     rxNext()  const override { return rx_next_;  }
    uint32_t rxHfn()   const override { return rx_hfn_;   }
    SN_t     rxDeliv() const override { return rx_deliv_; }

private:
    IPdcpRxProcedure::Deps deps_;
    SN_t     rx_next_;
    uint32_t rx_hfn_;
    SN_t     rx_deliv_;

    // Reserved for §5.1.2.1.4 implementation:
    uint32_t rx_reord_count_;   // Reordering_PDCP_RX_COUNT
    // RxStoreContainer rx_store_; // value type will need timestamp for timer
    // TimerHandle reordering_timer_;
};

} // namespace lte
