#pragma once
#include "common_types.h"
#include <cstdint>
#include <cstddef>

namespace lte
{

  // ============================================================
  // PDCP PDU formats — TS 36.323 §6.2
  //
  // Data PDU (D/C = 1), 12-bit SN for DRB:
  //   Byte 0:  [ D/C | R | R | R | SN[11:8] ]   (D/C=1, R=reserved=0)
  //   Byte 1:  [ SN[7:0] ]
  //   Bytes 2+: payload (ROHC-compressed SDU after ciphering)
  //
  // Data PDU (D/C = 1), 5-bit SN for SRB:
  //   Byte 0:  [ D/C | R | R | SN[4:0] ]
  //   Bytes 1+: payload
  //
  // Control PDU (D/C = 0): status report — not implemented in this phase
  // ============================================================

  // Maximum SDU size per TS 36.323
  constexpr size_t PDCP_MAX_SDU_SIZE = 8188;   // bytes
  constexpr size_t PDCP_HEADER_SIZE_12BIT = 2; // bytes for 12-bit SN
  constexpr size_t PDCP_HEADER_SIZE_5BIT = 1;  // bytes for 5-bit SN

  // D/C field value
  constexpr uint8_t PDCP_DC_DATA = 1;
  constexpr uint8_t PDCP_DC_CONTROL = 0;

  // ============================================================
  // PdcpPdu — in-memory representation of one PDCP PDU
  // Does NOT own the payload buffer; payload points into a
  // BufferPool block managed by PdcpEntity.
  // ============================================================
  struct PdcpPdu
  {
    SN_t sn = 0; // sequence number
    uint8_t dc = PDCP_DC_DATA;
    BearerType bearer = BearerType::DRB;

    const uint8_t *payload = nullptr; // points into BufferPool block
    size_t payload_len = 0;

    bool isData() const { return dc == PDCP_DC_DATA; }
    bool isControl() const { return dc == PDCP_DC_CONTROL; }
  };

  // ============================================================
  // PdcpPduCodec — serialise / deserialise PDUs to/from raw bytes
  // Stateless utility class (all static methods)
  // ============================================================
  class PdcpPduCodec
  {
  public:
    // Serialise a PdcpPdu into out_buf.
    // Returns number of bytes written, or 0 on error.
    // out_buf must be at least headerSize(pdu.bearer) + pdu.payload_len bytes.
    static size_t serialize(const PdcpPdu &pdu,
                            uint8_t *out_buf,
                            size_t out_buf_size);

    // Deserialise raw bytes into a PdcpPdu.
    // pdu.payload will point INSIDE raw_buf (zero-copy).
    // Caller must ensure raw_buf lifetime exceeds pdu usage.
    // Returns Status::OK or Status::PARSE_ERROR.
    static Status deserialize(const uint8_t *raw_buf,
                              size_t raw_len,
                              BearerType bearer,
                              PdcpPdu &out_pdu);

    // Header size in bytes for a given bearer type
    static size_t headerSize(BearerType bearer);

  private:
    static size_t serialize12(const PdcpPdu &pdu, uint8_t *buf, size_t sz);
    static size_t serialize5(const PdcpPdu &pdu, uint8_t *buf, size_t sz);
    static Status deserialize12(const uint8_t *buf, size_t len, PdcpPdu &out);
    static Status deserialize5(const uint8_t *buf, size_t len, PdcpPdu &out);
  };

} // namespace lte