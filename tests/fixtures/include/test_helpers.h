#pragma once
// =============================================================================
// test_helpers.h
// Shared test utilities used across unit and integration tests.
//
// Currently provides:
//   - PduBuilder  : thin wrapper around PdcpPduCodec::serialize for tests
//
// Future additions (when RLC/MAC arrive):
//   - MockRlcLayer  : gmock stub for the RLC SAP
//   - MockLowerLayer: generic downward-SAP mock
//   - PduGenerator  : bulk PDU factory for throughput/stress tests
// =============================================================================

#include "pdcp_pdu.h"
#include "common_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lte
{
  namespace test
  {

    // ---------------------------------------------------------------------------
    // buildRawPdu
    // Serialise a PDCP Data PDU (SN + payload) into a byte vector.
    // Used everywhere we need to feed raw bytes into rxPdu() directly.
    // ---------------------------------------------------------------------------
    inline std::vector<uint8_t> buildRawPdu(SN_t sn,
                                            const std::string &payload,
                                            BearerType bearer = BearerType::DRB)
    {
      const size_t hdr = PdcpPduCodec::headerSize(bearer);
      std::vector<uint8_t> buf(hdr + payload.size());

      PdcpPdu pdu;
      pdu.sn = sn;
      pdu.dc = PDCP_DC_DATA;
      pdu.bearer = bearer;
      pdu.payload = reinterpret_cast<const uint8_t *>(payload.data());
      pdu.payload_len = payload.size();

      const size_t written = PdcpPduCodec::serialize(pdu, buf.data(), buf.size());
      buf.resize(written);
      return buf;
    }

  } // namespace test
} // namespace lte
