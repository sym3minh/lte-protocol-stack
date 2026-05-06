#pragma once
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <chrono>

namespace lte {

// ============================================================
// MetricsCollector — per-entity runtime statistics
//
// Thread-safe snapshot; all internal counters are updated
// on every Tx/Rx event.  Call snapshot() to read a
// point-in-time copy without stopping traffic.
// ============================================================

struct PdcpMetrics {
    // Counters
    uint64_t tx_packets    = 0;
    uint64_t rx_packets    = 0;
    uint64_t dropped_packets = 0;
    uint64_t tx_bytes      = 0;
    uint64_t rx_bytes      = 0;

    // Derived (computed in snapshot())
    double throughput_tx_bps  = 0.0;  // bits/s over measurement window
    double throughput_rx_bps  = 0.0;
    double avg_latency_us     = 0.0;  // microseconds, Tx→Rx
    double packet_loss_rate   = 0.0;  // 0.0 – 1.0

    // Elapsed measurement time
    double elapsed_sec        = 0.0;
};

class MetricsCollector {
public:
    MetricsCollector();

    // Call when a PDU leaves the Tx side
    void recordTx(size_t bytes);

    // Call when a PDU is delivered by the Rx side
    // tx_timestamp_ns : value of tx_timestamp_ns in the PdcpPdu
    // rx_timestamp_ns : current time (use now_ns())
    void recordRx(size_t bytes,
                  uint64_t tx_timestamp_ns,
                  uint64_t rx_timestamp_ns);

    // Call when a PDU is dropped (SN out of window, pool exhausted…)
    void recordDrop();

    // Return a consistent snapshot (takes mutex once)
    PdcpMetrics snapshot() const;

    // Reset all counters and restart the measurement clock
    void reset();

    // Utility: current time in nanoseconds (monotonic clock)
    static uint64_t now_ns();

private:
    mutable std::mutex mutex_;

    // Raw counters
    uint64_t tx_packets_    = 0;
    uint64_t rx_packets_    = 0;
    uint64_t dropped_       = 0;
    uint64_t tx_bytes_      = 0;
    uint64_t rx_bytes_      = 0;

    // Latency accumulator
    double   latency_sum_us_ = 0.0;

    // Clock
    uint64_t start_ns_       = 0;
};

} // namespace lte
