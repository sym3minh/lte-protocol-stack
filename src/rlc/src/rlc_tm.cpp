// ============================================================
// rlc_tm.cpp — RLC Transparent Mode (TM) implementation
//
// Spec: TS 36.322 §5.1.1
// ============================================================

#include "rlc_tm.h"

#include <cassert>
#include <utility>

namespace lte
{

  // ============================================================
  // RlcTmTxEntity
  // ============================================================

  RlcTmTxEntity::RlcTmTxEntity(RlcTmConfig cfg, BufferPool &pool, Clock &clk)
      : cfg_(cfg), pool_(pool), clk_(clk)
  {
  }

  // ------------------------------------------------------------
  // handle_sdu — PDCP pushes SDU down to TM Tx
  // ------------------------------------------------------------
  void RlcTmTxEntity::handle_sdu(ByteBuffer sdu, uint32_t /*pdcp_sn*/)
  {
    // pdcp_sn intentionally ignored for TM:
    //   TM has no SDU discard mechanism (TS 36.322 §5.1.1.1 —
    //   no sequence numbering, no discard timer).

    if (!sdu.valid() || sdu.size() == 0)
    {
      // Silently discard empty / invalid buffer.
      return;
    }

    // Optional back-pressure: if max_tx_queue_bytes is set,
    // drop the incoming SDU when queue would exceed the limit.
    if (cfg_.max_tx_queue_bytes > 0 &&
        (queue_bytes_ + sdu.size()) > cfg_.max_tx_queue_bytes)
    {
      // Drop — caller must handle this case if they care.
      // (In TM production use on CCCH/BCCH, queue depth is tiny
      //  and this path should never fire.)
      return;
    }

    queue_bytes_ += sdu.size();
    tx_queue_.push_back(std::move(sdu));
  }

  // ------------------------------------------------------------
  // buildPdu — MAC pulls one PDU for transmission
  // ------------------------------------------------------------
  ByteBuffer RlcTmTxEntity::buildPdu(size_t grant_bytes)
  {
    if (tx_queue_.empty())
    {
      return ByteBuffer{}; // invalid — nothing to send
    }

    ByteBuffer &front = tx_queue_.front();

    if (front.size() > grant_bytes)
    {
      // Grant too small — keep SDU queued, signal MAC to try later.
      // Option B: no drop. MAC should issue a larger grant next TTI.
      ++tm_grant_too_small_;
      return ByteBuffer{}; // invalid — grant insufficient
    }

    // Pop SDU from queue: zero-copy move.
    ByteBuffer pdu = std::move(front);
    tx_queue_.pop_front();

    assert(queue_bytes_ >= pdu.size());
    queue_bytes_ -= pdu.size();

    return pdu; // ownership transferred to MAC
  }

  // ------------------------------------------------------------
  // bufferOccupancy — BSR value for MAC scheduler
  // ------------------------------------------------------------
  size_t RlcTmTxEntity::bufferOccupancy() const
  {
    // Convention B (TS 36.321 §6.1.3.1):
    //   Return SDU bytes + estimated RLC header overhead.
    //   TM header overhead = 0 bytes.
    return queue_bytes_;
  }

  // ------------------------------------------------------------
  // reestablish — TS 36.322 §5.4
  // ------------------------------------------------------------
  void RlcTmTxEntity::reestablish()
  {
    // Discard all queued SDUs. ByteBuffer destructor returns
    // blocks to the pool automatically.
    tx_queue_.clear();
    queue_bytes_ = 0;
    tm_grant_too_small_ = 0;
  }

  // ============================================================
  // RlcTmRxEntity
  // ============================================================

  RlcTmRxEntity::RlcTmRxEntity(RlcTmConfig cfg, Clock &clk)
      : cfg_(cfg), clk_(clk)
  {
  }

  // ------------------------------------------------------------
  // rxPdu — MAC delivers PDU to TM Rx
  // ------------------------------------------------------------
  Status RlcTmRxEntity::rxPdu(ByteBuffer pdu)
  {
    if (!pdu.valid() || pdu.size() == 0)
    {
      return Status::PARSE_ERROR;
    }

    if (upper_dn_ == nullptr)
    {
      // Not wired yet — discard. Caller should have called
      // set_upper_data_notifier() before driving the stack.
      return Status::PARSE_ERROR;
    }

    // TM Rx: forward raw bytes directly to PDCP (no header to strip).
    upper_dn_->on_new_pdu(std::move(pdu));
    return Status::OK;
  }

  // ------------------------------------------------------------
  // reestablish — TS 36.322 §5.4
  // ------------------------------------------------------------
  void RlcTmRxEntity::reestablish()
  {
    // TM Rx has no state to reset.
    // Empty body kept for interface symmetry with UM/AM.
  }

} // namespace lte
