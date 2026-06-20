#include "pdcp_pdu.h"
#include <cstring>

namespace lte
{

  // ============================================================
  // PdcpPduCodec — implementation
  // ============================================================

  size_t PdcpPduCodec::headerSize(PdcpPduType type)
  {
    switch (type)
    {
    case PdcpPduType::SRB:
      return PDCP_HEADER_SIZE_5BIT;
    case PdcpPduType::DRB_12bitSn:
      return PDCP_HEADER_SIZE_12BIT;
    }
    return 0;
  }

  // ------------------------------------------------------------
  // serialize
  // ------------------------------------------------------------
  size_t PdcpPduCodec::buildHeader(const PdcpHeader &hdr_pdu,
                                   uint8_t *hdr_buf,
                                   PdcpPduType type)
  {
    if (hdr_pdu.sn >= pdcpSnModulus(type))
      return 0;

    switch (type)
    {
    case PdcpPduType::DRB_12bitSn:
      return serialize12(hdr_pdu, hdr_buf);
    case PdcpPduType::SRB:
      return serialize5(hdr_pdu, hdr_buf);
    }
    return 0;
  }

  // 12-bit SN (DRB):
  //   Byte 0: [ D/C(1) | R(1) | R(1) | R(1) | SN[11:8](4) ]
  //   Byte 1: [ SN[7:0] ]
  size_t PdcpPduCodec::serialize12(const PdcpHeader &hdr_pdu, uint8_t *buf)
  {
    buf[0] = static_cast<uint8_t>(
        ((hdr_pdu.dc & 0x01) << 7) | // D/C  — bit 7
        ((hdr_pdu.sn >> 8) & 0x0F)   // SN[11:8] — bits 3:0
    );
    buf[1] = static_cast<uint8_t>(hdr_pdu.sn & 0xFF); // SN[7:0]

    return PDCP_HEADER_SIZE_12BIT;
  }

  // 5-bit SN (SRB1/SRB2):
  //   Byte 0: [ D/C(1) | R(1) | R(1) | SN[4:0](5) ]
  size_t PdcpPduCodec::serialize5(const PdcpHeader &hdr_pdu, uint8_t *buf)
  {
    buf[0] = static_cast<uint8_t>(
        ((hdr_pdu.dc & 0x01) << 7) | // D/C  — bit 7
        (hdr_pdu.sn & 0x1F)          // SN[4:0] — bits 4:0
    );
    return PDCP_HEADER_SIZE_5BIT;
  }

  // ------------------------------------------------------------
  // deserialize
  // ------------------------------------------------------------
  Status PdcpPduCodec::parseHeader(const uint8_t *data,
                                   size_t len,
                                   PdcpPduType type,
                                   PdcpHeader &out_pdu)
  {
    const size_t hdr_sz = headerSize(type);
    if (!data || len < hdr_sz)
      return Status::PARSE_ERROR;

    out_pdu.header_size = hdr_sz;
    switch (type)
    {
    case PdcpPduType::DRB_12bitSn:
      return deserialize12(data, out_pdu);
    case PdcpPduType::SRB:
      return deserialize5(data, out_pdu);
    }
    return Status::PARSE_ERROR;
  }

  Status PdcpPduCodec::deserialize12(const uint8_t *buf, PdcpHeader &out)
  {
    out.dc = (buf[0] >> 7) & 0x01;
    out.sn = static_cast<SN_t>(
        (static_cast<uint16_t>(buf[0] & 0x0F) << 8) | buf[1]);
    return Status::OK;
  }

  Status PdcpPduCodec::deserialize5(const uint8_t *buf, PdcpHeader &out)
  {
    out.dc = (buf[0] >> 7) & 0x01;
    out.sn = static_cast<SN_t>(buf[0] & 0x1F);
    return Status::OK;
  }

} // namespace lte