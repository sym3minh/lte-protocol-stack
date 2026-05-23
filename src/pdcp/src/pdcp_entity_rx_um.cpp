// ============================================================
// pdcp_entity_rx_um.cpp — §5.1.2.1.3 RX path for PdcpEntity (STUB)
//
// "DRBs mapped on RLC UM, reordering function not used"
// Ref: TS 36.323 §5.1.2.1.3
//
// STATUS: stub — rxPduUmNoReorder returns NOT_IMPLEMENTED.
//
// TODO: implement §5.1.2.1.3 in a future revision.
//   Key differences from AM path (§5.1.2.1.2):
//     - SN is 5-bit or 10-bit (TS 36.323 §6.3.4) vs 12-bit for DRB/AM
//     - No rx_store_ retention across re-establishment
//     - reestablishUm() must reset rx_next_ = 0 and rx_hfn_ = 0 (§5.2.2.2)
// ============================================================

#include "pdcp_entity.h"

namespace lte
{

  // ============================================================
  // rxPduUmNoReorder — TS 36.323 §5.1.2.1.3
  // ============================================================
  Status PdcpEntity::rxPduUmNoReorder(const uint8_t *raw_pdu,
                                      size_t raw_len,
                                      bool due_to_reestablishment)
  {
    // Unlike §5.1.2.1.2 (AM no-reorder), there is NO separate branch for
    // re-establishment in §5.1.2.1.3. UM does not buffer SDUs across
    // re-establishment (no rx_store_), so there is nothing to flush.
    //
    // Therefore the flag is accepted but intentionally ignored.
    (void)due_to_reestablishment;
    if (!raw_pdu || raw_len == 0)
      return Status::PARSE_ERROR;

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

    if (pdu.sn < rx_next_)
    {
      rx_hfn_++;
    }

    std::vector<uint8_t> sdu = decipherAndDecompress(pdu, rx_hfn_);

    const SN_t max_sn = static_cast<SN_t>(snModulus(bearer_) - 1);
    // spec literal; alternatively, can write the condition like RxClassification::ForwardInWindow in pdcp_entity_rx_am.cpp
    rx_next_ = static_cast<SN_t>(pdu.sn + 1);
    if (rx_next_ > max_sn)
    {
      rx_next_ = 0;
      rx_hfn_++;
    }

    // Record delivery metrics (latency = 0 for now; tx_ts lookup reserved)
    metrics_.recordRx(sdu.size(), 0, metrics_.now_ns());

    // Populate test-only last-delivered storage
    last_delivered_sdu_.assign(sdu.begin(), sdu.end());

    if (deliver_cb_)
    {
      deliver_cb_(sdu.data(), sdu.size());
    }

    return Status::OK;
  }

  // ============================================================
  // reestablishUm — TS 36.323 §5.2.2.2 (STUB)
  //
  // When implemented:
  //   rx_next_ = 0;
  //   rx_hfn_  = 0;
  //   rx_store_.clear();
  //   rohc_.resetDownlink();
  // ============================================================
  void PdcpEntity::reestablishUm()
  {
    // TODO: per §5.2.2.2 — reset Next_PDCP_RX_SN = 0, RX_HFN = 0
  }

} // namespace lte
