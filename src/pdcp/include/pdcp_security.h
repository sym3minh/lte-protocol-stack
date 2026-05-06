#pragma once
#include "common_types.h"
#include <cstdint>
#include <cstddef>

namespace lte
{

  // ============================================================
  // PdcpSecurity — Ciphering and Integrity Protection
  // Ref: TS 36.323 §5.8, TS 33.401
  //
  // STATUS: stub — pass-through only
  // TODO: implement EEA1 (SNOW 3G), EEA2 (AES-128-CTR),
  //       EIA1 (SNOW 3G MAC-I), EIA2 (AES-128-CMAC)
  // ============================================================
  class PdcpSecurity
  {
  public:
    // Apply ciphering to data in-place.
    // sn      : PDCP sequence number (used as part of COUNT)
    // is_srb  : true for SRBs (affects COUNT construction)
    void applyCiphering(uint8_t *data,
                        size_t len,
                        SN_t sn,
                        bool is_srb = false);

    // Remove ciphering from data in-place.
    void applyDeciphering(uint8_t *data,
                          size_t len,
                          SN_t sn,
                          bool is_srb = false);

    // Compute and append 4-byte MAC-I for integrity protection.
    // Returns false if buffer too small.
    bool applyIntegrity(uint8_t *data, size_t &len, size_t buf_capacity,
                        SN_t sn);

    // Verify MAC-I. Returns true if valid (stub: always true).
    bool verifyIntegrity(const uint8_t *data, size_t len, SN_t sn);

    bool cipheringEnabled() const { return ciphering_enabled_; }
    bool integrityEnabled() const { return integrity_enabled_; }
    void setCiphering(bool v) { ciphering_enabled_ = v; }
    void setIntegrity(bool v) { integrity_enabled_ = v; }

  private:
    bool ciphering_enabled_ = false; // disabled until keys are configured
    bool integrity_enabled_ = false;
  };

} // namespace lte
