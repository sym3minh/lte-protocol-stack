#pragma once
// ============================================================
// metrics_collector.h — per-entity runtime statistics
//
// Change from previous version:
//   static now_ns() removed.  Time is now read through an
//   injected Clock& (default: RealClock::instance()).
//
//   Why: RLC entities own timers (t-Reordering, t-PollRetransmit,
//   t-StatusProhibit).  If MetricsCollector still called
//   std::chrono directly, timer-driven tests would depend on real
//   wall time → non-deterministic.
//
//   Migration guide for callers:
//     Before: MetricsCollector::now_ns()
//     After:  metrics_instance.now_ns()
//             OR RealClock::instance().now_ns()
//             OR your_mock_clock.now_ns()
// ============================================================

#include "clock.h"

#include <cstdint>
#include <cstddef>
#include <mutex>

namespace lte
{

  // ============================================================
  // PdcpMetrics — snapshot struct (unchanged)
  // ============================================================
  struct PdcpMetrics
  {
    // Counters
    uint64_t tx_packets = 0;
    uint64_t rx_packets = 0;
    uint64_t dropped_packets = 0;
    uint64_t tx_bytes = 0;
    uint64_t rx_bytes = 0;

    // Derived (computed in snapshot())
    double throughput_tx_bps = 0.0; // bits/s over measurement window
    double throughput_rx_bps = 0.0;
    double avg_latency_us = 0.0;   // microseconds, Tx → Rx
    double packet_loss_rate = 0.0; // 0.0 – 1.0

    // Elapsed measurement time
    double elapsed_sec = 0.0;
  };

  // ============================================================
  // MetricsCollector
  // ============================================================
  class MetricsCollector
  {
  public:
    // Default constructor: uses RealClock::instance().
    // Existing code that creates MetricsCollector() needs no changes.
    MetricsCollector();

    // Injected clock constructor: use in tests or timer-driven entities.
    // Example:
    //   MockClock mock_clk;
    //   MetricsCollector mc(mock_clk);
    //   mock_clk.advance_ms(10);
    //   auto snap = mc.snapshot();  // elapsed_sec == 0.01
    explicit MetricsCollector(Clock &clock);

    // Non-copyable — owns mutex and accumulator state
    MetricsCollector(const MetricsCollector &) = delete;
    MetricsCollector &operator=(const MetricsCollector &) = delete;

    // ----------------------------------------------------------
    // Recording events
    // ----------------------------------------------------------

    // Call when a PDU leaves the Tx side
    void recordTx(size_t bytes);

    // Call when a PDU is delivered by the Rx side.
    //   tx_timestamp_ns : value of tx_timestamp_ns in the PDU (from clock_.now_ns() at Tx time)
    //   rx_timestamp_ns : current time, obtained via clock_.now_ns() at Rx site
    void recordRx(size_t bytes,
                  uint64_t tx_timestamp_ns,
                  uint64_t rx_timestamp_ns);

    // Call when a PDU is dropped (SN out of window, pool exhausted…)
    void recordDrop();

    // ----------------------------------------------------------
    // Read-out
    // ----------------------------------------------------------

    // Return a consistent snapshot (takes mutex once)
    PdcpMetrics snapshot() const;

    // Reset all counters and restart the measurement clock
    void reset();

    // ----------------------------------------------------------
    // Time utility — delegates to injected clock.
    //
    // Use this wherever you previously called MetricsCollector::now_ns().
    // For code that doesn't hold a MetricsCollector reference, call
    // RealClock::instance().now_ns() directly (production) or
    // your_mock_clock.now_ns() (test).
    // ----------------------------------------------------------
    uint64_t now_ns() const;

  private:
    Clock &clock_; // reference to injected clock (never null)

    mutable std::mutex mutex_;

    // Raw counters
    uint64_t tx_packets_ = 0;
    uint64_t rx_packets_ = 0;
    uint64_t dropped_ = 0;
    uint64_t tx_bytes_ = 0;
    uint64_t rx_bytes_ = 0;

    // Latency accumulator
    double latency_sum_us_ = 0.0;

    // Measurement window start (absolute ns from the injected clock)
    uint64_t start_ns_ = 0;
  };

} // namespace lte
