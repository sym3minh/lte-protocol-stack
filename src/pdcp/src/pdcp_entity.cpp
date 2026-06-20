#include "pdcp_entity.h"

#include <cstring>
#include <cassert>
#include <stdexcept>

namespace lte
{

  // ============================================================
  // Constructor
  //
  // Stores rx_mode and initialises all state variables per spec.
  // Sub-components (rohc_, security_, metrics_) are value-initialised
  // by their own default constructors.
  //
  // RX state per TS 36.323 §5.2.2.1 defaults:
  //   Next_PDCP_RX_SN          = 0
  //   RX_HFN                   = 0
  //   Last_Submitted_PDCP_RX_SN = Maximum_PDCP_SN  (= snModulus - 1)
  // ============================================================
  PdcpEntity::PdcpEntity(PdcpConfig cfg)
      : cfg_(cfg), tx_next_(0), tx_hfn_(0), rx_next_(0), rx_hfn_(0)
        // TS 36.323 §5.2.2.1: "set Last_Submitted_PDCP_RX_SN to Maximum_PDCP_SN"
        ,
        rx_deliv_(static_cast<SN_t>(pdcpSnModulus(cfg.pdu_type) - 1))
  {
  }

  // ============================================================
  // Test-only constructor
  //
  // Delegates to the normal constructor for all sub-component
  // initialisation, then overrides both TX and RX state with
  // caller-provided values.  This avoids duplicating sub-component
  // init logic while still allowing arbitrary starting conditions
  // for wrap-around and boundary tests.
  // ============================================================
  PdcpEntity::PdcpEntity(PdcpConfig cfg,
                         TestInitState init)
      : PdcpEntity(cfg) // delegate
  {
    // Override RX state with test-supplied values
    rx_next_ = init.rx_next;
    rx_hfn_ = init.rx_hfn;
    rx_deliv_ = init.rx_deliv;

    // Override TX state with test-supplied values
    tx_next_ = init.tx_next;
    tx_hfn_ = init.tx_hfn;
  }

  PdcpEntity::~PdcpEntity() = default;

  // ============================================================
  // setDeliverCallback
  //
  // Stored directly on the entity; rxPduDrbAmNoReorder (and future
  // mode handlers) call deliver_cb_ directly — no lambda wrapper
  // needed since there is no intermediate procedure object.
  // ============================================================
  void PdcpEntity::setDeliverCallback(SduDeliverCallback cb)
  {
    deliver_cb_ = std::move(cb);
  }

  // ============================================================
  // rxPduDispatch — routes incoming PDU to the correct mode handler
  //
  // Called by the public inline rxPdu().  Returning NOT_IMPLEMENTED
  // for stub modes satisfies the compiler's return requirement.
  // ============================================================
  Status PdcpEntity::rxPduDispatch(ByteBuffer pdu,
                                   bool due_to_reestablishment)
  {
    if (cfg_.bearer != BearerType::DRB)
      return Status::NOT_IMPLEMENTED;
    if (cfg_.reordering_enabled)
      return rxPduWithReorder(std::move(pdu), due_to_reestablishment);
    switch (cfg_.rlc_mode)
    {
    case RlcMode::AM:
      return rxPduDrbAmNoReorder(std::move(pdu), due_to_reestablishment);
    case RlcMode::UM:
      return rxPduDrbUmNoReorder(std::move(pdu), due_to_reestablishment);
    }
    return Status::NOT_IMPLEMENTED; // unreachable — satisfies compiler
  }

