#include "rlc_pdu.h"
#include <cstring>

namespace lte
{

  namespace
  {
    // Write the LI extension part
    size_t writeLiList(const std::array<uint16_t, MAX_LI_PER_PDU> &li,
                       uint8_t num_li,
                       uint8_t *out)
    {
      size_t octet = 0;
      uint8_t e = 1;
      for (uint8_t i = 0; i < num_li; i++)
      {
        if (li[i] == 0 || li[i] > LI_MAX_VALUE)
          return 0;
        if (i == num_li - 1)
          e = 0;
        if ((i & 1) == 0)
        {
          out[octet] = static_cast<uint8_t>((e << 7) | ((li[i] >> 4) & 0x7F));
          out[++octet] = static_cast<uint8_t>((li[i] & 0x0F) << 4);
        }
        else
        {
          out[octet] |= static_cast<uint8_t>((e << 3) | ((li[i] >> 8) & 0x07));
          out[++octet] = static_cast<uint8_t>(li[i] & 0xFF);
          octet++;
        }
      }
      octet = (num_li & 1) ? (octet + 1) : octet;
      return octet;
    }

    // Read the LI extension part
    Status readLiList(const uint8_t *buf,
                      size_t avail,
                      std::array<uint16_t, MAX_LI_PER_PDU> &out_li,
                      uint8_t &out_num_li,
                      size_t &out_consumed)
    {
      uint8_t e = 1;
      size_t index = 0, octet = 0, sum_li = 0;
      while (e)
      {
        if (octet + 1 >= avail)
          return Status::PARSE_ERROR;
        if ((index & 1) == 0)
        {
          e = static_cast<uint8_t>((buf[octet] >> 7) & 0x01);
          out_li[index] = static_cast<uint16_t>(
              (static_cast<uint16_t>(buf[octet] & 0x7F) << 4) | ((buf[octet + 1] >> 4) & 0x0F));
          octet++;
        }
        else
        {
          e = static_cast<uint8_t>((buf[octet] >> 3) & 0x01);
          out_li[index] = static_cast<uint16_t>(
              (static_cast<uint16_t>(buf[octet] & 0x07) << 8) | (buf[octet + 1] & 0xFF));
          octet += 2;
        }

        if ((index >= MAX_LI_PER_PDU) || (out_li[index] == 0) || (out_li[index] > LI_MAX_VALUE))
        {
          return Status::INVALID_LI;
        }
        sum_li += out_li[index++];
      }
      out_num_li = index;
      out_consumed = (out_num_li & 1) ? (octet + 1) : octet;
      if (out_consumed >= avail || sum_li >= (avail - out_consumed))
        return Status::INVALID_LI;
      return Status::OK;
    }
  }

  size_t RlcPduCodec::umdHeaderSize(RlcSnSize sn_size, uint8_t num_li)
  {
    size_t fixed_header = 0;
    if (sn_size == RlcSnSize::SN5)
    {
      fixed_header = UMD_FIXED_HEADER_SN5;
    }
    else if (sn_size == RlcSnSize::SN10)
    {
      fixed_header = UMD_FIXED_HEADER_SN10;
    }
    return fixed_header + (static_cast<size_t>(num_li) * 3 + 1) / 2; // num_li x 1.5 bytes, rounded up
  }

  size_t RlcPduCodec::serializeUmd(const RlcPdu &pdu,
                                   RlcSnSize sn_size,
                                   uint8_t *out_buf,
                                   size_t out_buf_size)
  {
    if (pdu.num_li > MAX_LI_PER_PDU || pdu.mode != RlcMode::UM || pdu.sn >= rlcSnModulus(sn_size))
      return 0;

    if (sn_size == RlcSnSize::SN5)
    {
      return serializeUmd5(pdu, out_buf, out_buf_size);
    }
    else if (sn_size == RlcSnSize::SN10)
    {
      return serializeUmd10(pdu, out_buf, out_buf_size);
    }
    return 0;
  }

  size_t RlcPduCodec::serializeUmd5(const RlcPdu &pdu, uint8_t *buf, size_t sz)
  {
    const size_t header_sz = umdHeaderSize(RlcSnSize::SN5, pdu.num_li);
    const size_t total = header_sz + pdu.data_len;
    if (sz < total)
      return 0;
    const uint8_t e = pdu.num_li > 0 ? 1 : 0;
    buf[0] = static_cast<uint8_t>((static_cast<uint8_t>(pdu.fi) << 6) | (e << 5) | (pdu.sn & 0x1F));

    if (e)
    {
      if (header_sz != writeLiList(pdu.length_indicators, pdu.num_li, buf + UMD_FIXED_HEADER_SN5) + UMD_FIXED_HEADER_SN5)
      {
        return 0;
      }
    }

    const uint8_t *expected_dst = buf + header_sz;
    if (pdu.data && pdu.data_len > 0 && pdu.data != expected_dst)
    {
      std::memcpy(buf + header_sz, pdu.data, pdu.data_len);
    }
    return total;
  }

