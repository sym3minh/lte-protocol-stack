#pragma once
// ============================================================
// pdcp_entity.h — one PDCP entity per radio bearer (façade)
//
// Ref: TS 36.323 §5 (procedures), §7 (state variables)
//
// Responsibilities:
//   TX path : SN assignment → RoHC compress → cipher → PDU build
//   RX path : dispatch to mode-specific private method via rx_mode_
//
// RX mode is selected at construction time via RxMode enum and
// cannot be changed at runtime (mode is fixed by RRC DRB config).
//
// All TX and RX state variables live directly in this class.
// Mode dispatch is via a switch in rxPduDispatch(); the three
// mode handlers are implemented in separate compilation units:
//   pdcp_entity_rx_am.cpp   — §5.1.2.1.2 AM no-reorder
//   pdcp_entity_rx_um.cpp   — §5.1.2.1.3 UM no-reorder  (stub)
//   pdcp_entity_rx_reorder.cpp — §5.1.2.1.4 with reorder (stub)
//
// Design: srsRAN-style mega-class (pdcp_entity_lte.cc).
//   No Strategy pattern, no IPdcpRxProcedure, no Deps struct.
// ============================================================

#include "common_types.h"
#include "pdcp_pdu.h"
#include "pdcp_rohc.h"
#include "pdcp_security.h"
#include "metrics_collector.h"
#include "rlc_sap.h"
#include "byte_buffer.h"

#include <cstdint>
#include <cstddef>
#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include <memory>
#include <utility>

namespace lte
{

  // Callback types so the entity can hand data to adjacent layers
  // without circular includes.
  using SduDeliverCallback = std::function<void(const uint8_t *sdu, size_t len)>;
  // using PduForwardCallback = std::function<void(const uint8_t* pdu, size_t len)>;

  // ------------------------------------------------------------
  // RxClassification — 5-case branching per TS 36.323 §5.1.2.1.2
  //
  // Used by PdcpEntity::classifyRx() to map each received SN to
  // exactly one spec branch.  Named enumerators match the spec's
  // language to allow side-by-side code review against the standard.
  // ------------------------------------------------------------
  enum class RxClassification : uint8_t
  {
    OutsideWindow,   // Case A: out-of-window → decipher+decompress, discard
    WrapAhead,       // Case B: Next – received > Reordering_Window → HFN+1
    LateFromPrevHfn, // Case C: received – Next >= Reordering_Window → HFN-1
    ForwardInWindow, // Case D: received >= Next (normal forward path)
    BehindSameHfn    // Case E: received < Next (in-order late arrival)
  };

  // ------------------------------------------------------------
  // RxStoreContainer — reordering / re-establishment buffer
  //
  // Key   = COUNT (= HFN × SN_modulus + SN), NOT raw SN.
  //         Two PDUs with the same SN but different HFN (wrap-around
  //         boundary) must be distinguishable for correct ascending-
  //         COUNT delivery required by the spec.
  // Value = decoded SDU bytes (after decipher + ROHC decompress).
  //
  // std::map chosen because:
  //   1. Spec requires delivery in ascending COUNT order →
  //      ordered iteration is a natural fit.
  //   2. At steady state the buffer is almost always empty, so
  //      the log-n overhead vs O(1) is negligible.
  //   3. Range-erase (lower_bound + erase) is O(log n + k) which
  //      is efficient for batch flush.
  // ------------------------------------------------------------
  using RxStoreContainer = std::map<uint32_t, ByteBuffer>;

  struct PdcpConfig
  {
    LCID_t lcid;
    BearerType bearer;
    PdcpPduType pdu_type;
    RlcMode rlc_mode;
    bool reordering_enabled = false;
  };

  class PdcpEntity
  {
  public:
    // ----------------------------------------------------------
    // TestInitState — used only by the test constructor below.
    // Allows unit tests to seed arbitrary RX/TX state without
    // driving thousands of loopback PDUs to reach wrap-around.
    // NOT intended for production use.
    // ----------------------------------------------------------
    struct TestInitState
    {
      SN_t rx_next;    // Next_PDCP_RX_SN
      uint32_t rx_hfn; // RX_HFN
      SN_t rx_deliv;   // Last_Submitted_PDCP_RX_SN
      SN_t tx_next;    // Next_PDCP_TX_SN
      uint32_t tx_hfn; // TX_HFN
    };

