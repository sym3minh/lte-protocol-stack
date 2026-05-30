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
  // Sequence number type (12-bit for DRB, 5-bit for SRB)
  // Using uint16_t to hold up to 12-bit values
  // ------------------------------------------------------------
  using SN_t = uint16_t;
  using LCID_t = uint8_t;

  // SN window sizes per TS 36.323 §7.1
  constexpr SN_t SN_MAX_12BIT = 4096;             // 2^12, for DRB
  constexpr SN_t SN_MAX_5BIT = 32;                // 2^5,  for SRB
  constexpr SN_t SN_WINDOW_12 = SN_MAX_12BIT / 2; // 2048
  constexpr SN_t SN_WINDOW_5 = SN_MAX_5BIT / 2;   // 16

  // ------------------------------------------------------------
  // Bearer / channel types
  // ------------------------------------------------------------
  enum class BearerType : uint8_t
  {
    SRB1 = 0, // Signalling Radio Bearer 1 (RRC)
    SRB2 = 1, // Signalling Radio Bearer 2 (RRC + NAS)
    DRB = 2   // Data Radio Bearer (user data)
  };

  enum class RlcMode : uint8_t
  {
    TM = 0, // Transparent Mode
    UM = 1, // Unacknowledged Mode
    AM = 2  // Acknowledged Mode
  };

  // SN length derived from bearer type
  inline SN_t snModulus(BearerType b)
  {
    return (b == BearerType::DRB) ? SN_MAX_12BIT : SN_MAX_5BIT;
  }

  inline SN_t snWindow(BearerType b)
  {
    return (b == BearerType::DRB) ? SN_WINDOW_12 : SN_WINDOW_5;
  }

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
    // ── RLC codec ──
    INVALID_LI = 6,     // LI (Length Indicator) list malformed
    RESERVED_VALUE = 7, // field uses reserved value
  };

  // ============================================================
  // RLC-specific types  (TS 36.322)
  // ============================================================

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
  inline uint32_t rlcSnModulus(RlcSnSize s)
  {
    return 1u << static_cast<uint8_t>(s);
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