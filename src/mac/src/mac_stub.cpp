// ============================================================
// mac_stub.cpp — MAC stub implementation
//
// Spec ref: TS 36.321 (MAC scheduling, BSR)
// ============================================================

#include "mac_stub.h"

#include <utility>
#include <cassert>

namespace lte
{

// ============================================================
// Constructor
// ============================================================
MacStub::MacStub(MacStubConfig cfg, Clock& clk)
    : cfg_(cfg)
    , clk_(clk)
    , rng_(cfg.rng_seed)
    , dist_(0.0, 1.0)
    , ge_in_bad_state_(false)
{}

// ============================================================
// tick() — drive one TTI
// ============================================================
//
// Follows the 4-step MAC scheduling sequence per TS 36.321:
//
//   Step A: Drain delivery queue.
//           Deliver any PDUs from peer whose delay has expired.
//
//   Step B: BSR (Buffer Status Report).
//           Read bufferOccupancy() from local RLC Tx.
//           (bufferOccupancy already includes RLC header
//            overhead — Convention B.)
//
//   Step C: Scheduler.
//           grant = min(bo, tb_size_bytes).
//           MAC does NOT add header bytes — that is RLC's job.
//
//   Step D: Tx opportunity.
//           Call buildPdu(grant) on local RLC Tx.
//           Apply Gilbert-Elliott loss model.
//           If not dropped, enqueue to peer (with delay).
// ============================================================
void MacStub::tick()
{
    // ── Step A: deliver matured PDUs incoming from peer ──────
    drain_delivery_queue();

    // ── Step B: BSR ──────────────────────────────────────────
    if (!rlc_tx_ || !peer_)
    {
        return; // not yet wired
    }

    const size_t bo = rlc_tx_->bufferOccupancy();
    if (bo == 0)
    {
        return; // nothing to transmit
    }

    // ── Step C: Scheduler ────────────────────────────────────
    // Convention B: bo already includes RLC header overhead.
    // Do NOT add extra bytes here.
    const size_t grant = (bo < cfg_.tb_size_bytes) ? bo : cfg_.tb_size_bytes;

    // ── Step D: Tx opportunity ───────────────────────────────
    ByteBuffer pdu = rlc_tx_->buildPdu(grant);
    if (!pdu.valid() || pdu.size() == 0)
    {
        // RLC declined (e.g. grant too small for front SDU in TM).
        return;
    }

    ++pdus_sent_;
    bytes_sent_ += pdu.size();

    // Apply Gilbert-Elliott loss model.
    if (should_drop())
    {
        ++pdus_dropped_;
        // pdu goes out of scope → ByteBuffer destructor returns
        // block to pool automatically. No explicit free needed.
        return;
    }

    // Deliver PDU to peer (with optional delay).
    peer_->enqueue_from_peer(std::move(pdu));
}

// ============================================================
// enqueue_from_peer — called by the paired MacStub's tick()
// ============================================================
void MacStub::enqueue_from_peer(ByteBuffer pdu)
{
    if (cfg_.delay_tti == 0)
    {
        // Immediate delivery — bypass the delay queue.
        if (rlc_rx_)
        {
            ++pdus_delivered_;
            rlc_rx_->rxPdu(std::move(pdu));
        }
        return;
    }

    // Compute absolute delivery time.
    // delay_tti TTIs from now, each TTI = tti_ms milliseconds.
    const uint64_t delay_ns = static_cast<uint64_t>(cfg_.delay_tti)
                            * static_cast<uint64_t>(cfg_.tti_ms)
                            * 1'000'000ULL; // ms → ns

    Pending p;
    p.deliver_at_ns = clk_.now_ns() + delay_ns;
    p.pdu           = std::move(pdu);
    delivery_queue_.push_back(std::move(p));
}

// ============================================================
// drain_delivery_queue — deliver matured PDUs to local RLC Rx
// ============================================================
void MacStub::drain_delivery_queue()
{
    const uint64_t now = clk_.now_ns();

    while (!delivery_queue_.empty()
        && delivery_queue_.front().deliver_at_ns <= now)
    {
        Pending p = std::move(delivery_queue_.front());
        delivery_queue_.pop_front();

        if (rlc_rx_)
        {
            ++pdus_delivered_;
            rlc_rx_->rxPdu(std::move(p.pdu));
        }
        // If rlc_rx_ is null, pdu is discarded (block returns to pool).
    }
}

// ============================================================
// should_drop — Gilbert-Elliott loss model
// ============================================================
//
// Two-state Markov chain (per-PDU transition):
//
//   GOOD ─(p_good_to_bad)─→ BAD
//   BAD  ─(p_bad_to_good)─→ GOOD
//
// In GOOD state: drop with prob loss_prob_in_good (typically ~0)
// In BAD  state: drop with prob loss_prob_in_bad  (typically ~1)
//
// State transition happens AFTER the drop decision so that the
// first PDU entering a burst has BAD-state loss applied to it.
//
// Bernoulli equivalence:
//   p_good_to_bad = 0, p_bad_to_good = 1, loss_in_good = rate
//   → single state, i.i.d. drops
// ============================================================
bool MacStub::should_drop()
{
    const GilbertElliottConfig& ge = cfg_.loss;

    // No loss configured → fast path
    if (ge.loss_prob_in_good == 0.0 && ge.loss_prob_in_bad == 0.0)
    {
        return false;
    }

    // Determine drop based on current state
    const double loss_prob = ge_in_bad_state_
                           ? ge.loss_prob_in_bad
                           : ge.loss_prob_in_good;

    const bool drop = (dist_(rng_) < loss_prob);

    // Markov state transition for NEXT PDU
    if (ge_in_bad_state_)
    {
        // BAD → GOOD with probability p_bad_to_good
        if (dist_(rng_) < ge.p_bad_to_good)
        {
            ge_in_bad_state_ = false;
        }
    }
    else
    {
        // GOOD → BAD with probability p_good_to_bad
        if (dist_(rng_) < ge.p_good_to_bad)
        {
            ge_in_bad_state_ = true;
        }
    }

    return drop;
}

} // namespace lte
