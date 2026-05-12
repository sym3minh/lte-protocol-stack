#include "pdcp_entity.h"
#include "pdcp_rx_am_noreorder.h"
#include "pdcp_rx_um_noreorder.h"
#include "pdcp_rx_with_reorder.h"

#include <cstring>
#include <cassert>
#include <stdexcept>

namespace lte {

// ============================================================
// Constructor
//
// Builds the Deps struct from this entity's sub-components
// and creates the appropriate concrete procedure via factory
// switch.  The procedure holds non-owning references to
// security_, rohc_, metrics_, pool_, and deliver_cb_.
// Lifetime is safe because entity owns procedure via unique_ptr:
// procedure destructs before entity's members.
// ============================================================
PdcpEntity::PdcpEntity(LCID_t      lcid,
                        BearerType  bearer,
                        RlcMode     rlc_mode,
                        BufferPool& pool,
                        RxMode      rx_mode)
    : lcid_(lcid)
    , bearer_(bearer)
    , rlc_mode_(rlc_mode)
    , pool_(pool)
{
    // deliver_cb_ is not set yet (caller calls setDeliverCallback later),
    // so we capture it by reference through a lambda wrapper so the
    // procedure always sees the most-recently-set callback.
    // The lambda is stored in the Deps struct and called by the procedure.
    RxDeliverCallback proc_deliver = [this](const uint8_t* sdu, size_t len) {
        // Store for test inspection
        last_delivered_sdu_.assign(sdu, sdu + len);
        // Forward to user-set callback
        if (deliver_cb_) {
            deliver_cb_(sdu, len);
        }
    };

    IPdcpRxProcedure::Deps deps{
        security_,
        rohc_,
        metrics_,
        pool_,
        std::move(proc_deliver),//std::function supports move constructor
        bearer_
    };

    switch (rx_mode) {
        case RxMode::AmNoReorder:
            rx_proc_ = std::make_unique<PdcpRxAmNoReorder>(deps);
            break;
        case RxMode::UmNoReorder:
            rx_proc_ = std::make_unique<PdcpRxUmNoReorder>(deps);
            break;
        case RxMode::WithReorder:
            rx_proc_ = std::make_unique<PdcpRxWithReorder>(deps);
            break;
        default:
            throw std::invalid_argument("Unknown RxMode");
    }
}

PdcpEntity::~PdcpEntity() = default;

// ============================================================
// setDeliverCallback
//
// Stored here on the entity; the procedure's deliver lambda
// (captured by reference to deliver_cb_) will automatically
// use the newly-set callback on the next rxPdu() call.
// ============================================================
void PdcpEntity::setDeliverCallback(SduDeliverCallback cb)
{
    deliver_cb_ = std::move(cb);
}

// ============================================================
// Transmit path — TS 36.323 §5.1.1
//
// Steps per spec:
//   1. Assign Next_PDCP_TX_SN as SN for this SDU
//   2. RoHC header compression (stub: pass-through)
//   3. Integrity protection then ciphering using
//      COUNT = TX_HFN × modulus + SN  (§6.3.5)
//   4. Build PDCP Data PDU
//   5. Increment Next_PDCP_TX_SN; if > Maximum_PDCP_SN → wrap + TX_HFN++
//   6. Submit PDU to lower layer
//
// TX path is unchanged from the previous revision — no logic
// related to §5.1.2.1.x lives here.
// ============================================================
Status PdcpEntity::txSdu(const uint8_t* sdu, size_t sdu_len)
{
    if (!sdu || sdu_len == 0 || sdu_len > PDCP_MAX_SDU_SIZE) return Status::PARSE_ERROR;

    const size_t header_sz = PdcpPduCodec::headerSize(bearer_);
    const size_t needed    = header_sz + sdu_len + 4; // +4 future MAC-I

    if (needed > pool_.blockSize()) return Status::POOL_EXHAUSTED;

    uint8_t* block = pool_.allocate();
    if (!block) {
        metrics_.recordDrop();
        return Status::POOL_EXHAUSTED;
    }

    // Place SDU right after where the header will go
    std::memcpy(block + header_sz, sdu, sdu_len);
    size_t payload_len = sdu_len;

    // Step 2: RoHC (stub: no-op) => just affect the Header of SDU
    rohc_.compress(block + header_sz, payload_len);

    // Step 3: ciphering using COUNT = TX_HFN × modulus + tx_next_ => affect the entire SDU (both the compressed header and body)
    const bool is_srb = (bearer_ != BearerType::DRB);
    security_.applyCiphering(block + header_sz, payload_len, tx_next_, is_srb);

    // Step 4: record tx timestamp keyed by COUNT before SN advances
    const uint32_t tx_count = countValue(tx_hfn_, tx_next_);
    tx_ts_map_[tx_count] = metrics_.now_ns();

    PdcpPdu pdu;
    pdu.sn          = tx_next_;
    pdu.dc          = PDCP_DC_DATA;
    pdu.bearer      = bearer_;
    pdu.payload     = block + header_sz;
    pdu.payload_len = payload_len;

    size_t serialised_len = PdcpPduCodec::serialize(pdu, block, pool_.blockSize());
    if (serialised_len == 0) {
        pool_.deallocate(block);
        return Status::PARSE_ERROR;
    }

    // Step 5: advance Next_PDCP_TX_SN + TX_HFN per spec §5.1.1
    const SN_t max_sn = static_cast<SN_t>(snModulus(bearer_) - 1);
    if (tx_next_ >= max_sn) {
        tx_next_ = 0;
        ++tx_hfn_;
    } else {
        ++tx_next_;
    }

    metrics_.recordTx(serialised_len);
    last_tx_pdu_.assign(block, block + serialised_len);

    if (tx_cb_) tx_cb_(block, serialised_len);

    pool_.deallocate(block);
    return Status::OK;
}

// ============================================================
// Control plane — stubs
// ============================================================
void PdcpEntity::discardSdu(SN_t sn)
{
    // TODO: cancel discardTimer for sn, remove from Tx buffer
    (void)sn;
}

void PdcpEntity::triggerStatusReport()
{
    // TODO: build PDCP Control PDU (D/C=0) per §5.3 and submit to RLC
}

// ============================================================
// TX SN helper
// ============================================================
SN_t PdcpEntity::nextTxSn()
{
    // Kept for potential future use; txSdu() manages SN inline.
    SN_t sn = tx_next_;
    const SN_t max_sn = static_cast<SN_t>(snModulus(bearer_) - 1);
    if (tx_next_ >= max_sn) { tx_next_ = 0; ++tx_hfn_; }
    else                    { ++tx_next_; }
    return sn;
}

} // namespace lte
