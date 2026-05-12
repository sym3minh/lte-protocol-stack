#include <gtest/gtest.h>
#include "clock.h"

#include <thread>
#include <chrono>

using namespace lte;

// ============================================================
// MockClock tests
// ============================================================

TEST(MockClockTest, StartsAtZero)
{
  MockClock clk;
  EXPECT_EQ(clk.now_ns(), 0ULL);
}

TEST(MockClockTest, ConstructWithInitialValue)
{
  MockClock clk(12'345ULL);
  EXPECT_EQ(clk.now_ns(), 12'345ULL);
}

TEST(MockClockTest, AdvanceNs)
{
  MockClock clk;
  clk.advance_ns(500);
  EXPECT_EQ(clk.now_ns(), 500ULL);
  clk.advance_ns(300);
  EXPECT_EQ(clk.now_ns(), 800ULL);
}

TEST(MockClockTest, AdvanceUs)
{
  MockClock clk;
  clk.advance_us(1);
  EXPECT_EQ(clk.now_ns(), 1'000ULL);
  clk.advance_us(9);
  EXPECT_EQ(clk.now_ns(), 10'000ULL);
}

TEST(MockClockTest, AdvanceMs)
{
  MockClock clk;
  clk.advance_ms(100);
  EXPECT_EQ(clk.now_ns(), 100'000'000ULL);
}

TEST(MockClockTest, SetNs)
{
  MockClock clk;
  clk.advance_ms(500);
  clk.set_ns(42ULL);
  EXPECT_EQ(clk.now_ns(), 42ULL);
}

TEST(MockClockTest, Reset)
{
  MockClock clk;
  clk.advance_ms(100);
  clk.reset();
  EXPECT_EQ(clk.now_ns(), 0ULL);
}

// ============================================================
// MockClock used via Clock& — polymorphic interface test
// ============================================================

// Helper that reads time via the interface (simulates what stack code does)
static uint64_t read_time_via_interface(const Clock& clk)
{
  return clk.now_ns();
}

TEST(MockClockTest, PolymorphicInterface)
{
  MockClock clk;
  clk.advance_ms(50);
  EXPECT_EQ(read_time_via_interface(clk), 50'000'000ULL);
}

// Verify that a timer-like assertion works deterministically:
// "t-Reordering fires after 100 ms" — no sleep needed.
TEST(MockClockTest, DeterministicTimerSimulation)
{
  MockClock clk;
  const uint64_t start   = clk.now_ns();
  const uint64_t timeout = 100'000'000ULL; // 100 ms in ns

  // Simulate time passing in steps without any sleep
  clk.advance_ms(50);
  EXPECT_LT(clk.now_ns() - start, timeout); // not yet fired

  clk.advance_ms(50);
  EXPECT_GE(clk.now_ns() - start, timeout); // fired
}

// ============================================================
// RealClock tests
// ============================================================

TEST(RealClockTest, SingletonIsSameInstance)
{
  // Both references should be the same object
  Clock& a = RealClock::instance();
  Clock& b = RealClock::instance();
  EXPECT_EQ(&a, &b);
}

TEST(RealClockTest, NowNsIsMonotonicallyIncreasing)
{
  RealClock& clk = RealClock::instance();
  uint64_t t1 = clk.now_ns();
  std::this_thread::sleep_for(std::chrono::microseconds(100));
  uint64_t t2 = clk.now_ns();
  EXPECT_GT(t2, t1);
}
