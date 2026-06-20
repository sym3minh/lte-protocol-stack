#pragma once
#include <cstdint>
#include <cstddef>

// ============================================================
// LTE Protocol Stack — Shared Types
// Ref: 3GPP TS 36.323 (PDCP), TS 36.322 (RLC), TS 36.321 (MAC)
// ============================================================

namespace lte
{

  // ------------------------------------------------------------
  // Bearer / channel types
  // ------------------------------------------------------------
  enum class BearerType : uint8_t
  {
    SRB1 = 0, // Signalling Radio Bearer 1 (RRC)
    SRB2 = 1, // Signalling Radio Bearer 2 (RRC + NAS)
    DRB = 2   // Data Radio Bearer (user data)
  };

  // ------------------------------------------------------------
  // Result codes
  // ------------------------------------------------------------
  enum class Status : uint8_t
  {
    OK = 0,
    POOL_EXHAUSTED = 1,
    INVALID_SN = 2,
    BUFFER_FULL = 3,
    PARSE_ERROR = 4,
    NOT_IMPLEMENTED = 5, // returned by stub RX procedures
    // RLC codec
    INVALID_LI = 6,     // LI (Length Indicator) list malformed
    RESERVED_VALUE = 7, // field uses reserved value
    // ByteBuffer
    HeaderRoomExhausted = 8,
  };

  // ------------------------------------------------------------
  // Sequence number type
  // Using uint32_t to hold up to 16-bit values
  // ------------------------------------------------------------
  using SN_t = uint32_t;
  using LCID_t = uint8_t;

  // PDCP SN sizes per TS 36.323 §7.1
  constexpr SN_t SN_MAX_12BIT = 4096; // 2^12, for DRB long SN
  constexpr SN_t SN_MAX_5BIT = 32;    // 2^5,  for SRB

  enum class PdcpPduType : uint8_t
  {
    SRB,
    DRB_7bitSn,
    DRB_12bitSn,
  };

  // SN length derived from bearer type
  inline SN_t pdcpSnModulus(PdcpPduType type)
  {
    switch (type)
    {
    case PdcpPduType::SRB:
      return SN_MAX_5BIT;
    case PdcpPduType::DRB_12bitSn:
      return SN_MAX_12BIT;
    }
    return 0;
  }

  inline SN_t pdcpSnWindow(PdcpPduType type)
  {
    return pdcpSnModulus(type) >> 1; // Divide by 2
  }

  // ============================================================
  // RLC-specific types  (TS 36.322)
  // ============================================================
  enum class RlcMode : uint8_t
  {
    TM = 0, // Transparent Mode
    UM = 1, // Unacknowledged Mode
    AM = 2  // Acknowledged Mode
  };

  // ------------------------------------------------------------
  // RlcSnSize — sequence number bit-width per TS 36.322 §6.2.1
  //
  //   UM: SN5 (5-bit, for SRB-like use) or SN10 (10-bit, DTCH)
  //   AM: SN10 (10-bit) in LTE; SN16 reserved for future / 5G NR
  // ------------------------------------------------------------
  enum class RlcSnSize : uint8_t
  {
    SN5 = 5,   // UM only  — 32 sequence numbers
    SN10 = 10, // UM / AM  — 1024 sequence numbers
    SN12 = 12, // (reserved for future extension)
    SN16 = 16, // (reserved — used in 5G NR UM)
  };

  // Helper: modulus for a given SN size (2^N)
  inline SN_t rlcSnModulus(RlcSnSize s)
  {
    return static_cast<SN_t>(1) << static_cast<uint8_t>(s);
  }

  // ------------------------------------------------------------
  // LogicalChannel — TS 36.321 §6.1.1
  //
  // Determines scheduling priority and which RLC mode is allowed.
  //   CCCH : Common Control  — mapped to TM only
  //   DCCH : Dedicated Control — SRB1/SRB2, mapped to AM
  //   DTCH : Dedicated Traffic — DRB, mapped to UM or AM
  //   PCCH : Paging — downlink only, TM
  //   BCCH : Broadcast — downlink only, TM
  // ------------------------------------------------------------
  enum class LogicalChannel : uint8_t
  {
    CCCH = 0,
    DCCH = 1,
    DTCH = 2,
    PCCH = 3,
    BCCH = 4,
  };

  // ------------------------------------------------------------
  // Timer sentinel — used by RLC entities to represent "no timer"
  //
  // TimerManager will use uint32_t handle IDs.  This sentinel
  // value indicates that a handle is not currently active,
  // analogous to a null pointer.
  // ------------------------------------------------------------
  constexpr uint32_t INVALID_TIMER_ID = UINT32_MAX;

} // namespace lte