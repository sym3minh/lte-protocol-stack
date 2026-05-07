#include <gtest/gtest.h>
#include "metrics_collector.h"
#include <thread>
#include <chrono>

using namespace lte;

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
  uint64_t t0 = MetricsCollector::now_ns();
  mc.recordRx(150, t0, t0 + 1000); // 1 microsecond latency
  mc.recordRx(150, t0, t0 + 3000); // 3 microseconds latency

  auto m = mc.snapshot();
  EXPECT_EQ(m.rx_packets, 2u);
  EXPECT_EQ(m.rx_bytes, 300u);

  // Average latency = (1 + 3) / 2 = 2 µs
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

TEST(MetricsTest, ThroughputIsPositiveAfterDelay)
{
  MetricsCollector mc;
  mc.recordTx(1500); // one Ethernet-sized frame

  // Small sleep so elapsed_sec > 0
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  auto m = mc.snapshot();
  EXPECT_GT(m.throughput_tx_bps, 0.0);
  EXPECT_GT(m.elapsed_sec, 0.0);
}

TEST(MetricsTest, NowNsIsMonotonicallyIncreasing)
{
  uint64_t t1 = MetricsCollector::now_ns();
  std::this_thread::sleep_for(std::chrono::microseconds(100));
  uint64_t t2 = MetricsCollector::now_ns();
  EXPECT_GT(t2, t1);
}
