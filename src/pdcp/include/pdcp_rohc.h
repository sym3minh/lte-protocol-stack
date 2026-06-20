#pragma once
#include <cstdint>
#include <cstddef>

namespace lte
{

  // ============================================================
  // PdcpRohc — Robust Header Compression
  // Ref: TS 36.323 §6.2.3, RFC 3095
  //
  // STATUS: stub — pass-through only
  // TODO: implement UoR, UO-0, UO-1, UO-2 packet types
  // ============================================================
  class PdcpRohc
  {
  public:
    // Compress IP/UDP/RTP headers in-place.
    // data    : buffer containing the SDU (IP packet)
    // len     : in/out — updated if header is compressed
    // Returns true on success.
    bool compress(uint8_t *data, size_t len);

    // Decompress ROHC-compressed header back to full IP header.
    // data    : buffer containing ROHC-compressed data
    // len     : in/out — updated after decompression
    // Returns true on success.
    bool decompress(uint8_t *data, size_t len);

    // Reset the downlink (RX-side) decompressor context.
    // Called on PDCP re-establishment per TS 36.323 §5.2.2.1.
    // Stub: no-op until full ROHC is implemented.
    void resetDownlink() { /* TODO: reset decompressor state machine */ }

    // Reset the uplink (TX-side) compressor context.
    // Called on PDCP re-establishment.
    void resetUplink() { /* TODO: reset compressor state machine */ }

    bool isEnabled() const { return enabled_; }
    void setEnabled(bool v) { enabled_ = v; }

  private:
    bool enabled_ = false; // disabled until properly implemented
  };

} // namespace lte