#include "pdcp_rohc.h"

namespace lte
{

  // TODO: implement per RFC 3095 / TS 36.323 §6.2.3
  // Currently pass-through — data and len unchanged

  bool PdcpRohc::compress(uint8_t * /*data*/, size_t /*len*/)
  {
    // pass-through: no compression applied
    return true;
  }

  bool PdcpRohc::decompress(uint8_t * /*data*/, size_t /*len*/)
  {
    // pass-through: no decompression applied
    return true;
  }

} // namespace lte