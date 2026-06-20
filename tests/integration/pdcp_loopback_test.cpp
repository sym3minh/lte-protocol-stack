// ============================================================
// pdcp_loopback_test.cpp — Integration test: TX → RX on same entity
//
// Simulates the path:
//   App → txSdu(ByteBuffer) → [PDCP PDU via MockRlcSap] → rxPdu(ByteBuffer) → App
//
// Refactored for zero-copy SAP:
//   - PdcpConfig struct
//   - txSdu(ByteBuffer), rxPdu(ByteBuffer)
//   - MockRlcSap captures TX PDU; test re-injects as ByteBuffer for RX
//   - lastDeliveredSdu() used for RX verification (application boundary)
// ============================================================

#include <gtest/gtest.h>
#include "pdcp_entity.h"
#include "test_helpers.h"

#include <string>
#include <vector>

using namespace lte;
using namespace lte::test;

class PdcpLoopbackTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    pool_ = std::make_unique<BufferPool>(2048, 64);

    PdcpConfig cfg;
    cfg.lcid = 1;
    cfg.bearer = BearerType::DRB;
    cfg.pdu_type = PdcpPduType::DRB_12bitSn;
    cfg.rlc_mode = RlcMode::AM;
    cfg.reordering_enabled = false;

    entity_ = std::make_unique<PdcpEntity>(cfg);
    entity_->set_lower_layer_sap(&mock_rlc_);
  }

  // Transmit an SDU and get the raw PDU bytes captured by MockRlcSap
  std::vector<uint8_t> txAndCapture(const std::string &payload)
  {
    auto sdu = makeSduBuffer(*pool_, payload);
    EXPECT_TRUE(sdu.valid());
    Status s = entity_->txSdu(std::move(sdu));
    EXPECT_EQ(s, Status::OK);
    return mock_rlc_.last_sdu;
  }

  // Re-inject captured PDU bytes as a ByteBuffer into rxPdu
  Status reinjectPdu(const std::vector<uint8_t> &pdu_bytes)
  {
    auto buf = ByteBuffer::allocate(*pool_, pdu_bytes.size(), DEFAULT_HEADROOM);
    EXPECT_TRUE(buf.valid());
    buf.append(pdu_bytes.data(), pdu_bytes.size());
    return entity_->rxPdu(std::move(buf));
  }

  std::unique_ptr<BufferPool> pool_;
  MockRlcSap mock_rlc_;
  std::unique_ptr<PdcpEntity> entity_;
};

// ------------------------------------------------------------
// The flagship test: send "Minh", receive "Minh"
// ------------------------------------------------------------
TEST_F(PdcpLoopbackTest, SimpleStringMinh)
{
  auto pdu_bytes = txAndCapture("Minh");
  ASSERT_GT(pdu_bytes.size(), 0u);

  Status s = reinjectPdu(pdu_bytes);
  ASSERT_EQ(s, Status::OK);

  const auto &delivered = entity_->lastDeliveredSdu();
  ASSERT_EQ(delivered.size(), 4u);

  std::string output(delivered.begin(), delivered.end());
  EXPECT_EQ(output, "Minh");
}

// ------------------------------------------------------------
// Same test with UTF-8 Vietnamese characters
// ------------------------------------------------------------
TEST_F(PdcpLoopbackTest, VietnameseString)
{
  const std::string input = "Xin chào thế giới";

  auto pdu_bytes = txAndCapture(input);
  ASSERT_EQ(reinjectPdu(pdu_bytes), Status::OK);

  const auto &delivered = entity_->lastDeliveredSdu();
  std::string output(delivered.begin(), delivered.end());
  EXPECT_EQ(output, input);
}

// ------------------------------------------------------------
// Binary payload (e.g. simulated IP packet header bytes)
// ------------------------------------------------------------
TEST_F(PdcpLoopbackTest, BinaryPayload)
{
  const std::vector<uint8_t> input = {
      0x45, 0x00, 0x00, 0x28,
      0xAB, 0xCD, 0x40, 0x00,
      0x40, 0x11, 0x00, 0x00};

  auto sdu = makeSduBuffer(*pool_, input.data(), input.size());
  ASSERT_TRUE(sdu.valid());
  ASSERT_EQ(entity_->txSdu(std::move(sdu)), Status::OK);

  ASSERT_EQ(reinjectPdu(mock_rlc_.last_sdu), Status::OK);

  const auto &delivered = entity_->lastDeliveredSdu();
  ASSERT_EQ(delivered.size(), input.size());
  EXPECT_EQ(delivered, input);
}

// ------------------------------------------------------------
// Empty / invalid inputs must be rejected cleanly
// ------------------------------------------------------------
TEST_F(PdcpLoopbackTest, EmptySduIsRejected)
{
  // Default-constructed ByteBuffer is invalid
  ByteBuffer empty;
  EXPECT_EQ(entity_->txSdu(std::move(empty)), Status::PARSE_ERROR);
}

TEST_F(PdcpLoopbackTest, EmptyPduIsRejected)
{
  ByteBuffer empty;
  EXPECT_EQ(entity_->rxPdu(std::move(empty)), Status::PARSE_ERROR);
}

TEST_F(PdcpLoopbackTest, TruncatedPduRejected)
{
  // Only 1 byte — needs at least 2 for 12-bit SN header
  const uint8_t bad[] = {0x80};
  auto buf = ByteBuffer::allocate(*pool_, sizeof(bad), DEFAULT_HEADROOM);
  ASSERT_TRUE(buf.valid());
  buf.append(bad, sizeof(bad));
  EXPECT_EQ(entity_->rxPdu(std::move(buf)), Status::PARSE_ERROR);
}