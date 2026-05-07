#include <gtest/gtest.h>
#include "pdcp_entity.h"
#include <string>
#include <vector>

using namespace lte;

// ============================================================
// Loopback fixture — one PdcpEntity acts as both Tx and Rx.
// This simulates the path:
//   "Minh" → txSdu() → [PDU bytes] → rxPdu() → "Minh"
// ============================================================
class PdcpLoopbackTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    pool = std::make_unique<BufferPool>(2048, 32);
    entity = std::make_unique<PdcpEntity>(
        /*lcid=*/1, BearerType::DRB, RlcMode::AM, *pool);
  }

  std::unique_ptr<BufferPool> pool;
  std::unique_ptr<PdcpEntity> entity;
};

// ------------------------------------------------------------
// The flagship test: send "Minh", receive "Minh"
// ------------------------------------------------------------
TEST_F(PdcpLoopbackTest, SimpleStringMinh)
{
  const std::string input = "Minh";

  // Tx: wrap "Minh" into a PDCP PDU
  Status s = entity->txSdu(
      reinterpret_cast<const uint8_t *>(input.data()),
      input.size());
  ASSERT_EQ(s, Status::OK) << "txSdu failed";

  // The PDU is stored in lastTxPdu() for test access
  const auto &pdu_bytes = entity->lastTxPdu();
  ASSERT_GT(pdu_bytes.size(), 0u) << "PDU is empty";

  // Rx: feed the raw PDU back into the same entity
  s = entity->rxPdu(pdu_bytes.data(), pdu_bytes.size());
  ASSERT_EQ(s, Status::OK) << "rxPdu failed";

  // Check what came out the other side
  const auto &delivered = entity->lastDeliveredSdu();
  ASSERT_EQ(delivered.size(), input.size())
      << "Delivered SDU length mismatch";

  std::string output(delivered.begin(), delivered.end());
  EXPECT_EQ(output, input)
      << "Expected \"" << input << "\", got \"" << output << "\"";
}

// ------------------------------------------------------------
// Same test with UTF-8 Vietnamese characters
// ------------------------------------------------------------
TEST_F(PdcpLoopbackTest, VietnameseString)
{
  const std::string input = "Xin chào thế giới";

  ASSERT_EQ(entity->txSdu(
                reinterpret_cast<const uint8_t *>(input.data()), input.size()),
            Status::OK);

  ASSERT_EQ(entity->rxPdu(
                entity->lastTxPdu().data(), entity->lastTxPdu().size()),
            Status::OK);

  const auto &delivered = entity->lastDeliveredSdu();
  std::string output(delivered.begin(), delivered.end());
  EXPECT_EQ(output, input);
}

// ------------------------------------------------------------
// Binary payload (e.g. simulated IP packet header bytes)
// ------------------------------------------------------------
TEST_F(PdcpLoopbackTest, BinaryPayload)
{
  const std::vector<uint8_t> input = {
      0x45, 0x00, 0x00, 0x28, // IP header (version, IHL, DSCP, length)
      0xAB, 0xCD, 0x40, 0x00, // identification, flags, frag offset
      0x40, 0x11, 0x00, 0x00  // TTL, protocol=UDP, checksum
  };

  ASSERT_EQ(entity->txSdu(input.data(), input.size()), Status::OK);
  ASSERT_EQ(entity->rxPdu(
                entity->lastTxPdu().data(), entity->lastTxPdu().size()),
            Status::OK);

  const auto &delivered = entity->lastDeliveredSdu();
  ASSERT_EQ(delivered.size(), input.size());
  EXPECT_EQ(delivered, input);
}

// ------------------------------------------------------------
// Empty / invalid inputs must be rejected cleanly
// ------------------------------------------------------------
TEST_F(PdcpLoopbackTest, EmptySduIsRejected)
{
  EXPECT_EQ(entity->txSdu(nullptr, 0), Status::PARSE_ERROR);
}

TEST_F(PdcpLoopbackTest, EmptyPduIsRejected)
{
  EXPECT_EQ(entity->rxPdu(nullptr, 0), Status::PARSE_ERROR);
}

TEST_F(PdcpLoopbackTest, TruncatedPduRejected)
{
  const uint8_t bad[] = {0x80}; // only 1 byte, needs at least 2 for 12-bit SN
  EXPECT_EQ(entity->rxPdu(bad, sizeof(bad)), Status::PARSE_ERROR);
}