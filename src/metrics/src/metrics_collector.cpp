#include "metrics_collector.h"

namespace lte
{

  // ============================================================
  // Constructors
  // ============================================================

  MetricsCollector::MetricsCollector()
      : clock_(RealClock::instance())
  {
    reset();
  }

  MetricsCollector::MetricsCollector(Clock &clock)
      : clock_(clock)
  {
    reset();
  }

  // ============================================================
  // reset
  // ============================================================
  void MetricsCollector::reset()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tx_packets_ = 0;
    rx_packets_ = 0;
    dropped_ = 0;
    tx_bytes_ = 0;
    rx_bytes_ = 0;
    latency_sum_us_ = 0.0;
    start_ns_ = clock_.now_ns(); // snapshot start time from injected clock
  }

  // ============================================================
  // recordTx
  // ============================================================
  void MetricsCollector::recordTx(size_t bytes)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++tx_packets_;
    tx_bytes_ += bytes;
  }

  // ============================================================
  // recordRx
  // ============================================================
  void MetricsCollector::recordRx(size_t bytes,
                                  uint64_t tx_ts_ns,
                                  uint64_t rx_ts_ns)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++rx_packets_;
    rx_bytes_ += bytes;

    if (rx_ts_ns >= tx_ts_ns)
    {
      double latency_us = static_cast<double>(rx_ts_ns - tx_ts_ns) / 1000.0;
      latency_sum_us_ += latency_us;
    }
  }

  // ============================================================
  // recordDrop
  // ============================================================
  void MetricsCollector::recordDrop()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++dropped_;
  }

  // ============================================================
  // snapshot
  // ============================================================
  PdcpMetrics MetricsCollector::snapshot() const
  {
    std::lock_guard<std::mutex> lock(mutex_);

    PdcpMetrics m;
    m.tx_packets = tx_packets_;
    m.rx_packets = rx_packets_;
    m.dropped_packets = dropped_;
    m.tx_bytes = tx_bytes_;
    m.rx_bytes = rx_bytes_;

    // Elapsed time via injected clock (works correctly for MockClock too)
    const uint64_t now = clock_.now_ns();
    const double elapsed_sec =
        static_cast<double>(now - start_ns_) / 1e9;
    m.elapsed_sec = elapsed_sec;

    if (elapsed_sec > 0.0)
    {
      m.throughput_tx_bps =
          static_cast<double>(tx_bytes_ * 8) / elapsed_sec;
      m.throughput_rx_bps =
          static_cast<double>(rx_bytes_ * 8) / elapsed_sec;
    }

    if (rx_packets_ > 0)
    {
      m.avg_latency_us =
          latency_sum_us_ / static_cast<double>(rx_packets_);
    }

    const uint64_t total_attempted = tx_packets_ + dropped_;
    if (total_attempted > 0)
    {
      m.packet_loss_rate =
          static_cast<double>(dropped_) /
          static_cast<double>(total_attempted);
    }

    return m;
  }

  // ============================================================
  // now_ns — non-static delegate to injected clock
  // ============================================================
  uint64_t MetricsCollector::now_ns() const
  {
    return clock_.now_ns();
  }

} // namespace lte
