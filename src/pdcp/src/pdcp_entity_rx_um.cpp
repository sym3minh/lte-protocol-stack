// ============================================================
// pdcp_entity_rx_um.cpp — §5.1.2.1.3 RX path for PdcpEntity (STUB)
//
// "DRBs mapped on RLC UM, reordering function not used"
// Ref: TS 36.323 §5.1.2.1.3
//
// STATUS: stub — rxPduDrbUmNoReorder returns NOT_IMPLEMENTED.
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
  // rxPduDrbUmNoReorder — TS 36.323 §5.1.2.1.3
  // ============================================================
  Status PdcpEntity::rxPduDrbUmNoReorder(ByteBuffer pdu,
                                         bool due_to_reestablishment)
  {
    // Unlike §5.1.2.1.2 (AM no-reorder), there is NO separate branch for
    // re-establishment in §5.1.2.1.3. UM does not buffer SDUs across
    // re-establishment (no rx_store_), so there is nothing to flush.
    //
    // Therefore the flag is accepted but intentionally ignored.
    (void)due_to_reestablishment;
    if (!pdu.valid() || pdu.size() == 0)
      return Status::PARSE_ERROR;

    PdcpHeader hdr;
    Status parse_status = PdcpPduCodec::parseHeader(pdu.data(), pdu.size(),
                                                    pduType(), hdr);
    if (parse_status != Status::OK)
    {
      metrics_.recordDrop();
      return parse_status;
    }
    if (!hdr.isData())
    {
      // Control PDU — not handled by this procedure
      return Status::OK;
    }

    // Advance head pointer, remove header bytes from the data area.
    // After consume(), pdu.data() points directly to the payload.
    // Header bytes remain within the block but outside [head_, tail_).
    pdu.consume(hdr.header_size);

    // HFN update
    if (hdr.sn < rx_next_)
    {
      rx_hfn_++;
    }

    decipherAndDecompress(pdu, rx_hfn_);

    const SN_t max_sn = static_cast<SN_t>(pdcpSnModulus(pduType()) - 1);
    // spec literal; alternatively, can write the condition like RxClassification::ForwardInWindow in pdcp_entity_rx_am.cpp
    rx_next_ = static_cast<SN_t>(hdr.sn + 1);
    if (rx_next_ > max_sn)
    {
      rx_next_ = 0;
      rx_hfn_++;
    }

    // Record delivery metrics (latency = 0 for now; tx_ts lookup reserved)
    metrics_.recordRx(pdu.size(), 0, metrics_.now_ns());

    // Populate test-only last-delivered storage
    last_delivered_sdu_.assign(pdu.data(), pdu.data() + pdu.size());

    if (deliver_cb_)
    {
      deliver_cb_(pdu.data(), pdu.size());
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