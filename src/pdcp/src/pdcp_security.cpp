#include "pdcp_security.h"

namespace lte {

// TODO: implement EEA2 (AES-128-CTR) per TS 33.401 Annex B
void PdcpSecurity::applyCiphering(uint8_t* /*data*/, size_t /*len*/,
                                   SN_t /*sn*/, bool /*is_srb*/)
{
    // pass-through: no ciphering applied
}

void PdcpSecurity::applyDeciphering(uint8_t* /*data*/, size_t /*len*/,
                                     SN_t /*sn*/, bool /*is_srb*/)
{
    // pass-through: no deciphering applied
}

// TODO: implement EIA2 (AES-128-CMAC) per TS 33.401 Annex D
bool PdcpSecurity::applyIntegrity(uint8_t* /*data*/, size_t& /*len*/,
                                   size_t /*buf_capacity*/, SN_t /*sn*/)
{
    // stub: pretend MAC-I was appended successfully
    return true;
}

bool PdcpSecurity::verifyIntegrity(const uint8_t* /*data*/,
                                    size_t /*len*/, SN_t /*sn*/)
{
    // stub: always pass
    return true;
}

} // namespace lte
