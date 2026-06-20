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
  constexpr size_t MAX_PDCP_HEADER_SIZE = 8;   // bytes
  constexpr size_t PDCP_HEADER_SIZE_12BIT = 2; // bytes for 12-bit SN
  constexpr size_t PDCP_HEADER_SIZE_5BIT = 1;  // bytes for 5-bit SN

  // D/C field value
  constexpr uint8_t PDCP_DC_DATA = 1;
  constexpr uint8_t PDCP_DC_CONTROL = 0;

  struct PdcpHeader
  {
    SN_t sn = 0; // sequence number
    uint8_t dc = PDCP_DC_DATA;
    size_t header_size = 0;

    bool isData() const { return dc == PDCP_DC_DATA; }
    bool isControl() const { return dc == PDCP_DC_CONTROL; }
  };
  // ============================================================
  // PdcpPdu — in-memory representation of one PDCP PDU
  // Does NOT own the payload buffer; payload points into a
  // BufferPool block managed by PdcpEntity.
  // ============================================================
  // struct PdcpPdu
  // {
  //   SN_t sn = 0; // sequence number
  //   uint8_t dc = PDCP_DC_DATA;
  //   BearerType bearer = BearerType::DRB;

  //   const uint8_t *payload = nullptr; // points into BufferPool block
  //   size_t payload_len = 0;

  //   bool isData() const { return dc == PDCP_DC_DATA; }
  //   bool isControl() const { return dc == PDCP_DC_CONTROL; }
  // };

  // ============================================================
  // PdcpPduCodec — serialise / deserialise PDUs to/from raw bytes
  // Stateless utility class (all static methods)
  // ============================================================
  class PdcpPduCodec
  {
  public:
    // Serialise a PdcpPdu into out_buf.
    // Returns number of bytes written, or 0 on error.
    static size_t buildHeader(const PdcpHeader &hdr_pdu,
                              uint8_t *hdr_buf,
                              PdcpPduType type);

    // Deserialise raw bytes into a PdcpPdu.
    static Status parseHeader(const uint8_t *data,
                              size_t len,
                              PdcpPduType type,
                              PdcpHeader &out_pdu);

    // Header size in bytes for a given bearer type
    static size_t headerSize(PdcpPduType type);

  private:
    static size_t serialize12(const PdcpHeader &hdr_pdu, uint8_t *buf);
    static size_t serialize5(const PdcpHeader &hdr_pdu, uint8_t *buf);
    static Status deserialize12(const uint8_t *buf, PdcpHeader &out);
    static Status deserialize5(const uint8_t *buf, PdcpHeader &out);
  };

} // namespace lte