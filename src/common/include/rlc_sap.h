#pragma once

// SAP interfaces between PDCP and RLC

#include "byte_buffer.h"
#include <cstdint>

namespace lte
{

  constexpr size_t DEFAULT_HEADROOM = 16;
  // PDCP -> RLC (Tx direction)
  // PDCP holds a pointer to this interface. Once PDCP finishes building the PDU (=RLC SDU),
  // it calls handle_sdu() to push it down to the RLC. IRlcEntity implements this interface.

  class rlc_tx_upper_layer_data_sap
  {
  public:
    virtual ~rlc_tx_upper_layer_data_sap() = default;
    virtual void handle_sdu(ByteBuffer sdu, uint32_t pdcp_sn) = 0;
  };

  // RLC -> PDCP (Rx direction)
  // RLC holds a pointer to this inteface. Once RLC finishes reassembling a complete SDU (or TM forwards it directly),
  // it calls on_new_pdu() to deliver it up to PDCP.

  class rlc_rx_upper_layer_data_notifier
  {
  public:
    virtual ~rlc_rx_upper_layer_data_notifier() = default;
    virtual void on_new_pdu(ByteBuffer pdu) = 0;
  };
}