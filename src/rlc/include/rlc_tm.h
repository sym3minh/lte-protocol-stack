#pragma once
// ============================================================
// rlc_tm.h — RLC Transparent Mode (TM) entities
//
// Spec: TS 36.322 §5.1.1, §6.2.1.2
//
// TM PDU has NO header — the entire PDU = raw SDU bytes.
//
// TM Tx (§5.1.1.1):
//   - Receives SDUs from upper layer (PDCP), queues them.
//   - When MAC pulls (buildPdu), pops one SDU and submits it.
//   - If grant < front SDU size → keep SDU queued, return invalid
//     (Option B: no silent drop; aids debug).
//
// TM Rx (§5.1.1.2):
//   - Receives PDU from MAC, forwards directly to PDCP via
//     upper_dn_->on_new_pdu(). No state, no reassembly.
//
// TM is used for CCCH / PCCH / BCCH logical channels.
// No SN, no HFN, no reordering buffer.
// ============================================================

#include "rlc_entity.h"
#include "buffer_pool.h"
#include "clock.h"
#include "common_types.h"

#include <deque>
#include <cstddef>
#include <cstdint>

namespace lte
{

  // ============================================================
  // RlcTmConfig
  // ============================================================
  struct RlcTmConfig
  {
    LCID_t lcid = 0;
    LogicalChannel lc = LogicalChannel::CCCH;

    // Optional: max queued bytes for back-pressure.
    // 0 = unlimited (default for TM — broadcast channels are
    // small and infrequent, no need for hard cap unless testing).
    size_t max_tx_queue_bytes = 0;
  };

  // ============================================================
  // RlcTmTxEntity — Transmitting TM entity
  // ============================================================
  class RlcTmTxEntity final : public IRlcTxEntity
  {
  public:
    RlcTmTxEntity(RlcTmConfig cfg, BufferPool &pool, Clock &clk);

    // ── rlc_tx_upper_layer_data_sap (via IRlcTxEntity) ──────
    // pdcp_sn: ignored for TM (no SDU discard, no SN tracking).
    void handle_sdu(ByteBuffer sdu, uint32_t pdcp_sn) override; // PDCP -> RLC

    // ── IRlcTxEntity ────────────────────────────────────────

    // Build one TM PDU for MAC.
    //
    // TM: PDU == SDU (no header). Pops front SDU from queue.
    //
    // grant_bytes : transmission opportunity size from MAC.
    // Returns     : valid ByteBuffer if front SDU fits in grant.
    //               invalid (ByteBuffer{}) if queue is empty OR
    //               front SDU exceeds grant — SDU is kept queued.
    //
    // NOTE: TM does NOT segment. If grant < SDU size, the SDU
    // stays in queue. A metric counter (tm_grant_too_small_)
    // records this for observability.
    ByteBuffer buildPdu(size_t grant_bytes) override; // RLC -> MAC

    // Buffer status for MAC scheduling (BSR).
    //
    // Returns: estimated number of bytes RLC would need on the
    //          air interface to transmit ALL currently queued SDUs,
    //          INCLUDING estimated RLC header overhead.
    //          Mirrors the LTE BSR definition (TS 36.321 §6.1.3.1).
    //
    // TM implementation: returns sum of SDU sizes.
    //   Header overhead = 0 (TM has no RLC header).
    //   MAC uses: grant = min(bufferOccupancy(), tb_size_bytes)
    //   and does NOT add header on top (Convention B).
    size_t bufferOccupancy() const override;

    // Re-establishment per TS 36.322 §5.4:
    // Discard all queued SDUs, reset occupancy.
    void reestablish() override;

    // Diagnostics — number of times grant was too small to fit
    // the front SDU. Zero is ideal in production; non-zero means
    // MAC is issuing grants smaller than typical SDU sizes.
    uint64_t grantTooSmallCount() const { return tm_grant_too_small_; }

  private:
    RlcTmConfig cfg_;
    BufferPool &pool_; // stored for symmetry with UM/AM; TM itself does not alloc
    Clock &clk_;       // stored for symmetry with UM/AM; TM has no timer

    std::deque<ByteBuffer> tx_queue_;
    size_t queue_bytes_ = 0; // O(1) occupancy cache
    uint64_t tm_grant_too_small_ = 0;
  };

  // ============================================================
  // RlcTmRxEntity — Receiving TM entity
  // ============================================================
  class RlcTmRxEntity final : public IRlcRxEntity
  {
  public:
    RlcTmRxEntity(RlcTmConfig cfg, Clock &clk);

    // ── IRlcRxEntity ────────────────────────────────────────

    // Receive one TM PDU from MAC. TM Rx simply forwards the
    // buffer to PDCP via upper_dn_->on_new_pdu().
    //
    // Returns Status::OK         on success.
    //         Status::PARSE_ERROR if pdu is invalid/empty or
    //                              upper_dn_ is not wired.
    Status rxPdu(ByteBuffer pdu) override;

    // Re-establishment per TS 36.322 §5.4:
    // TM Rx has no state — empty body, kept for interface symmetry.
    void reestablish() override;

  private:
    RlcTmConfig cfg_;
    Clock &clk_;
  };

} // namespace lte