  size_t RlcPduCodec::serializeUmd10(const RlcPdu &pdu, uint8_t *buf, size_t sz)
  {
    const size_t header_sz = umdHeaderSize(RlcSnSize::SN10, pdu.num_li);
    const size_t total = header_sz + pdu.data_len;
    if (sz < total)
      return 0;
    const uint8_t e = pdu.num_li > 0 ? 1 : 0;
    buf[0] = static_cast<uint8_t>((static_cast<uint8_t>(pdu.fi) << 3) | (e << 2) | ((pdu.sn >> 8) & 0x03));
    buf[1] = static_cast<uint8_t>(pdu.sn & 0xFF);

    if (e)
    {
      if (header_sz != writeLiList(pdu.length_indicators, pdu.num_li, buf + UMD_FIXED_HEADER_SN10) + UMD_FIXED_HEADER_SN10)
      {
        return 0;
      }
    }

    const uint8_t *expected_dst = buf + header_sz;
    if (pdu.data && pdu.data_len > 0 && pdu.data != expected_dst)
    {
      std::memcpy(buf + header_sz, pdu.data, pdu.data_len);
    }
    return total;
  }

  Status RlcPduCodec::deserializeUmd(const uint8_t *raw_buf,
                                     size_t raw_len,
                                     RlcSnSize sn_size,
                                     RlcPdu &out_pdu)
  {
    if (!raw_buf || raw_len == 0)
      return Status::PARSE_ERROR;
    if (sn_size == RlcSnSize::SN5)
    {
      return deserializeUmd5(raw_buf, raw_len, out_pdu);
    }
    else if (sn_size == RlcSnSize::SN10)
    {
      return deserializeUmd10(raw_buf, raw_len, out_pdu);
    }
    else
    {
      return Status::NOT_IMPLEMENTED;
    }
  }

  Status RlcPduCodec::deserializeUmd5(const uint8_t *buf, size_t len, RlcPdu &out)
  {
    if (len < UMD_FIXED_HEADER_SN5)
      return Status::PARSE_ERROR;

    out.fi = static_cast<FramingInfo>((buf[0] >> 6) & 0x03);
    const uint8_t e = static_cast<uint8_t>((buf[0] >> 5) & 0x01);
    out.sn = static_cast<SN_t>(buf[0] & 0x1F);

    size_t consumed = 0;
    if (e)
    {
      Status s = readLiList(buf + UMD_FIXED_HEADER_SN5, len - UMD_FIXED_HEADER_SN5,
                            out.length_indicators, out.num_li, consumed);
      if (s != Status::OK)
        return s;
    }
    else
    {
      out.num_li = 0;
    }
    out.data = buf + UMD_FIXED_HEADER_SN5 + consumed;
    out.data_len = len - UMD_FIXED_HEADER_SN5 - consumed;
    return Status::OK;
  }

  Status RlcPduCodec::deserializeUmd10(const uint8_t *buf, size_t len, RlcPdu &out)
  {
    if (len < UMD_FIXED_HEADER_SN10)
      return Status::PARSE_ERROR;
    if ((buf[0] >> 5) & 0x07)
      return Status::RESERVED_VALUE;
    else
    {
      out.fi = static_cast<FramingInfo>((buf[0] >> 3) & 0x03);
    }
    const uint8_t e = static_cast<uint8_t>((buf[0] >> 2) & 0x01);
    out.sn = static_cast<SN_t>(
        (static_cast<uint16_t>(buf[0] & 0x03) << 8) | (buf[1] & 0xFF));

    size_t consumed = 0;
    if (e)
    {
      Status s = readLiList(buf + UMD_FIXED_HEADER_SN10, len - UMD_FIXED_HEADER_SN10,
                            out.length_indicators, out.num_li, consumed);
      if (s != Status::OK)
        return s;
    }
    else
    {
      out.num_li = 0;
    }
    out.data = buf + UMD_FIXED_HEADER_SN10 + consumed;
    out.data_len = len - UMD_FIXED_HEADER_SN10 - consumed;
    return Status::OK;
  }

}