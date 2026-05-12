#pragma once
// ============================================================
// clock.h — Monotonic clock abstraction
//
// Ref: RLC_ARCHITECTURE_DECISIONS.md §2
//
// Design rule (ENFORCED):
//   All time in the LTE stack must go through Clock.
//   std::chrono::steady_clock::now() is NEVER called directly
//   in production stack code — only inside RealClock::now_ns().
//
// Rationale:
//   If RLC / PDCP code calls std::chrono directly, timer-driven
//   unit tests (t-Reordering, t-PollRetransmit, discardTimer)
//   must use real wall-clock sleep → lose determinism and speed.
//   With MockClock, tests fast-forward 100 ms instantly via
//   clk.advance_ms(100) without any sleep.
//
// Naming:
//   We use `Clock` (no I- prefix) for the abstract base, matching
//   the codebase convention (BufferPool, ByteBuffer, …).
// ============================================================

#include <cstdint>
#include <chrono>

namespace lte {

// ============================================================
// Clock — pure interface
// ============================================================
class Clock {
public:
    virtual ~Clock() = default;

    // Returns current time in nanoseconds (monotonic).
    virtual uint64_t now_ns() const = 0;
};

// ============================================================
// RealClock — wraps std::chrono::steady_clock
//
// USAGE POLICY (IMPORTANT):
//   RealClock::instance() exists ONLY as a fallback for components
//   that pre-date the Clock abstraction (currently only
//   MetricsCollector — see RLC_ARCHITECTURE_DECISIONS.md §7).
//
//   New code (RLC entities, PDCP timer-driven flows, RadioBearer)
//   MUST receive a Clock& through their constructor. Do NOT call
//   RealClock::instance() inside any new entity — doing so makes
//   that code untestable with MockClock and defeats the purpose
//   of the abstraction.
// ============================================================
class RealClock final : public Clock {
public:
    // Fallback accessor — see USAGE POLICY above.
    // Prefer constructor injection of Clock& everywhere else.
    static RealClock& instance();

    uint64_t now_ns() const override;

    // Non-copyable, non-movable — singleton
    RealClock(const RealClock&)            = delete;
    RealClock& operator=(const RealClock&) = delete;

private:
    RealClock() = default;
};

// ============================================================
// MockClock — deterministic clock for unit tests
//
// Starts at t = 0 ns.  Tests control time explicitly:
//   MockClock clk;
//   clk.advance_ms(100);   // "jumps" 100 ms with no sleep
//   assert(clk.now_ns() == 100'000'000ULL);
//
// Thread-safety: NOT thread-safe — single-threaded test use only.
// In Phase 1 all entities are single-threaded, so this is fine.
// ============================================================
class MockClock final : public Clock {
public:
    MockClock() = default;
    explicit MockClock(uint64_t initial_ns) : current_ns_(initial_ns) {}

    uint64_t now_ns() const override { return current_ns_; }

    // Advance by a delta
    void advance_ns(uint64_t delta_ns) { current_ns_ += delta_ns; }
    void advance_us(uint64_t delta_us) { current_ns_ += delta_us * 1'000ULL; }
    void advance_ms(uint64_t delta_ms) { current_ns_ += delta_ms * 1'000'000ULL; }

    // Set to an absolute value
    void set_ns(uint64_t abs_ns) { current_ns_ = abs_ns; }

    // Reset to zero
    void reset() { current_ns_ = 0; }

private:
    uint64_t current_ns_ = 0;
};

} // namespace lte
