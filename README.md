# lte-protocol-stack

Spec-compliant implementation of the LTE Layer-2 protocol stack (PDCP / RLC / MAC)
in C++17, per 3GPP TS 36.321, TS 36.322, and TS 36.323. Each layer measures
throughput, latency, and packet loss rate directly.

---

## Implementation Status

| Layer | Status      | Spec Reference   |
|-------|-------------|------------------|
| PDCP  | in progress | TS 36.323        |
| RLC   | in progress | TS 36.322        |
| MAC   | planned     | TS 36.321        |

---

## Architecture

+--------------------------------------------------+
|                  Application                     |
+--------------------------------------------------+
          |  SDU                        ^ SDU
          v                             |
+--------------------------------------------------+
|                 PDCP  (complete)                 |
|                                                  |
|  SN management  |  Ciphering  |  ROHC (stub)     |
|  Reordering window (AM / UM)  |  Metrics         |
+--------------------------------------------------+
          |  PDU                        ^ PDU
          v                             |
+--------------------------------------------------+
|                 RLC   (in progress)              |
|                                                  |
|  TM  |  UM (segmentation, t-Reordering)          |
|  AM  (ARQ, polling, STATUS PDU, retransmission)  |
+--------------------------------------------------+
          |  TB                         ^ TB
          v                             |
+--------------------------------------------------+
|              MAC Stub  (planned)                 |
|                                                  |
|  Transport Block scheduling  |  Loss model       |
|  TTI tick  |  HARQ simulation                    |
+--------------------------------------------------+

---

## References

- 3GPP TS 36.323 - PDCP specification
- 3GPP TS 36.322 - RLC specification
- 3GPP TS 36.321 - MAC specification
