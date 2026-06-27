#pragma once
#include "rlc_sap.h"
#include "byte_buffer.h"
#include "common_types.h"
#include <cstdint>
#include <cstddef>

namespace lte
{

  // RLC entity in transmitting direction
  class IRlcTxEntity : public rlc_tx_upper_layer_data_sap
  {
  public:
    virtual ~IRlcTxEntity() = default;
    // Inherited: PDCP -> RLC (Tx): handle_sdu(ByteBuffer, uint32_t pdcp_sn)

    // Logical channel: MAC pulls PDU from RLC (Tx)
    // MAC calls this upon a transmission opportunity.
    // RLC segments/concatenates the queued SDUs, build the header, and returns a ByteBuffer <= grant_bytes
    virtual ByteBuffer buildPdu(size_t grant_bytes) = 0;

    // Buffer status for MAC scheduling
    virtual size_t bufferOccupancy() const = 0;

    // RRC reconfiguration: clear queue, reset Tx SN counter
    virtual void reestablish() = 0;
  };

  // RLC entity in receiving direction
  class IRlcRxEntity
  {
  public:
    virtual ~IRlcRxEntity() = default;

    // Logical channel: MAC -> RLC (Rx push)
    // MAC deliver RLC PDU to RLC
    virtual Status rxPdu(ByteBuffer pdu) = 0;

    // Wiring: set notifier to deliver -> PDCP
    void set_upper_data_notifier(rlc_rx_upper_layer_data_notifier *n)
    {
      upper_dn_ = n;
    }

    // RRC reconfiguration
    virtual void reestablish() = 0;

  protected:
    rlc_rx_upper_layer_data_notifier *upper_dn_ = nullptr;
  };
}

//         TX path (PULL)              RX path (PUSH)
//         ───────────────             ───────────────
//         ┌────────┐                  ┌────────┐
//         │  PDCP  │                  │  PDCP  │
//         │   TX   │                  │   RX   │
//         └───┬────┘                  └────▲───┘
//             │                            │
//             │ handle_sdu()               │ on_new_pdu()
//             │ ◄── PDCP gọi               │ ◄── RLC gọi
//             ▼                            │
//         ┌────────┐                  ┌────────┐
//         │  RLC   │                  │  RLC   │
//         │ TM TX  │                  │ TM RX  │
//         └───▲────┘                  └────▲───┘
//             │                            │
//             │ buildPdu()                 │ rxPdu()
//             │ bufferOccupancy()          │ ◄── MAC gọi
//             │ ◄── MAC gọi                │
//             │                            │
//         ┌───┴────┐                  ┌────┴───┐
//         │  MAC   │                  │  MAC   │
//         │   TX   │                  │   RX   │
//         └────────┘                  └────────┘