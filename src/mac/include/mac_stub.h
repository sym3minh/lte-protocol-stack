#pragma once
// ============================================================
// mac_stub.h — Simplified MAC layer stub for testing
//
// Spec ref: TS 36.321 (MAC scheduling, BSR)
//
// Models a "transmission opportunity" between two peer RLC
// entities, implementing a pull-driven scheduling model that
// mirrors real eNB/UE MAC behaviour (srsRAN, OAI style):
//
//   RlcTx ─ buildPdu(grant) ─→ MacStub(A) ─link─ MacStub(B) ─ rxPdu ─→ RlcRx
//
// Features modelled per TTI:
//   - Grant size (TB size) — limits bytes pulled from RLC per tick
//   - Loss — Gilbert-Elliott two-state Markov channel
//   - Delay — fixed N-TTI propagation delay
//
// ── Gilbert-Elliott Loss Model ───────────────────────────────
//
// Two-state Markov chain:
//
//   GOOD ──(p_g_to_b)──→ BAD
//   BAD  ──(p_b_to_g)──→ GOOD
//
//   loss_prob_in_good : P(drop | state == GOOD)   (typically low, e.g. 0.01)
//   loss_prob_in_bad  : P(drop | state == BAD)    (typically high, e.g. 0.9)
//   p_g_to_b          : P(GOOD → BAD per PDU)     (burst entry probability)
//   p_b_to_g          : P(BAD → GOOD per PDU)     (burst recovery probability)
//
// Steady-state loss rate = (p_g_to_b * loss_bad + p_b_to_g * loss_good)
//                          / (p_g_to_b + p_b_to_g)   [approx]
//
// To emulate simple Bernoulli loss: set p_g_to_b=0, p_b_to_g=1,
//   loss_prob_in_good=<desired_rate>, loss_prob_in_bad=0.
//
// ── Convention B (bufferOccupancy) ───────────────────────────
//
// MAC reads rlc_tx_->bufferOccupancy() which already includes
// estimated RLC header overhead (Convention B, TS 36.321 §6.1.3.1).
// MAC does NOT add extra header bytes — that is RLC's responsibility.
//
//   grant = min(bufferOccupancy(), tb_size_bytes)
//
// ── Pull-driven tick model ────────────────────────────────────
//
// MAC scheduler is the master clock. The test harness:
//   1. Advances MockClock by 1 ms.
//   2. Calls tick() on both MacStub instances.
//
// tick() follows the 4-step MAC scheduling sequence:
//   A. Drain delivery queue (deliver delayed PDUs that have matured)
//   B. BSR — read bufferOccupancy() from local RLC Tx
//   C. Scheduler — compute grant = min(bo, tb_size_bytes)
//   D. Tx opportunity — buildPdu(grant) → loss model → push to peer
// ============================================================

#include "rlc_entity.h"
#include "clock.h"
#include "byte_buffer.h"

#include <deque>
#include <random>
#include <cstdint>
#include <cstddef>

namespace lte
{

// ============================================================
// GilbertElliottConfig — parameters for the G-E channel model
// ============================================================
struct GilbertElliottConfig
{
    // Transition probabilities (per-PDU)
    double p_good_to_bad  = 0.0;   // P(GOOD→BAD); 0.0 = never enter bad burst
    double p_bad_to_good  = 1.0;   // P(BAD→GOOD); 1.0 = immediately recover

    // Loss probabilities per state
    double loss_prob_in_good = 0.0; // typically near 0
    double loss_prob_in_bad  = 0.0; // typically near 1 during burst

    // --- Convenience factory methods ---

    // Build a pure Bernoulli loss channel (single state, no burst).
    // Equivalent to G-E with no state transitions.
    static GilbertElliottConfig bernoulli(double loss_rate)
    {
        GilbertElliottConfig c;
        c.p_good_to_bad      = 0.0;
        c.p_bad_to_good      = 1.0;
        c.loss_prob_in_good  = loss_rate;
        c.loss_prob_in_bad   = 0.0;
        return c;
    }