    // lcid    : Logical Channel ID
    // bearer  : SRB1 / SRB2 / DRB
    // rlc_mode: RLC mode below (affects SN size / discard behaviour)
    // rx_mode : which RX handler to use (default = AmNoReorder)
    explicit PdcpEntity(PdcpConfig cfg);

    // TEST-ONLY: bypass §5.2.2.1 defaults — seed arbitrary RX/TX state.
    // Delegates to normal ctor for sub-component init, then overrides
    // state variables with caller-provided values.
    PdcpEntity(PdcpConfig cfg, TestInitState init);

    ~PdcpEntity();

    // Non-copyable — owns pool references and sub-component state
    PdcpEntity(const PdcpEntity &) = delete;
    PdcpEntity &operator=(const PdcpEntity &) = delete;

    // ----------------------------------------------------------
    // Callbacks (set before first Tx/Rx)
    // ----------------------------------------------------------

    // Called when a PDU is ready to be sent down to RLC
    // Before: void setTxCallback(PduForwardCallback cb) { tx_cb_ = std::move(cb); }
    void set_lower_layer_sap(rlc_tx_upper_layer_data_sap *sap)
    {
      lower_dn_ = sap;
    }

    // Called when an SDU is ready to be delivered up to IP/RRC
    void setDeliverCallback(SduDeliverCallback cb);

    // ----------------------------------------------------------
    // Transmit path (upper → lower)
    // ----------------------------------------------------------

    // Accept an SDU from RRC/IP layer, wrap it in a PDCP PDU,
    // and invoke tx_cb_ with the serialised PDU bytes.
    Status txSdu(ByteBuffer sdu);

    // ----------------------------------------------------------
    // Receive path (lower → upper)
    // ----------------------------------------------------------

    // Accept a raw PDU from RLC.
    // due_to_reestablishment: set true for the first PDU batch
    // after an RLC re-establishment event (handover / RLF).
    // Default false keeps existing callers compile-compatible.
    Status rxPdu(ByteBuffer pdu,
                 bool due_to_reestablishment = false)
    {
      if (!pdu.valid() || pdu.size() == 0)
        return Status::PARSE_ERROR;
      return rxPduDispatch(std::move(pdu), due_to_reestablishment);
    }

    // ----------------------------------------------------------
    // Re-establishment (TS 36.323 §5.2.2)
    // ----------------------------------------------------------
    void reestablish()
    {
      if (cfg_.bearer != BearerType::DRB)
        return;
      if (cfg_.reordering_enabled)
      {
        reestablishWithReorder();
        return;
      }
      switch (cfg_.rlc_mode)
      {
      case RlcMode::AM:
        reestablishAm();
        break;
      case RlcMode::UM:
        reestablishUm();
        break;
      }
    }

    // ----------------------------------------------------------
    // State variable accessors (TS 36.323 §7.1)
    // ----------------------------------------------------------
    // Expose config via const reference, no copy
    const PdcpConfig &config() const noexcept { return cfg_; }
    BearerType bearer() const noexcept { return cfg_.bearer; }
    PdcpPduType pduType() const noexcept { return cfg_.pdu_type; }
    RlcMode rlcMode() const noexcept { return cfg_.rlc_mode; }

    SN_t txNext() const { return tx_next_; }   // Next_PDCP_TX_SN
    uint32_t txHfn() const { return tx_hfn_; } // TX_HFN

    SN_t rxNext() const { return rx_next_; }   // Next_PDCP_RX_SN
    uint32_t rxHfn() const { return rx_hfn_; } // RX_HFN
    SN_t rxDeliv() const { return rx_deliv_; } // Last_Submitted_PDCP_RX_SN

    // Compute the full 32-bit COUNT for any SN + HFN pair
    // COUNT = HFN × SN_modulus + SN  (TS 36.323 §6.3.5)
    uint32_t countValue(uint32_t hfn, SN_t sn) const
    {
      return hfn * pdcpSnModulus(pduType()) + sn;
    }

    // ----------------------------------------------------------
    // Test helpers (not part of the production interface)
    // ----------------------------------------------------------

    // Last serialised PDU built by txSdu()
    // const std::vector<uint8_t> &lastTxPdu() const { return last_tx_pdu_; }

    // Last SDU delivered by rxPdu() (populated by deliver callback)
    const std::vector<uint8_t> &lastDeliveredSdu() const { return last_delivered_sdu_; }

