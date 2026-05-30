# lte-protocol-stack

Spec-compliant implementation of the LTE Layer-2 protocol stack (PDCP / RLC / MAC)
in C++17, per 3GPP TS 36.321, TS 36.322, and TS 36.323. Each layer measures
throughput, latency, and packet loss rate directly — no simulation framework dependency.

---

## Implementation Status

| Layer | Status      | Spec Reference   |
|-------|-------------|------------------|
| PDCP  | in progress | TS 36.323        |
| RLC   | in progress | TS 36.322        |
| MAC   | planned     | TS 36.321        |

---

## Architecture

```
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
```

## Building

**Requirements:** CMake >= 3.20, a C++17-capable compiler, GoogleTest 1.14.

Clone GoogleTest and point `SOURCE_DIR` in the root `CMakeLists.txt` to your local path,
then:

```bash
# Linux
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

# Windows (MSVC)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Run a specific layer's tests:

```bash
ctest --test-dir build -L pdcp --output-on-failure
ctest --test-dir build -L rlc  --output-on-failure
ctest --test-dir build -L unit --output-on-failure
```

---

## References

- 3GPP TS 36.323 — PDCP specification
- 3GPP TS 36.322 — RLC specification
- 3GPP TS 36.321 — MAC specification