    // Build a burst-loss channel.
    //   avg_burst_len   : mean consecutive PDUs lost per burst
    //   p_burst_entry   : probability of entering a burst each PDU
    //   loss_in_burst   : loss probability while in BAD state (default 1.0)
    static GilbertElliottConfig burst(double p_burst_entry,
                                      double avg_burst_len,
                                      double loss_in_burst = 1.0)
    {
        GilbertElliottConfig c;
        c.p_good_to_bad      = p_burst_entry;
        c.p_bad_to_good      = (avg_burst_len > 1.0)
                                   ? (1.0 / avg_burst_len)
                                   : 1.0;
        c.loss_prob_in_good  = 0.0;
        c.loss_prob_in_bad   = loss_in_burst;
        return c;
    }

    // No loss at all (default-constructed = lossless).
    static GilbertElliottConfig lossless()
    {
        return GilbertElliottConfig{};
    }
};

// ============================================================
// MacStubConfig
// ============================================================
struct MacStubConfig
{
    size_t                tb_size_bytes = 1024;      // grant per TTI (bytes)
    uint32_t              tti_ms        = 1;         // LTE = 1 ms per TTI
    GilbertElliottConfig  loss          = GilbertElliottConfig::lossless();
    uint32_t              delay_tti     = 0;         // 0 = immediate delivery
    uint32_t              rng_seed      = 0xDEADBEEF; // deterministic for tests
};

// ============================================================
// MacStub
// ============================================================
class MacStub
{
public:
    MacStub(MacStubConfig cfg, Clock& clk);

    // ── Wiring ──────────────────────────────────────────────
    // Attach local RLC entities. Caller retains ownership.
    void attach_rlc_tx(IRlcTxEntity* tx) { rlc_tx_ = tx; }
    void attach_rlc_rx(IRlcRxEntity* rx) { rlc_rx_ = rx; }

    // Pair two MacStubs to form a bidirectional link.
    // After pairing:
    //   local.tick()  → pulls from local.rlc_tx_, delivers to peer.rlc_rx_
    //   peer.tick()   → pulls from peer.rlc_tx_,  delivers to local.rlc_rx_
    void pair(MacStub& peer)
    {
        peer_       = &peer;
        peer.peer_  = this;
    }

    // ── Scheduling ──────────────────────────────────────────
    // Drive one TTI. Caller must advance Clock before calling.
    // Sequence: drain_delivery_queue → BSR → grant → buildPdu
    //           → loss → enqueue_from_peer
    void tick();

    // ── Metrics ─────────────────────────────────────────────
    uint64_t pdus_sent()      const { return pdus_sent_; }
    uint64_t pdus_dropped()   const { return pdus_dropped_; }
    uint64_t pdus_delivered() const { return pdus_delivered_; }
    uint64_t bytes_sent()     const { return bytes_sent_; }

    // Current G-E channel state (for test inspection)
    bool in_bad_state() const { return ge_in_bad_state_; }

    void reset_metrics()
    {
        pdus_sent_      = 0;
        pdus_dropped_   = 0;
        pdus_delivered_ = 0;
        bytes_sent_     = 0;
    }

private:
    // Pending PDU waiting in the delay queue.
    struct Pending
    {
        uint64_t   deliver_at_ns; // absolute delivery time
        ByteBuffer pdu;
    };

    // Called by peer's tick() to enqueue an incoming PDU.
    // Applies delay or delivers immediately to local rlc_rx_.
    void enqueue_from_peer(ByteBuffer pdu);

    // Drain pending PDUs whose deliver_at_ns <= now.
    void drain_delivery_queue();

    // Apply Gilbert-Elliott loss model for one PDU.
    // Updates ge_in_bad_state_ via Markov transition.
    // Returns true if the PDU should be DROPPED.
    bool should_drop();

    MacStubConfig  cfg_;
    Clock&         clk_;
    IRlcTxEntity*  rlc_tx_ = nullptr;
    IRlcRxEntity*  rlc_rx_ = nullptr;
    MacStub*       peer_   = nullptr;

    // Delay queue — PDUs received from peer, not yet delivered.
    std::deque<Pending> delivery_queue_;

    // Gilbert-Elliott RNG state
    std::mt19937                          rng_;
    std::uniform_real_distribution<double> dist_; // [0.0, 1.0)
    bool ge_in_bad_state_ = false;                // starts in GOOD state

    // Metrics
    uint64_t pdus_sent_      = 0;
    uint64_t pdus_dropped_   = 0;
    uint64_t pdus_delivered_ = 0;
    uint64_t bytes_sent_     = 0;
};

} // namespace lte
