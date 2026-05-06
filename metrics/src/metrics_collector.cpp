#include "metrics_collector.h"
#include <cstring>

namespace lte {

MetricsCollector::MetricsCollector() {
    reset();
}

void MetricsCollector::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    tx_packets_   = 0;
    rx_packets_   = 0;
    dropped_      = 0;
    tx_bytes_     = 0;
    rx_bytes_     = 0;
    latency_sum_us_ = 0.0;
    start_ns_     = now_ns();
}

void MetricsCollector::recordTx(size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++tx_packets_;
    tx_bytes_ += bytes;
}

void MetricsCollector::recordRx(size_t   bytes,
                                 uint64_t tx_ts_ns,
                                 uint64_t rx_ts_ns)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++rx_packets_;
    rx_bytes_ += bytes;

    if (rx_ts_ns >= tx_ts_ns) {
        double latency_us = static_cast<double>(rx_ts_ns - tx_ts_ns) / 1000.0;
        latency_sum_us_ += latency_us;
    }
}

void MetricsCollector::recordDrop() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++dropped_;
}

PdcpMetrics MetricsCollector::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);

    PdcpMetrics m;
    m.tx_packets      = tx_packets_;
    m.rx_packets      = rx_packets_;
    m.dropped_packets = dropped_;
    m.tx_bytes        = tx_bytes_;
    m.rx_bytes        = rx_bytes_;

    // Elapsed time
    double elapsed_sec = static_cast<double>(now_ns() - start_ns_) / 1e9;
    m.elapsed_sec = elapsed_sec;

    // Throughput
    if (elapsed_sec > 0.0) {
        m.throughput_tx_bps = static_cast<double>(tx_bytes_ * 8) / elapsed_sec;
        m.throughput_rx_bps = static_cast<double>(rx_bytes_ * 8) / elapsed_sec;
    }

    // Average latency
    if (rx_packets_ > 0) {
        m.avg_latency_us = latency_sum_us_ / static_cast<double>(rx_packets_);
    }

    // Packet loss rate: dropped / (tx_sent + dropped)
    uint64_t total_attempted = tx_packets_ + dropped_;
    if (total_attempted > 0) {
        m.packet_loss_rate =
            static_cast<double>(dropped_) / static_cast<double>(total_attempted);
    }

    return m;
}

uint64_t MetricsCollector::now_ns() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(
            steady_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace lte
