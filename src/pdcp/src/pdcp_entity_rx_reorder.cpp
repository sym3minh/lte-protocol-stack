// ============================================================
// pdcp_entity_rx_reorder.cpp — §5.1.2.1.4 RX path for PdcpEntity (STUB)
//
// "PDCP entities using t-Reordering timer"
// Ref: TS 36.323 §5.1.2.1.4
//
// STATUS: stub — rxPduWithReorder returns NOT_IMPLEMENTED.
//
// TODO: implement §5.1.2.1.4 in a future revision.
//   Key additions vs AM no-reorder path:
//     - t-Reordering timer management (start/stop/expiry handler)
//     - RX_Reord state variable (SN triggering the timer)
//     - On timer expiry: deliver all buffered SDUs with COUNT ≤ RX_Reord
//     - reestablishWithReorder() must stop timer + flush buffer + reset state
// ============================================================

#include "pdcp_entity.h"

namespace lte
{

  // ============================================================
  // rxPduWithReorder — TS 36.323 §5.1.2.1.4 (STUB)
  // ============================================================
  Status PdcpEntity::rxPduWithReorder(ByteBuffer /*pdu*/,
                                      bool /*due_to_reestablishment*/)
  {
    // TODO: implement TS 36.323 §5.1.2.1.4
    return Status::NOT_IMPLEMENTED;
  }

  // ============================================================
  // reestablishWithReorder — TS 36.323 §5.2.2 (STUB)
  //
  // When implemented:
  //   Stop t-Reordering timer (if running).
  //   Flush rx_store_ (deliver or discard depending on spec branch).
  //   Reset relevant state variables.
  //   rohc_.resetDownlink();
  // ============================================================
  void PdcpEntity::reestablishWithReorder()
  {
    // TODO: stop t-Reordering timer, flush buffer, reset state
  }

} // namespace lte
