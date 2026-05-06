#include "pdcp_pdu.h"
#include <cstring>

namespace lte {

// ============================================================
// PdcpPduCodec — implementation
// ============================================================

size_t PdcpPduCodec::headerSize(BearerType bearer) {
    return (bearer == BearerType::DRB)
        ? PDCP_HEADER_SIZE_12BIT
        : PDCP_HEADER_SIZE_5BIT;
}

// ------------------------------------------------------------
// serialize
// ------------------------------------------------------------
size_t PdcpPduCodec::serialize(const PdcpPdu& pdu,
                                uint8_t*       out_buf,
                                size_t         out_buf_size)
{
    if (pdu.bearer == BearerType::DRB) {
        return serialize12(pdu, out_buf, out_buf_size);
    } else {
        return serialize5(pdu, out_buf, out_buf_size);
    }
}

// 12-bit SN (DRB):
//   Byte 0: [ D/C(1) | R(1) | R(1) | R(1) | SN[11:8](4) ]
//   Byte 1: [ SN[7:0] ]
//   Bytes 2+: payload
//
// FIX: avoid the double-memcpy bug.
//
// txSdu() pre-positions the SDU at block[header_sz..], then sets
// pdu.payload = block + header_sz and calls serialize(pdu, block, ...).
// That makes buf == block and pdu.payload == buf + PDCP_HEADER_SIZE_12BIT,
// so the old unconditional memcpy was doing memcpy(dst, dst, n) — a
// self-copy that is undefined behaviour in C++ (memcpy requires
// non-overlapping regions).
//
// Fix: write the two header bytes, then copy the payload only when it
// does NOT already reside at buf + PDCP_HEADER_SIZE_12BIT.
size_t PdcpPduCodec::serialize12(const PdcpPdu& pdu,
                                  uint8_t*       buf,
                                  size_t         sz)
{
    const size_t total = PDCP_HEADER_SIZE_12BIT + pdu.payload_len;
    if (sz < total) return 0;

    buf[0] = static_cast<uint8_t>(
        ((pdu.dc & 0x01) << 7) |     // D/C  — bit 7
        ((pdu.sn >> 8)   & 0x0F)     // SN[11:8] — bits 3:0
    );
    buf[1] = static_cast<uint8_t>(pdu.sn & 0xFF);  // SN[7:0]

    // Only copy when payload lives in a different buffer region
    const uint8_t* expected_dst = buf + PDCP_HEADER_SIZE_12BIT;
    if (pdu.payload && pdu.payload_len > 0 && pdu.payload != expected_dst) {
        std::memcpy(buf + PDCP_HEADER_SIZE_12BIT, pdu.payload, pdu.payload_len);
    }

    return total;
}

// 5-bit SN (SRB1/SRB2):
//   Byte 0: [ D/C(1) | R(1) | R(1) | SN[4:0](5) ]
//   Bytes 1+: payload
//
// Same fix applied for consistency.
size_t PdcpPduCodec::serialize5(const PdcpPdu& pdu,
                                 uint8_t*       buf,
                                 size_t         sz)
{
    const size_t total = PDCP_HEADER_SIZE_5BIT + pdu.payload_len;
    if (sz < total) return 0;

    buf[0] = static_cast<uint8_t>(
        ((pdu.dc & 0x01) << 7) |     // D/C  — bit 7
        (pdu.sn & 0x1F)              // SN[4:0] — bits 4:0
    );

    const uint8_t* expected_dst = buf + PDCP_HEADER_SIZE_5BIT;
    if (pdu.payload && pdu.payload_len > 0 && pdu.payload != expected_dst) {
        std::memcpy(buf + PDCP_HEADER_SIZE_5BIT, pdu.payload, pdu.payload_len);
    }

    return total;
}

// ------------------------------------------------------------
// deserialize
// ------------------------------------------------------------
Status PdcpPduCodec::deserialize(const uint8_t* raw_buf,
                                  size_t         raw_len,
                                  BearerType     bearer,
                                  PdcpPdu&       out_pdu)
{
    if (!raw_buf || raw_len == 0) return Status::PARSE_ERROR;

    out_pdu.bearer = bearer;

    if (bearer == BearerType::DRB) {
        return deserialize12(raw_buf, raw_len, out_pdu);
    } else {
        return deserialize5(raw_buf, raw_len, out_pdu);
    }
}

Status PdcpPduCodec::deserialize12(const uint8_t* buf,
                                    size_t         len,
                                    PdcpPdu&       out)
{
    if (len < PDCP_HEADER_SIZE_12BIT) return Status::PARSE_ERROR;

    out.dc  = (buf[0] >> 7) & 0x01;
    out.sn  = static_cast<SN_t>(
        (static_cast<uint16_t>(buf[0] & 0x0F) << 8) | buf[1]
    );

    // Payload points directly into the caller's buffer (zero-copy)
    out.payload     = buf + PDCP_HEADER_SIZE_12BIT;
    out.payload_len = len - PDCP_HEADER_SIZE_12BIT;
    return Status::OK;
}

Status PdcpPduCodec::deserialize5(const uint8_t* buf,
                                   size_t         len,
                                   PdcpPdu&       out)
{
    if (len < PDCP_HEADER_SIZE_5BIT) return Status::PARSE_ERROR;

    out.dc  = (buf[0] >> 7) & 0x01;
    out.sn  = static_cast<SN_t>(buf[0] & 0x1F);

    out.payload     = buf + PDCP_HEADER_SIZE_5BIT;
    out.payload_len = len - PDCP_HEADER_SIZE_5BIT;
    return Status::OK;
}

} // namespace lte
