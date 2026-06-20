#pragma once
// =============================================================================
// test_helpers.h
// Shared test utilities used across unit and integration tests.
//
// Provides:
//   - buildRawPdu()            : build raw PDU bytes as vector<uint8_t>
//   - buildRawPduBuffer()      : build raw PDU as ByteBuffer (for new API)
//   - MockRlcSap               : mock for rlc_tx_upper_layer_data_sap
//   - MockUpperLayerNotifier   : mock for rlc_rx_upper_layer_data_notifier
// =============================================================================

#include "pdcp_pdu.h"
#include "common_types.h"
#include "rlc_sap.h"
#include "byte_buffer.h"
#include "buffer_pool.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lte
{
  namespace test
  {

    // ---------------------------------------------------------------------------
    // buildRawPdu
    // Serialise a PDCP Data PDU (header + payload) into a byte vector.
    // Uses the new PdcpHeader + PdcpPduCodec::buildHeader API.
    // ---------------------------------------------------------------------------
    inline std::vector<uint8_t> buildRawPdu(SN_t sn,
                                            const std::string &payload,
                                            PdcpPduType pdu_type = PdcpPduType::DRB_12bitSn)
    {
      const size_t hdr_sz = PdcpPduCodec::headerSize(pdu_type);
      std::vector<uint8_t> buf(hdr_sz + payload.size());

      PdcpHeader hdr;
      hdr.sn = sn;
      hdr.dc = PDCP_DC_DATA;

      PdcpPduCodec::buildHeader(hdr, buf.data(), pdu_type);

      // Copy payload after header
      std::memcpy(buf.data() + hdr_sz, payload.data(), payload.size());

      return buf;
    }

    // ---------------------------------------------------------------------------
    // buildRawPduBuffer
    // Same as buildRawPdu but returns a ByteBuffer allocated from the given pool.
    // Useful for tests that call the new rxPdu(ByteBuffer) API.
    // ---------------------------------------------------------------------------
    inline ByteBuffer buildRawPduBuffer(BufferPool &pool,
                                        SN_t sn,
                                        const std::string &payload,
                                        PdcpPduType pdu_type = PdcpPduType::DRB_12bitSn)
    {
      auto raw = buildRawPdu(sn, payload, pdu_type);
      auto buf = ByteBuffer::allocate(pool, raw.size(), DEFAULT_HEADROOM);
      if (buf.valid())
      {
        buf.append(raw.data(), raw.size());
      }
      return buf;
    }

    // ---------------------------------------------------------------------------
    // makeSduBuffer
    // Allocate a ByteBuffer and fill it with raw payload bytes.
    // For use with txSdu(ByteBuffer).
    // ---------------------------------------------------------------------------
    inline ByteBuffer makeSduBuffer(BufferPool &pool,
                                    const uint8_t *data, size_t len)
    {
      auto buf = ByteBuffer::allocate(pool, len, DEFAULT_HEADROOM);
      if (buf.valid())
      {
        buf.append(data, len);
      }
      return buf;
    }

    inline ByteBuffer makeSduBuffer(BufferPool &pool,
                                    const std::string &payload)
    {
      return makeSduBuffer(pool,
                           reinterpret_cast<const uint8_t *>(payload.data()),
                           payload.size());
    }

    // ---------------------------------------------------------------------------
    // MockRlcSap — mock for PDCP → RLC (Tx direction)
    //
    // Captures the last SDU pushed down from PDCP for test assertions.
    // Per SAP plan §6.1: copy out for assert, not perf-critical.
    // ---------------------------------------------------------------------------
    class MockRlcSap : public rlc_tx_upper_layer_data_sap
    {
    public:
      void handle_sdu(ByteBuffer sdu, uint32_t pdcp_sn) override
      {
        last_sdu.assign(sdu.data(), sdu.data() + sdu.size());
        last_pdcp_sn = pdcp_sn;
        ++call_count;
      }

      std::vector<uint8_t> last_sdu;
      uint32_t last_pdcp_sn = 0;
      uint32_t call_count = 0;

      void reset()
      {
        last_sdu.clear();
        last_pdcp_sn = 0;
        call_count = 0;
      }
    };

    // ---------------------------------------------------------------------------
    // MockUpperLayerNotifier — mock for RLC → PDCP (Rx direction)
    //
    // Captures the last PDU delivered up from RLC for test assertions.
    // ---------------------------------------------------------------------------
    class MockUpperLayerNotifier : public rlc_rx_upper_layer_data_notifier
    {
    public:
      void on_new_pdu(ByteBuffer pdu) override
      {
        last_pdu.assign(pdu.data(), pdu.data() + pdu.size());
        ++call_count;
      }

      std::vector<uint8_t> last_pdu;
      uint32_t call_count = 0;

      void reset()
      {
        last_pdu.clear();
        call_count = 0;
      }
    };

  } // namespace test
} // namespace lte