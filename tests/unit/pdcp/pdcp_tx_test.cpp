// ============================================================
// pdcp_tx_test.cpp — Unit tests for PDCP TX path
//
// Refactored for zero-copy SAP:
//   - PdcpConfig struct replaces positional constructor args
//   - txSdu(ByteBuffer) replaces txSdu(const uint8_t*, size_t)
//   - MockRlcSap replaces lastTxPdu() / setTxCallback()
//   - ByteBuffer allocated from shared pool with DEFAULT_HEADROOM
// ============================================================

#include <gtest/gtest.h>
#include "pdcp_entity.h"
#include "pdcp_pdu.h"
#include "test_helpers.h"

#include <vector>
#include <string>

using namespace lte;
using namespace lte::test;

class PdcpTxTest : public ::testing::Test
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

  // Convenience: build a ByteBuffer SDU from raw bytes
  ByteBuffer makeSdu(const uint8_t *data, size_t len)
  {
    return test::makeSduBuffer(*pool_, data, len);
  }

  ByteBuffer makeSdu(const std::string &payload)
  {
    return test::makeSduBuffer(*pool_, payload);
  }

  std::unique_ptr<BufferPool> pool_;
  MockRlcSap mock_rlc_;
  std::unique_ptr<PdcpEntity> entity_;
};

// SN starts at 0 and increments with each txSdu()
TEST_F(PdcpTxTest, SequenceNumberStartsAtZero)
{
  EXPECT_EQ(entity_->txNext(), 0u);
}

TEST_F(PdcpTxTest, SequenceNumberIncrementsAfterTx)
{
  const uint8_t data[] = {0x01, 0x02, 0x03};

  entity_->txSdu(makeSdu(data, sizeof(data)));
  EXPECT_EQ(entity_->txNext(), 1u);

  entity_->txSdu(makeSdu(data, sizeof(data)));
  EXPECT_EQ(entity_->txNext(), 2u);
}

// PDU header must encode the correct SN and D/C=1
TEST_F(PdcpTxTest, PduHeaderEncodesCorrectSN)
{
  entity_->txSdu(makeSdu("Hello"));

  ASSERT_EQ(mock_rlc_.call_count, 1u);
  const auto &pdu_bytes = mock_rlc_.last_sdu;
  ASSERT_GE(pdu_bytes.size(), 2u);

  // 12-bit SN: byte0[3:0] = SN[11:8], byte1 = SN[7:0]
  // First PDU has SN=0
  uint8_t dc = (pdu_bytes[0] >> 7) & 0x01;
  uint16_t sn = ((pdu_bytes[0] & 0x0F) << 8) | pdu_bytes[1];

  EXPECT_EQ(dc, 1u); // D/C = 1 means Data PDU
  EXPECT_EQ(sn, 0u); // SN = 0 for first PDU
}

// Payload bytes must survive serialisation intact
TEST_F(PdcpTxTest, PayloadPreservedInPdu)
{
  const std::string msg = "TestPayload";
  entity_->txSdu(makeSdu(msg));

  ASSERT_EQ(mock_rlc_.call_count, 1u);
  const auto &pdu = mock_rlc_.last_sdu;
  ASSERT_GE(pdu.size(), 2 + msg.size());

  // Skip 2-byte header
  std::string extracted(pdu.begin() + 2, pdu.end());
  EXPECT_EQ(extracted, msg);
}

// PDU at SN=4095 encodes correctly in header
TEST_F(PdcpTxTest, PduHeaderAtMaxSn)
{
  const uint8_t data[2] = {0x11, 0x22};

  // Advance to SN=4095
  for (int i = 0; i < 4095; ++i)
  {
    entity_->txSdu(makeSdu(data, sizeof(data)));
  }
  mock_rlc_.reset();
  entity_->txSdu(makeSdu(data, sizeof(data))); // this PDU has SN=4095

  ASSERT_EQ(mock_rlc_.call_count, 1u);
  const auto &pdu = mock_rlc_.last_sdu;
  ASSERT_GE(pdu.size(), 2u);

  uint16_t sn = ((pdu[0] & 0x0F) << 8) | pdu[1];
  EXPECT_EQ(sn, 4095u);
}

// PDU forwarded via SAP (replaces old TxCallbackReceivesPdu)
TEST_F(PdcpTxTest, PduForwardedToLowerLayerSap)
{
  const std::string msg = "sap_test";
  entity_->txSdu(makeSdu(msg));

  EXPECT_EQ(mock_rlc_.call_count, 1u);
  EXPECT_EQ(mock_rlc_.last_pdcp_sn, 0u);
  EXPECT_GT(mock_rlc_.last_sdu.size(), msg.size()); // payload + header
}

// SN wraps around correctly
TEST_F(PdcpTxTest, SnWrapsAroundAfterMax)
{
  const uint8_t data[1] = {0xAA};
  for (int i = 0; i < 4096; ++i)
  {
    entity_->txSdu(makeSdu(data, sizeof(data)));
  }
  // After 4096 transmissions: SN wrapped to 0, HFN incremented
  EXPECT_EQ(entity_->txNext(), 0u);
  EXPECT_EQ(entity_->txHfn(), 1u);
}