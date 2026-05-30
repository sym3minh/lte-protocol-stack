#include <gtest/gtest.h>

#include "pdcp_entity.h"
#include "pdcp_pdu.h"
#include "buffer_pool.h"
#include "test_helpers.h"

#include <vector>
#include <string>
#include <cstdint>

using namespace lte;
using lte::test::buildRawPdu;

class RxUmNoReorderTest : public ::testing::Test
{
protected:
  static constexpr LCID_t lcid = 1;

  void SetUp() override
  {
    pool = std::make_unique<BufferPool>(2048, 64);
    entity = std::make_unique<PdcpEntity>(
        lcid, BearerType::DRB, RlcMode::UM, *pool,
        PdcpEntity::RxMode::UmNoReorder);

    entity->setDeliverCallback([this](const uint8_t *sdu, size_t len)
                               { delivered.emplace_back(sdu, sdu + len); });
  }

  // Convenience: build a raw PDU and inject it into the entity.
  Status rx(SN_t sn, const std::string &payload = "X",
            bool reestablish = false)
  {
    auto raw = buildRawPdu(sn, payload);
    return entity->rxPdu(raw.data(), raw.size(), reestablish);
  }

  // Convenience: rebuild entity with custom initial RX state.
  // TX state defaults to 0 — not used by Surface A tests.
  void resetWithState(SN_t rx_next, uint32_t rx_hfn)
  {
    delivered.clear();
    PdcpEntity::TestInitState init{
        .rx_next = rx_next,
        .rx_hfn = rx_hfn,
        .rx_deliv = 0, // unused by UM
        .tx_next = 0,
        .tx_hfn = 0};
    entity = std::make_unique<PdcpEntity>(
        lcid, BearerType::DRB, RlcMode::UM, *pool,
        PdcpEntity::RxMode::UmNoReorder, init);

    entity->setDeliverCallback([this](const uint8_t *sdu, size_t len)
                               { delivered.emplace_back(sdu, sdu + len); });
  }

  std::unique_ptr<BufferPool> pool;
  std::unique_ptr<PdcpEntity> entity;

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
  EXPECT_EQ(entity->rxNext(), 3u);
}

TEST_F(RxUmNoReorderTest, OutOfOrder_SnGreaterThanNext_DeliveredImmediately_NoBuffering)
{
  ASSERT_EQ(rx(0, "p0"), Status::OK);
  ASSERT_EQ(rx(3, "p3"), Status::OK);
  EXPECT_EQ(entity->rxNext(), 4u);
}

TEST_F(RxUmNoReorderTest, LatePdu_SnLessThanNext_IncrementsHfnPerSpecLiteral)
{
  ASSERT_EQ(rx(0, "p0"), Status::OK);
  ASSERT_EQ(rx(1, "p1"), Status::OK);
  ASSERT_EQ(rx(2, "p2"), Status::OK);
  ASSERT_EQ(rx(3, "p3"), Status::OK);
  ASSERT_EQ(rx(1, "p11"), Status::OK);
  EXPECT_EQ(entity->rxNext(), 2u);
  EXPECT_EQ(entity->rxHfn(), 1u);
}

TEST_F(RxUmNoReorderTest, WrapAround_SnAtMax_RxNextResetsAndHfnIncrements)
{
  resetWithState(4095, 0);
  ASSERT_EQ(rx(4095, "p4095"), Status::OK);
  EXPECT_EQ(entity->rxNext(), 0u);
  EXPECT_EQ(entity->rxHfn(), 1u);
}
