#include "rlc_pdu.h"

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace lte;

namespace
{

  RlcPdu makeUmdPdu(SN_t sn,
                    FramingInfo fi,
                    const uint8_t *data,
                    size_t data_len,
                    const std::vector<uint16_t> &lis = {})
  {
    RlcPdu p;
    p.mode = RlcMode::UM;
    p.sn = sn;
    p.fi = fi;
    p.data = data;
    p.data_len = data_len;
    p.num_li = static_cast<uint8_t>(lis.size());
    for (size_t i = 0; i < lis.size(); ++i)
    {
      p.length_indicators[i] = lis[i];
    }
    return p;
  }

  void expectPduEqual(const RlcPdu &a, const RlcPdu &b)
  {
    EXPECT_EQ(a.sn, b.sn);
    EXPECT_EQ(a.fi, b.fi);
    EXPECT_EQ(a.num_li, b.num_li);
    for (uint8_t i = 0; i < a.num_li; ++i)
    {
      EXPECT_EQ(a.length_indicators[i], b.length_indicators[i]) << "LI mismatch at i=" << int(i);
    }
    ASSERT_EQ(a.data_len, b.data_len);
    if (a.data_len > 0)
    {
      EXPECT_EQ(0, std::memcmp(a.data, b.data, a.data_len));
    }
  }

} // namespace

// ============================================================
// 1. Round-trip — SN5, no LI
// ============================================================
TEST(RlcPduUmd, Sn5_NoLi_RoundTrip)
{
  const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
  auto tx = makeUmdPdu(/*sn*/ 0, FramingInfo::FIRST_LAST, payload, sizeof(payload));

  std::array<uint8_t, 64> buf{};
  size_t n = RlcPduCodec::serializeUmd(tx, RlcSnSize::SN5, buf.data(), buf.size());
  ASSERT_GT(n, 0u);
  EXPECT_EQ(n, UMD_FIXED_HEADER_SN5 + sizeof(payload));

  RlcPdu rx;
  ASSERT_EQ(Status::OK,
            RlcPduCodec::deserializeUmd(buf.data(), n, RlcSnSize::SN5, rx));
  expectPduEqual(tx, rx);
}

// ============================================================
// 2. Round-trip — SN10, no LI, SN at max boundary
// ============================================================
TEST(RlcPduUmd, Sn10_NoLi_RoundTrip_SnMax)
{
  const uint8_t payload[] = {0x01, 0x02, 0x03};
  auto tx = makeUmdPdu(/*sn*/ 1023, FramingInfo::NOTFIRST_NOTLAST, payload, sizeof(payload));

  std::array<uint8_t, 64> buf{};
  size_t n = RlcPduCodec::serializeUmd(tx, RlcSnSize::SN10, buf.data(), buf.size());
  ASSERT_GT(n, 0u);
  EXPECT_EQ(n, UMD_FIXED_HEADER_SN10 + sizeof(payload));

  RlcPdu rx;
  ASSERT_EQ(Status::OK,
            RlcPduCodec::deserializeUmd(buf.data(), n, RlcSnSize::SN10, rx));
  expectPduEqual(tx, rx);
}

// ============================================================
// 3. Round-trip — SN10, 2 LI (even, 3 LI extension bytes)
// ============================================================
TEST(RlcPduUmd, Sn10_TwoLi_RoundTrip)
{
  std::array<uint8_t, 40> payload{};
  for (size_t i = 0; i < payload.size(); ++i)
    payload[i] = static_cast<uint8_t>(i);

  auto tx = makeUmdPdu(/*sn*/ 42, FramingInfo::FIRST_NOTLAST,
                       payload.data(), payload.size(),
                       /*lis*/ {10, 20});

  std::array<uint8_t, 64> buf{};
  size_t n = RlcPduCodec::serializeUmd(tx, RlcSnSize::SN10, buf.data(), buf.size());
  ASSERT_GT(n, 0u);
  EXPECT_EQ(n, RlcPduCodec::umdHeaderSize(RlcSnSize::SN10, 2) + payload.size());

  RlcPdu rx;
  ASSERT_EQ(Status::OK,
            RlcPduCodec::deserializeUmd(buf.data(), n, RlcSnSize::SN10, rx));
  expectPduEqual(tx, rx);
}