  // ============================================================
  // Transmit path — TS 36.323 §5.1.1
  //
  // Steps per spec:
  //   1. Assign Next_PDCP_TX_SN as SN for this SDU
  //   2. RoHC header compression (stub: pass-through)
  //   3. Integrity protection then ciphering using
  //      COUNT = TX_HFN × modulus + SN  (§6.3.5)
  //   4. Build PDCP Data PDU
  //   5. Increment Next_PDCP_TX_SN; if > Maximum_PDCP_SN → wrap + TX_HFN++
  //   6. Submit PDU to lower layer
  //
  // TX path is unchanged from the Strategy-pattern revision.
  // ============================================================
  Status PdcpEntity::txSdu(ByteBuffer sdu)
  {
    if (!sdu.valid() || sdu.size() == 0 || sdu.size() > PDCP_MAX_SDU_SIZE)
      return Status::PARSE_ERROR;

    const size_t header_sz = PdcpPduCodec::headerSize(pduType());
    if (sdu.headroom() < header_sz)
      return Status::HeaderRoomExhausted;

    const SN_t sn = tx_next_;

    // Step 2: RoHC (stub: no-op) — affects SDU header only
    rohc_.compress(sdu.data(), sdu.size());

    // Step 3: ciphering using COUNT = TX_HFN × modulus + tx_next_
    const bool is_srb = (bearer() != BearerType::DRB);
    security_.applyCiphering(sdu.data(), sdu.size(), sn, is_srb);

    // Step 4: record tx timestamp keyed by COUNT before SN advances
    const uint32_t tx_count = countValue(tx_hfn_, tx_next_);
    tx_ts_map_[tx_count] = metrics_.now_ns();

    // Step 5: prepend PDCP header
    PdcpHeader hdr_pdu;
    hdr_pdu.sn = sn;

    uint8_t hdr_buf[MAX_PDCP_HEADER_SIZE];
    size_t hdr_len = PdcpPduCodec::buildHeader(hdr_pdu, hdr_buf, pduType());
    if (!sdu.prepend(hdr_buf, hdr_len))
    {
      return Status::HeaderRoomExhausted;
    }

    // Step 6: advance Next_PDCP_TX_SN + TX_HFN per spec §5.1.1
    const SN_t max_sn = static_cast<SN_t>(pdcpSnModulus(pduType()) - 1);
    if (tx_next_ >= max_sn)
    {
      tx_next_ = 0;
      ++tx_hfn_;
    }
    else
    {
      ++tx_next_;
    }

    metrics_.recordTx(sdu.size());

    // ── Step 7: submit xuống RLC ────────────────────────────
    if (lower_dn_)
    {
      lower_dn_->handle_sdu(std::move(sdu), sn);
    }

    return Status::OK;
  }

  // ============================================================
  // decipherAndDecompress — shared helper
  //
  // Called for BOTH accepted PDUs and discarded (Case A) PDUs.
  // Even when discarding, we must run decipher + ROHC decompress
  // to keep the cipher stream and ROHC decompressor in sync with
  // the peer.
  //
  // Note: only the Data field of the PDU is affected (not the
  // PDCP header).  Integrity Verification (if applied) would
  // cover the entire PDU including the header — reserved for
  // future implementation.
  // ============================================================
  void PdcpEntity::decipherAndDecompress(ByteBuffer &pdu, uint32_t hfn_for_decipher)
  {
    (void)hfn_for_decipher; // available for future real cipher; stub ignores it
  }

  // ============================================================
  // Control plane — stubs
  // ============================================================
  void PdcpEntity::discardSdu(SN_t sn)
  {
    // TODO: cancel discardTimer for sn, remove from Tx buffer
    (void)sn;
  }

  void PdcpEntity::triggerStatusReport()
  {
    // TODO: build PDCP Control PDU (D/C=0) per §5.3 and submit to RLC
  }

  // ============================================================
  // TX SN helper
  // ============================================================
  SN_t PdcpEntity::nextTxSn()
  {
    // Kept for potential future use; txSdu() manages SN inline.
    SN_t sn = tx_next_;
    const SN_t max_sn = static_cast<SN_t>(pdcpSnModulus(pduType()) - 1);
    if (tx_next_ >= max_sn)
    {
      tx_next_ = 0;
      ++tx_hfn_;
    }
    else
    {
      ++tx_next_;
    }
    return sn;
  }

} // namespace lte