    // ----------------------------------------------------------
    // Control plane
    // ----------------------------------------------------------
    void discardSdu(SN_t sn);   // TS 36.323 §5.2.1
    void triggerStatusReport(); // TS 36.323 §5.5

    // ----------------------------------------------------------
    // Metrics
    // ----------------------------------------------------------
    PdcpMetrics getMetrics() const { return metrics_.snapshot(); }

  private:
    // ----------------------------------------------------------
    // Configuration
    // ----------------------------------------------------------
    PdcpConfig cfg_;

    // ----------------------------------------------------------
    // TX state variables (TS 36.323 §7.1)
    //
    //   Next_PDCP_TX_SN  — SN to assign to the next outgoing PDU
    //   TX_HFN           — upper part of COUNT on the Tx side
    // ----------------------------------------------------------
    SN_t tx_next_ = 0;
    uint32_t tx_hfn_ = 0;

    // ----------------------------------------------------------
    // RX state variables (TS 36.323 §7.1)
    //
    //   Next_PDCP_RX_SN          — next expected SN
    //   RX_HFN                   — upper part of COUNT on the Rx side
    //   Last_Submitted_PDCP_RX_SN — SN of last SDU delivered upward
    //
    // Initialised per §5.2.2.1 in normal ctor:
    //   rx_next_ = 0, rx_hfn_ = 0, rx_deliv_ = snModulus - 1
    // ----------------------------------------------------------
    SN_t rx_next_;    // Next_PDCP_RX_SN
    uint32_t rx_hfn_; // RX_HFN
    SN_t rx_deliv_;   // Last_Submitted_PDCP_RX_SN

    // ----------------------------------------------------------
    // RX reordering / re-establishment buffer
    // Key = COUNT (not SN) — see RxStoreContainer rationale in
    //       pdcp_rx_types.h
    // ----------------------------------------------------------
    RxStoreContainer rx_store_;

    // ----------------------------------------------------------
    // Sub-components (owned by entity)
    // ----------------------------------------------------------
    PdcpRohc rohc_;
    PdcpSecurity security_;
    MetricsCollector metrics_;

    // ----------------------------------------------------------
    // Latency measurement map: COUNT → tx_timestamp_ns
    //
    // Keyed by COUNT (= HFN × SN_modulus + SN) — globally unique
    // for the bearer lifetime, so no wrap-around collision.
    // Erased when the matching Rx PDU arrives.
    // ----------------------------------------------------------
    std::unordered_map<uint32_t, uint64_t> tx_ts_map_;

    // ----------------------------------------------------------
    // Callbacks
    // ----------------------------------------------------------
    rlc_tx_upper_layer_data_sap *lower_dn_ = nullptr; // PDCP -> RLC
    SduDeliverCallback deliver_cb_;                   // PDCP -> IP/RRC

    // ----------------------------------------------------------
    // Test-only storage
    // ----------------------------------------------------------
    std::vector<uint8_t> last_delivered_sdu_;

    // ----------------------------------------------------------
    // TX helper
    // ----------------------------------------------------------
    SN_t nextTxSn();

    // ----------------------------------------------------------
    // RX dispatch — called by public rxPdu()
    // ----------------------------------------------------------

    // Top-level dispatcher: routes to mode-specific handler
    Status rxPduDispatch(ByteBuffer pdu, bool due_to_reestablishment);

    // Mode-specific handlers (implemented in 3 separate .cpp files)
    Status rxPduDrbAmNoReorder(ByteBuffer pdu, bool due_to_reestablishment); // pdcp_entity_rx_am.cpp
    Status rxPduDrbUmNoReorder(ByteBuffer pdu, bool due_to_reestablishment); // pdcp_entity_rx_um.cpp
    Status rxPduWithReorder(ByteBuffer pdu, bool due_to_reestablishment);    // pdcp_entity_rx_reorder.cpp

    // Shared helpers (implemented in pdcp_entity_rx_am.cpp — used by all modes)
    RxClassification classifyRx(SN_t sn, uint32_t &out_hfn) const;
    void decipherAndDecompress(ByteBuffer &pdu, uint32_t hfn_for_decipher);

    // Re-establishment helpers (one per mode)
    void reestablishAm();                  // pdcp_entity_rx_am.cpp
    void reestablishAmWithStoredContext(); // pdcp_entity_rx_am.cpp
    void reestablishUm();                  // pdcp_entity_rx_um.cpp
    void reestablishWithReorder();         // pdcp_entity_rx_reorder.cpp
  };

} // namespace lte