// ============================================================
// 4. Round-trip — SN10, 3 LI (odd, padding 4 bit at last LI)
// ============================================================
TEST(RlcPduUmd, Sn10_ThreeLi_RoundTrip)
{
  std::array<uint8_t, 100> payload{};
  for (size_t i = 0; i < payload.size(); ++i)
    payload[i] = static_cast<uint8_t>(i * 3);

  auto tx = makeUmdPdu(/*sn*/ 500, FramingInfo::NOTFIRST_LAST,
                       payload.data(), payload.size(),
                       /*lis*/ {10, 20, 30});

  std::array<uint8_t, 128> buf{};
  size_t n = RlcPduCodec::serializeUmd(tx, RlcSnSize::SN10, buf.data(), buf.size());
  ASSERT_GT(n, 0u);
  EXPECT_EQ(n, RlcPduCodec::umdHeaderSize(RlcSnSize::SN10, 3) + payload.size());

  RlcPdu rx;
  ASSERT_EQ(Status::OK,
            RlcPduCodec::deserializeUmd(buf.data(), n, RlcSnSize::SN10, rx));
  expectPduEqual(tx, rx);
}

// ============================================================
// 5. Byte-exact — SN10, no LI
//
// SN=0x155 (341), FI=NOTFIRST_LAST(0b10), E=0
//   Oct1: R1 R1 R1 | FI(10) | E(0) | SN[9:8](01) = 0b00010001 = 0x11
//   Oct2: SN[7:0] = 0x55
// ============================================================
TEST(RlcPduUmd, Sn10_NoLi_ExactBytes)
{
  const uint8_t payload[] = {0xAA, 0xBB};
  auto tx = makeUmdPdu(/*sn*/ 0x155, FramingInfo::NOTFIRST_LAST,
                       payload, sizeof(payload));

  std::array<uint8_t, 16> buf{};
  size_t n = RlcPduCodec::serializeUmd(tx, RlcSnSize::SN10, buf.data(), buf.size());
  ASSERT_EQ(n, 4u);

  const uint8_t expected[] = {0x11, 0x55, 0xAA, 0xBB};
  EXPECT_EQ(0, std::memcmp(buf.data(), expected, n));
}

// ============================================================
// 6. Byte-exact — SN5, 1 LI (odd → 4-bit padding)
//
// SN=10, FI=FIRST_NOTLAST(0b01), E=1, LI1=2, data 3 bytes
// => N=2 elements: element1 = 2 bytes (LI1), last element = 1 byte
//   Oct1: FI(01) | E(1) | SN(01010)        = 0b01101010 = 0x6A
//   Oct2: E_k(0) | LI1[10:4](0000000)      = 0x00
//   Oct3: LI1[3:0](0010) | padding(0000)   = 0x20
// ============================================================
TEST(RlcPduUmd, Sn5_OneLi_ExactBytes)
{
  const uint8_t payload[] = {0xAA, 0xBB, 0xCC}; // 3 bytes
  auto tx = makeUmdPdu(/*sn*/ 10, FramingInfo::FIRST_NOTLAST,
                       payload, sizeof(payload),
                       /*lis*/ {2});

  std::array<uint8_t, 16> buf{};
  size_t n = RlcPduCodec::serializeUmd(tx, RlcSnSize::SN5, buf.data(), buf.size());
  ASSERT_EQ(n, 6u); // 1 (fixed) + 2 (1 LI + padding) + 3 (data)

  const uint8_t expected[] = {0x6A, 0x00, 0x20, 0xAA, 0xBB, 0xCC};
  EXPECT_EQ(0, std::memcmp(buf.data(), expected, n));

  RlcPdu rx;
  ASSERT_EQ(Status::OK,
            RlcPduCodec::deserializeUmd(buf.data(), n, RlcSnSize::SN5, rx));
  expectPduEqual(tx, rx);
}

// ============================================================
// 7. Error — truncated header
// ============================================================
TEST(RlcPduUmd, Sn10_TruncatedHeader_ParseError)
{
  const uint8_t buf[] = {0x11};
  RlcPdu rx;
  EXPECT_EQ(Status::PARSE_ERROR,
            RlcPduCodec::deserializeUmd(buf, sizeof(buf), RlcSnSize::SN10, rx));
}

// ============================================================
// 8. Error — sum of LIs exceeds data length
// ============================================================
TEST(RlcPduUmd, Sn10_LiSumExceedsData_InvalidLi)
{
  std::array<uint8_t, 10> payload{};
  auto tx = makeUmdPdu(/*sn*/ 1, FramingInfo::FIRST_LAST,
                       payload.data(), payload.size(),
                       /*lis*/ {100, 200});

  std::array<uint8_t, 64> buf{};
  size_t n = RlcPduCodec::serializeUmd(tx, RlcSnSize::SN10, buf.data(), buf.size());
  ASSERT_GT(n, 0u);

  RlcPdu rx;
  EXPECT_EQ(Status::INVALID_LI,
            RlcPduCodec::deserializeUmd(buf.data(), n, RlcSnSize::SN10, rx));
}

