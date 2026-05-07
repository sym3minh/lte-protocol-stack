#include <gtest/gtest.h>
#include "pdcp_entity.h"
#include "pdcp_pdu.h"
#include <vector>
#include <string>

using namespace lte;

class PdcpTxTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    pool = std::make_unique<BufferPool>(2048, 64);
    entity = std::make_unique<PdcpEntity>(
        1, BearerType::DRB, RlcMode::AM, *pool);
  }

  std::unique_ptr<BufferPool> pool;
  std::unique_ptr<PdcpEntity> entity;
};

// SN starts at 0 and increments with each txSdu()
TEST_F(PdcpTxTest, SequenceNumberStartsAtZero)
{
  EXPECT_EQ(entity->txNext(), 0u);
}

TEST_F(PdcpTxTest, SequenceNumberIncrementsAfterTx)
{
  const uint8_t data[] = {0x01, 0x02, 0x03};

  entity->txSdu(data, sizeof(data));
  EXPECT_EQ(entity->txNext(), 1u);

  entity->txSdu(data, sizeof(data));
  EXPECT_EQ(entity->txNext(), 2u);
}

// PDU header must encode the correct SN and D/C=1
TEST_F(PdcpTxTest, PduHeaderEncodesCorrectSN)
{
  const uint8_t payload[] = {'H', 'e', 'l', 'l', 'o'};
  entity->txSdu(payload, sizeof(payload));

  const auto &pdu_bytes = entity->lastTxPdu();
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
  entity->txSdu(reinterpret_cast<const uint8_t *>(msg.data()), msg.size());

  const auto &pdu = entity->lastTxPdu();
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
    entity->txSdu(data, sizeof(data));
  }
  entity->txSdu(data, sizeof(data)); // this PDU has SN=4095

  const auto &pdu = entity->lastTxPdu();
  ASSERT_GE(pdu.size(), 2u);

  uint16_t sn = ((pdu[0] & 0x0F) << 8) | pdu[1];
  EXPECT_EQ(sn, 4095u);
}

// PDU forwarded via callback
TEST_F(PdcpTxTest, TxCallbackReceivesPdu)
{
  std::vector<uint8_t> forwarded;
  entity->setTxCallback([&](const uint8_t *pdu, size_t len)
                        { forwarded.assign(pdu, pdu + len); });

  const std::string msg = "callback_test";
  entity->txSdu(reinterpret_cast<const uint8_t *>(msg.data()), msg.size());

  EXPECT_EQ(forwarded, entity->lastTxPdu());
  EXPECT_GT(forwarded.size(), 0u);
}
