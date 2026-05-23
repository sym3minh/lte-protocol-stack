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
  PdcpEntity::PdcpEntity(LCID_t lcid,
                         BearerType bearer,
                         RlcMode rlc_mode,
                         BufferPool &pool,
                         RxMode rx_mode)
      : lcid_(lcid), bearer_(bearer), rlc_mode_(rlc_mode), rx_mode_(rx_mode), tx_next_(0), tx_hfn_(0), rx_next_(0), rx_hfn_(0)
        // TS 36.323 §5.2.2.1: "set Last_Submitted_PDCP_RX_SN to Maximum_PDCP_SN"
        ,
        rx_deliv_(static_cast<SN_t>(snModulus(bearer) - 1)), pool_(pool)
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
  PdcpEntity::PdcpEntity(LCID_t lcid,
                         BearerType bearer,
                         RlcMode rlc_mode,
                         BufferPool &pool,
                         RxMode rx_mode,
                         TestInitState init)
      : PdcpEntity(lcid, bearer, rlc_mode, pool, rx_mode) // delegate
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
  // Stored directly on the entity; rxPduAmNoReorder (and future
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
  Status PdcpEntity::rxPduDispatch(const uint8_t *raw_pdu,
                                   size_t raw_len,
                                   bool due_to_reestablishment)
  {
    switch (rx_mode_)
    {
    case RxMode::AmNoReorder:
      return rxPduAmNoReorder(raw_pdu, raw_len, due_to_reestablishment);
    case RxMode::UmNoReorder:
      return rxPduUmNoReorder(raw_pdu, raw_len, due_to_reestablishment);
    case RxMode::WithReorder:
      return rxPduWithReorder(raw_pdu, raw_len, due_to_reestablishment);
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
  Status PdcpEntity::txSdu(const uint8_t *sdu, size_t sdu_len)
  {
    if (!sdu || sdu_len == 0 || sdu_len > PDCP_MAX_SDU_SIZE)
      return Status::PARSE_ERROR;

    const size_t header_sz = PdcpPduCodec::headerSize(bearer_);
    const size_t needed = header_sz + sdu_len + 4; // +4 future MAC-I

    if (needed > pool_.blockSize())
      return Status::POOL_EXHAUSTED;

    uint8_t *block = pool_.allocate();
    if (!block)
    {
      metrics_.recordDrop();
      return Status::POOL_EXHAUSTED;
    }

    // Place SDU right after where the header will go
    std::memcpy(block + header_sz, sdu, sdu_len);
    size_t payload_len = sdu_len;

    // Step 2: RoHC (stub: no-op) — affects SDU header only
    rohc_.compress(block + header_sz, payload_len);

    // Step 3: ciphering using COUNT = TX_HFN × modulus + tx_next_
    const bool is_srb = (bearer_ != BearerType::DRB);
    security_.applyCiphering(block + header_sz, payload_len, tx_next_, is_srb);

    // Step 4: record tx timestamp keyed by COUNT before SN advances
    const uint32_t tx_count = countValue(tx_hfn_, tx_next_);
    tx_ts_map_[tx_count] = metrics_.now_ns();

    PdcpPdu pdu;
    pdu.sn = tx_next_;
    pdu.dc = PDCP_DC_DATA;
    pdu.bearer = bearer_;
    pdu.payload = block + header_sz;
    pdu.payload_len = payload_len;

    size_t serialised_len = PdcpPduCodec::serialize(pdu, block, pool_.blockSize());
    if (serialised_len == 0)
    {
      pool_.deallocate(block);
      return Status::PARSE_ERROR;
    }

    // Step 5: advance Next_PDCP_TX_SN + TX_HFN per spec §5.1.1
    const SN_t max_sn = static_cast<SN_t>(snModulus(bearer_) - 1);
    if (tx_next_ >= max_sn)
    {
      tx_next_ = 0;
      ++tx_hfn_;
    }
    else
    {
      ++tx_next_;
    }

    metrics_.recordTx(serialised_len);
    last_tx_pdu_.assign(block, block + serialised_len);

    if (tx_cb_)
      tx_cb_(block, serialised_len);

    pool_.deallocate(block);
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
  std::vector<uint8_t>
  PdcpEntity::decipherAndDecompress(const PdcpPdu &pdu, uint32_t hfn_for_decipher)
  {
    (void)hfn_for_decipher; // available for future real cipher; stub ignores it

    std::vector<uint8_t> work(pdu.payload, pdu.payload + pdu.payload_len);

    const bool is_srb = (bearer_ != BearerType::DRB);
    security_.applyDeciphering(work.data(), work.size(), pdu.sn, is_srb);

    // If applying Integrity Verification, it would affect the entire PDU
    // (PDCP header + Data field).  Reserved for future revision.

    size_t decompressed_len = work.size();
    rohc_.decompress(work.data(), decompressed_len);
    work.resize(decompressed_len);

    return work;
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
    const SN_t max_sn = static_cast<SN_t>(snModulus(bearer_) - 1);
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