// ============================================================
// 9. Round-trip — Segment(start) + Full SDU + Segment(end), FI=11
//
// Data field = 3 elements (TS 36.322 §6.2.2.2):
//   element[0] = tail of a previous SDU   (segment at start)
//   element[1] = one full SDU
//   element[2] = head of a next SDU       (segment at end)
// 3 elements => 2 LI. FI=NOTFIRST_NOTLAST(0b11):
//   first byte of Data field is NOT the start of an SDU,
//   last  byte of Data field is NOT the end   of an SDU.
// The two segments belong to two different SDUs (spec-compliant).
//   LI1 = len(element[0]) = 5
//   LI2 = len(element[1]) = 10
//   last element length = data_len - (5 + 10) = 30 - 15 = 15
// ============================================================
TEST(RlcPduUmd, Sn10_StartSeg_FullSdu_EndSeg_RoundTrip)
{
  std::array<uint8_t, 30> payload{};
  for (size_t i = 0; i < payload.size(); ++i)
    payload[i] = static_cast<uint8_t>(0xA0 + i);

  auto tx = makeUmdPdu(/*sn*/ 7, FramingInfo::NOTFIRST_NOTLAST,
                       payload.data(), payload.size(),
                       /*lis*/ {5, 10}); // element0=5, element1=10, last=15

  std::array<uint8_t, 64> buf{};
  size_t n = RlcPduCodec::serializeUmd(tx, RlcSnSize::SN10, buf.data(), buf.size());
  ASSERT_GT(n, 0u);
  EXPECT_EQ(n, RlcPduCodec::umdHeaderSize(RlcSnSize::SN10, 2) + payload.size());

  RlcPdu rx;
  ASSERT_EQ(Status::OK,
            RlcPduCodec::deserializeUmd(buf.data(), n, RlcSnSize::SN10, rx));
  expectPduEqual(tx, rx);
}

// ============================================================
// 10. Error — LI value 0 is reserved (TS 36.322 §6.2.2.5)
//
// "The value 0 is reserved." A receiver getting a PDU whose LI
// field is 0 must treat it as invalid (§5.5.1: discard PDU with
// reserved/invalid values).
//
// We hand-craft the bytes so the wire form definitely carries
// LI=0, independent of whether serializeUmd would reject it on TX.
//
// SN5, FI=FIRST_NOTLAST(0b01), E=1, LI1=0, then 1 data byte.
//   Oct1: FI(01) | E(1) | SN(00001)      = 0b01100001 = 0x61
//   Oct2: E_k(0) | LI1[10:4](0000000)    = 0x00
//   Oct3: LI1[3:0](0000) | padding(0000) = 0x00
//   Oct4: data                           = 0xCC
// ============================================================
TEST(RlcPduUmd, Sn5_LiZero_Reserved_InvalidLi)
{
  const uint8_t buf[] = {0x61, 0x00, 0x00, 0xCC};

  RlcPdu rx;
  EXPECT_EQ(Status::INVALID_LI,
            RlcPduCodec::deserializeUmd(buf, sizeof(buf), RlcSnSize::SN5, rx));
}

// ============================================================
// 11. Round-trip — Last element > 2047 bytes (exceeds 11-bit LI)
//
// LI is 11 bits => max representable length = 2047.
// The LAST Data field element carries NO LI (its length is
// inferred), so it is NOT bounded by 2047. Per §6.2.2.2, an SDU /
// segment larger than 2047 octets "can only be mapped to the end
// of the Data field" — exactly because the last element needs no LI.
//
// 2 elements => 1 LI:
//   element[0] = 100  bytes (LI1=100, fits in 11 bits)
//   last       = 3000 bytes (no LI, > 2047 — must be valid)
// ============================================================
TEST(RlcPduUmd, Sn10_LastElementOver2047_RoundTrip)
{
  std::vector<uint8_t> payload(3100);
  for (size_t i = 0; i < payload.size(); ++i)
    payload[i] = static_cast<uint8_t>(i & 0xFF);

  auto tx = makeUmdPdu(/*sn*/ 123, FramingInfo::FIRST_LAST,
                       payload.data(), payload.size(),
                       /*lis*/ {100}); // element0=100, last=3000

  std::vector<uint8_t> buf(4096, 0);
  size_t n = RlcPduCodec::serializeUmd(tx, RlcSnSize::SN10, buf.data(), buf.size());
  ASSERT_GT(n, 0u);
  EXPECT_EQ(n, RlcPduCodec::umdHeaderSize(RlcSnSize::SN10, 1) + payload.size());

  RlcPdu rx;
  ASSERT_EQ(Status::OK,
            RlcPduCodec::deserializeUmd(buf.data(), n, RlcSnSize::SN10, rx));
  expectPduEqual(tx, rx);
}
