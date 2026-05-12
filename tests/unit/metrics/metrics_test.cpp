#include <gtest/gtest.h>
#include "metrics_collector.h"
#include "clock.h"

#include <thread>
#include <chrono>

using namespace lte;

// ============================================================
// Basic counter tests (unchanged behaviour)
// ============================================================

TEST(MetricsTest, InitialSnapshotIsZero)
{
  MetricsCollector mc;
  auto m = mc.snapshot();

  EXPECT_EQ(m.tx_packets, 0u);
  EXPECT_EQ(m.rx_packets, 0u);
  EXPECT_EQ(m.dropped_packets, 0u);
  EXPECT_DOUBLE_EQ(m.packet_loss_rate, 0.0);
}

TEST(MetricsTest, TxCountsAccumulate)
{
  MetricsCollector mc;
  mc.recordTx(100);
  mc.recordTx(200);
  mc.recordTx(300);

  auto m = mc.snapshot();
  EXPECT_EQ(m.tx_packets, 3u);
  EXPECT_EQ(m.tx_bytes, 600u);
}

TEST(MetricsTest, RxCountsAccumulate)
{
  MetricsCollector mc;
  // Use mc.now_ns() instead of the removed MetricsCollector::now_ns() static
  uint64_t t0 = mc.now_ns();
  mc.recordRx(150, t0, t0 + 1000); // 1 µs latency
  mc.recordRx(150, t0, t0 + 3000); // 3 µs latency

  auto m = mc.snapshot();
  EXPECT_EQ(m.rx_packets, 2u);
  EXPECT_EQ(m.rx_bytes, 300u);
  EXPECT_NEAR(m.avg_latency_us, 2.0, 0.5);
}

TEST(MetricsTest, PacketLossRateCalculated)
{
  MetricsCollector mc;
  for (int i = 0; i < 9; ++i)
    mc.recordTx(100);
  mc.recordDrop(); // 1 drop out of 10 attempts = 10%

  auto m = mc.snapshot();
  EXPECT_EQ(m.dropped_packets, 1u);
  EXPECT_NEAR(m.packet_loss_rate, 0.10, 0.01);
}

TEST(MetricsTest, ResetClearsAllCounters)
{
  MetricsCollector mc;
  mc.recordTx(100);
  mc.recordDrop();

  mc.reset();
  auto m = mc.snapshot();

  EXPECT_EQ(m.tx_packets, 0u);
  EXPECT_EQ(m.dropped_packets, 0u);
}

// ============================================================
// MockClock injection tests
// ============================================================

// Verify that MetricsCollector works correctly with MockClock:
// elapsed_sec should reflect the mock time, not real wall time.
TEST(MetricsTest, MockClockControlsElapsedTime)
{
  MockClock clk;
  MetricsCollector mc(clk);

  mc.recordTx(1000); // 8000 bits

  // No real sleep — advance mock clock by 1 second
  clk.advance_ms(1000);

  auto m = mc.snapshot();
  EXPECT_NEAR(m.elapsed_sec, 1.0, 0.001);
  EXPECT_NEAR(m.throughput_tx_bps, 8000.0, 1.0);
}

TEST(MetricsTest, MockClockLatencyMeasurement)
{
  MockClock clk;
  MetricsCollector mc(clk);

  const uint64_t tx_time = clk.now_ns(); // t = 0

  // Simulate 200 µs latency
  clk.advance_us(200);
  const uint64_t rx_time = clk.now_ns(); // t = 200'000 ns

  mc.recordRx(500, tx_time, rx_time);

  auto m = mc.snapshot();
  EXPECT_NEAR(m.avg_latency_us, 200.0, 0.01);
}

TEST(MetricsTest, MockClockResetRestartsWindow)
{
  MockClock clk;
  MetricsCollector mc(clk);

  clk.advance_ms(500);
  mc.recordTx(100);
  mc.reset(); // start_ns_ re-snapshots at t = 500 ms

  clk.advance_ms(100); // now elapsed = 100 ms, not 600 ms
  auto m = mc.snapshot();
  EXPECT_NEAR(m.elapsed_sec, 0.1, 0.001);
}
