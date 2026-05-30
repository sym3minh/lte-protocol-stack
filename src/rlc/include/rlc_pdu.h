#pragma once
// ============================================================
// rlc_pdu.h — RLC PDU formats (TS 36.322 §6.2)
// ============================================================

#include "common_types.h"

#include <cstdint>
#include <cstddef>
#include <array>

namespace lte
{

  // Maximum number of Length Indicators in one RLC PDU.
  //
  // Rationale (NOT a spec limit):
  //   TS 36.322 places no hard cap on LI count — it is grant-bounded.
  //   16 is chosen as a stack-allocation cap that comfortably covers
  //   realistic LTE traffic patterns (typical SDU size > 100 bytes,
  //   typical TB size < 12 kB → < 5 LIs per PDU).
  //   If a future scenario needs more, raise this constant; the cost
  //   is just a few bytes per PDU object.
  constexpr uint8_t MAX_LI_PER_PDU = 16;

  // Header size constants (UMD data PDU, no LI).
  //   UMD with SN5  (§6.2.1.3): 1 byte fixed header
  //   UMD with SN10 (§6.2.1.3): 2 byte fixed header
  // Each additional LI adds 1.5 bytes (1-bit E + 11-bit LI) — handled
  // dynamically by umdHeaderSize() / amdHeaderSize().
  constexpr size_t UMD_FIXED_HEADER_SN5 = 1;
  constexpr size_t UMD_FIXED_HEADER_SN10 = 2;

  // Field width constants
  constexpr uint16_t LI_MAX_VALUE = (1u << 11) - 1; // LI is 11-bit

  // ============================================================
  // FramingInfo — TS 36.322 §6.2.2.4
  //
  // 2-bit field in UMD/AMD header indicating whether the PDU starts
  // at the beginning of an SDU and ends at the end of an SDU.
  //
  //   FIRST_LAST       (00): PDU contains one or more complete SDUs
  //                          OR starts at SDU boundary AND ends at one
  //   FIRST_NOTLAST    (01): starts at SDU boundary, last byte is
  //                          NOT the last byte of an SDU
  //   NOTFIRST_LAST    (10): first byte is NOT the first byte of an SDU,
  //                          last byte IS the last byte of an SDU
  //   NOTFIRST_NOTLAST (11): both first and last bytes are mid-SDU
  // ============================================================
  enum class FramingInfo : uint8_t
  {
    FIRST_LAST = 0b00,
    FIRST_NOTLAST = 0b01,
    NOTFIRST_LAST = 0b10,
    NOTFIRST_NOTLAST = 0b11,
  };

  // ============================================================
  // RlcPdu — in-memory representation of one RLC data PDU
  //
  // Used for both UMD and AMD. Fields are grouped by applicability;
  // fields outside a group should be ignored for the given mode.
  // ============================================================
  struct RlcPdu
  {
    // ── Common to UMD and AMD ──
    RlcMode mode = RlcMode::UM;
    SN_t sn = 0; // valid range: depends on sn_size
    FramingInfo fi = FramingInfo::FIRST_LAST;

    // ── LI list (UMD and AMD data PDU) ──
    // length_indicators[0..num_li-1] are valid.
    // Each LI is the length in bytes of one SDU/SDU-segment that
    // ENDS within this PDU's data field. The LAST data unit (which
    // may be a complete SDU or a continuation segment) has no LI —
    // its length is implicit (data_len - sum of LIs).
    std::array<uint16_t, MAX_LI_PER_PDU> length_indicators{};
    uint8_t num_li = 0;

    // ── Payload (BORROWED — not owned by RlcPdu) ──
    // After deserialize(): points into the raw_buf passed in.
    // Before serialize():  caller sets this from their SDU buffer.
    const uint8_t *data = nullptr;
    size_t data_len = 0;
  };

  // ============================================================
  // RlcPduCodec — serialize / deserialize PDUs to / from raw bytes
  //
  // Stateless utility class (all static methods), following the
  // same pattern as PdcpPduCodec.
  //
  // All methods are pure functions of their inputs — they do not
  // allocate, do not touch global state, and do not depend on any
  // clock or pool.
  // ============================================================
  class RlcPduCodec
  {
  public:
    // ----------------------------------------------------------
    // UMD (Unacknowledged Mode Data PDU) — §6.2.1.3
    // ----------------------------------------------------------

    // Serialize a UMD PDU into out_buf.
    //
    // Preconditions:
    //   pdu.mode  must be RlcMode::UM
    //   sn_size   must be RlcSnSize::SN5 or RlcSnSize::SN10
    //   pdu.sn    must fit in sn_size bits
    //   pdu.num_li must be < MAX_LI_PER_PDU
    //   pdu.length_indicators[i] must be <= LI_MAX_VALUE for i < num_li
    //   pdu.data  must point to valid bytes of length pdu.data_len
    //
    // Returns: number of bytes written, or 0 on error.
    //          0 means either preconditions failed or out_buf too small.
    //          (Use deserializeUmd's Status return for finer detail
    //          on the receive path; on Tx the only error mode in
    //          practice is BUFFER_TOO_SMALL.)
    static size_t serializeUmd(const RlcPdu &pdu,
                               RlcSnSize sn_size,
                               uint8_t *out_buf,
                               size_t out_buf_size);

    // Deserialize raw bytes into a UMD RlcPdu.
    //
    // out_pdu.data will point INSIDE raw_buf (zero-copy borrow).
    // Caller must ensure raw_buf lifetime exceeds out_pdu usage.
    //
    // Returns:
    //   OK              on success
    //   PARSE_ERROR     on malformed header / truncated PDU
    //   INVALID_LI      on LI list inconsistency
    //   RESERVED_VALUE  on reserved field violation
    static Status deserializeUmd(const uint8_t *raw_buf,
                                 size_t raw_len,
                                 RlcSnSize sn_size,
                                 RlcPdu &out_pdu);

    // ----------------------------------------------------------
    // Header size helpers — used by entities to size grants
    // ----------------------------------------------------------

    // UMD header size in bytes for given SN size and number of LIs.
    //   Formula: fixed_header + ceil(num_li * 1.5)  bytes
    //   (Each LI is 11 bits + 1 bit E flag = 12 bits = 1.5 bytes.)
    static size_t umdHeaderSize(RlcSnSize sn_size, uint8_t num_li);

  private:
    // ── UMD private helpers ──
    static size_t serializeUmd5(const RlcPdu &pdu, uint8_t *buf, size_t sz);
    static size_t serializeUmd10(const RlcPdu &pdu, uint8_t *buf, size_t sz);
    static Status deserializeUmd5(const uint8_t *buf, size_t len, RlcPdu &out);
    static Status deserializeUmd10(const uint8_t *buf, size_t len, RlcPdu &out);
  };
} // namespace lte