#pragma once
// ============================================================
// pdcp_rx_um_noreorder.h — §5.1.2.1.3 RX procedure (STUB)
//
// "DRBs mapped on RLC UM, reordering function not used"
// Ref: TS 36.323 §5.1.2.1.3
//
// STATUS: stub — all methods compile but rxPdu returns
//   Status::NOT_IMPLEMENTED.
// TODO: implement TS 36.323 §5.1.2.1.3 in a future revision.
//   reestablish() must reset Next_PDCP_RX_SN and RX_HFN to 0
//   per §5.2.2.2.
// ============================================================

#include "pdcp_rx_procedure.h"

namespace lte {

class PdcpRxUmNoReorder final : public IPdcpRxProcedure {
public:
    explicit PdcpRxUmNoReorder(IPdcpRxProcedure::Deps deps)
        : deps_(deps)
        , rx_next_(0)
        , rx_hfn_(0)
        , rx_deliv_(0)
    {}

    Status rxPdu(const uint8_t*, size_t, bool) override {
        // TODO: implement TS 36.323 §5.1.2.1.3
        return Status::NOT_IMPLEMENTED;
    }

    void reestablish() override {
        // TODO: per §5.2.2.2 — reset Next_PDCP_RX_SN = 0, RX_HFN = 0
    }

    SN_t     rxNext()  const override { return rx_next_;  }
    uint32_t rxHfn()   const override { return rx_hfn_;   }
    SN_t     rxDeliv() const override { return rx_deliv_; }

private:
    IPdcpRxProcedure::Deps deps_;
    SN_t     rx_next_;
    uint32_t rx_hfn_;
    SN_t     rx_deliv_;
};

} // namespace lte
