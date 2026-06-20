// ============================================================
// pdcp_rx_um_noreorder_test.cpp
//
// Unit tests for §5.1.2.1.3 (DRBs on RLC UM, no reordering).
//
// Refactored for zero-copy SAP:
//   - PdcpConfig struct replaces positional constructor args
//   - rxPdu(ByteBuffer) replaces rxPdu(raw*, len, bool)
//   - ByteBuffer allocated from shared pool
// ============================================================

#include <gtest/gtest.h>

#include "pdcp_entity.h"
#include "pdcp_pdu.h"
#include "buffer_pool.h"
#include "test_helpers.h"

#include <vector>
#include <string>
#include <cstdint>

using namespace lte;
using namespace lte::test;

static PdcpConfig makeUmDrbConfig()
{
  PdcpConfig cfg;
  cfg.lcid = 1;
  cfg.bearer = BearerType::DRB;
  cfg.pdu_type = PdcpPduType::DRB_12bitSn;
  cfg.rlc_mode = RlcMode::UM;
  cfg.reordering_enabled = false;
  return cfg;
}

class RxUmNoReorderTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    pool_ = std::make_unique<BufferPool>(2048, 64);
    entity_ = std::make_unique<PdcpEntity>(makeUmDrbConfig());

    entity_->setDeliverCallback([this](const uint8_t *sdu, size_t len)
                                { delivered.emplace_back(sdu, sdu + len); });
  }

  // Build raw PDU as ByteBuffer and inject into entity
  Status rx(SN_t sn, const std::string &payload = "X",
            bool reestablish = false)
  {
    auto buf = buildRawPduBuffer(*pool_, sn, payload);
    EXPECT_TRUE(buf.valid());
    return entity_->rxPdu(std::move(buf), reestablish);
  }

  // Rebuild entity with custom initial RX state
  void resetWithState(SN_t rx_next, uint32_t rx_hfn)
  {
    delivered.clear();
    PdcpEntity::TestInitState init{};
    init.rx_next = rx_next;
    init.rx_hfn = rx_hfn;
    init.rx_deliv = 0; // unused by UM
    init.tx_next = 0;
    init.tx_hfn = 0;

    entity_ = std::make_unique<PdcpEntity>(makeUmDrbConfig(), init);

    entity_->setDeliverCallback([this](const uint8_t *sdu, size_t len)
                                { delivered.emplace_back(sdu, sdu + len); });
  }

  std::unique_ptr<BufferPool> pool_;
  std::unique_ptr<PdcpEntity> entity_;

  std::vector<std::vector<uint8_t>> delivered;
};

TEST_F(RxUmNoReorderTest, Forward_SequentialPdus_AllDeliveredInOrder)
{
  ASSERT_EQ(rx(0, "p0"), Status::OK);
  ASSERT_EQ(rx(1, "p1"), Status::OK);
  ASSERT_EQ(rx(2, "p2"), Status::OK);
  EXPECT_EQ(std::string(delivered[0].begin(), delivered[0].end()), "p0");
  EXPECT_EQ(std::string(delivered[1].begin(), delivered[1].end()), "p1");
  EXPECT_EQ(std::string(delivered[2].begin(), delivered[2].end()), "p2");
  EXPECT_EQ(entity_->rxNext(), 3u);
}

TEST_F(RxUmNoReorderTest, OutOfOrder_SnGreaterThanNext_DeliveredImmediately_NoBuffering)
{
  ASSERT_EQ(rx(0, "p0"), Status::OK);
  ASSERT_EQ(rx(3, "p3"), Status::OK);
  EXPECT_EQ(entity_->rxNext(), 4u);
}

TEST_F(RxUmNoReorderTest, LatePdu_SnLessThanNext_IncrementsHfnPerSpecLiteral)
{
  ASSERT_EQ(rx(0, "p0"), Status::OK);
  ASSERT_EQ(rx(1, "p1"), Status::OK);
  ASSERT_EQ(rx(2, "p2"), Status::OK);
  ASSERT_EQ(rx(3, "p3"), Status::OK);
  ASSERT_EQ(rx(1, "p11"), Status::OK);
  EXPECT_EQ(entity_->rxNext(), 2u);
  EXPECT_EQ(entity_->rxHfn(), 1u);
}

TEST_F(RxUmNoReorderTest, WrapAround_SnAtMax_RxNextResetsAndHfnIncrements)
{
  resetWithState(4095, 0);
  ASSERT_EQ(rx(4095, "p4095"), Status::OK);
  EXPECT_EQ(entity_->rxNext(), 0u);
  EXPECT_EQ(entity_->rxHfn(), 1u);
}