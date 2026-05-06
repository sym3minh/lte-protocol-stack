# Build & Test Guide

## Sau `cmake ..` chạy gì tiếp?

```bat
:: Bước 1 — bạn đã làm (từ trong folder build)
D:\lte-stack\build> cmake ..

:: Bước 2 — build (compile tất cả source)
D:\lte-stack\build> cmake --build . --config Debug

:: Bước 3A — chạy TẤT CẢ tests cùng lúc (verbose, thấy từng test)
D:\lte-stack\build> ctest -C Debug -V

:: Bước 3B — chạy từng suite riêng lẻ (filter theo tên)
D:\lte-stack\build\bin\Debug> lte_tests.exe --gtest_filter="PdcpLoopbackTest.*"
D:\lte-stack\build\bin\Debug> lte_tests.exe --gtest_filter="PdcpTxTest.*"
D:\lte-stack\build\bin\Debug> lte_tests.exe --gtest_filter="PdcpRxTest.*"
D:\lte-stack\build\bin\Debug> lte_tests.exe --gtest_filter="PdcpSnTest.*"
D:\lte-stack\build\bin\Debug> lte_tests.exe --gtest_filter="BufferPoolTest.*"
D:\lte-stack\build\bin\Debug> lte_tests.exe --gtest_filter="MetricsTest.*"

:: Chạy một test đơn lẻ
D:\lte-stack\build\bin\Debug> lte_tests.exe --gtest_filter="PdcpLoopbackTest.SimpleStringMinh"
```

---

## 36 Tests — Giải thích và kết quả mong đợi

### PdcpLoopbackTest — 5 tests

Kiểm tra pipeline đầu cuối: SDU vào → PDU → SDU ra phải giống nhau.

| Test | Input | Kết quả mong đợi |
|------|-------|-----------------|
| `SimpleStringMinh` | `"Minh"` (4 bytes ASCII) | `rxPdu()` deliver đúng `"Minh"` |
| `VietnameseString` | `"Xin chào thế giới"` (UTF-8 multibyte) | Deliver đúng từng byte UTF-8 |
| `BinaryPayload` | 12 bytes giả lập IP header `{0x45, 0x00, ...}` | Deliver đúng binary |
| `EmptySduIsRejected` | `nullptr, len=0` | `txSdu()` trả `PARSE_ERROR`, không crash |
| `EmptyPduIsRejected` | `nullptr, len=0` | `rxPdu()` trả `PARSE_ERROR`, không crash |

---

### PdcpTxTest — 6 tests

Kiểm tra chiều xuống (Transmit path): SN assignment, header encoding, callback.

| Test | Kiểm tra gì | Kết quả mong đợi |
|------|------------|-----------------|
| `SequenceNumberStartsAtZero` | `txNext()` khi mới tạo entity | `== 0` |
| `SequenceNumberIncrementsAfterTx` | Gửi 2 PDU | SN tăng `0 → 1 → 2` |
| `PduHeaderEncodesCorrectSN` | Đọc raw bytes của PDU đầu tiên | Byte 0 bit7=1 (D/C=Data), SN=0 |
| `PayloadPreservedInPdu` | PDU bytes từ byte 2 trở đi | Bằng đúng chuỗi `"TestPayload"` |
| `Send100Packets` | Gửi 100 SDU liên tiếp | Tất cả `Status::OK`, `txNext()==100` |
| `TxCallbackReceivesPdu` | Set callback, gửi 1 SDU | Callback nhận bytes bằng `lastTxPdu()` |

---

### PdcpRxTest — 5 tests

Kiểm tra chiều lên (Receive path): parsing, delivery, reordering.

| Test | Kiểm tra gì | Kết quả mong đợi |
|------|------------|-----------------|
| `InOrderDelivery` | Tx rồi Rx 1 PDU | `lastDeliveredSdu() == "HelloRx"` |
| `DeliverCallbackFired` | Set deliver callback | Callback nhận `"CallbackTest"` |
| `TenPacketsInOrder` | Rx 10 PDU theo thứ tự | Callback được gọi đúng 10 lần |
| `OutOfOrderReassembly` | Rx theo thứ tự: SN 0,1,3,2 | Deliver theo thứ tự đúng 0,1,2,3 — SN=3 được giữ lại cho đến khi SN=2 đến |
| `TruncatedPduRejected` | Truyền vào 1 byte `{0x80}` (thiếu byte SN thứ 2) | `rxPdu()` trả `PARSE_ERROR` |

---

### PdcpSnTest — 4 tests

Kiểm tra Sequence Number wrap-around — test quan trọng nhất về correctness của protocol.

| Test | Kiểm tra gì | Kết quả mong đợi |
|------|------------|-----------------|
| `WrapAroundAt4096` | Gửi đúng 4096 PDU (DRB, 12-bit SN) | `txNext()` quay về `0` |
| `SnAt4095BeforeWrap` | Gửi 4095 PDU rồi kiểm tra | `txNext() == 4095`, sau đó gửi thêm 1 → `txNext() == 0` |
| `PduHeaderAtMaxSn` | Đọc header của PDU thứ 4096 | Bytes encode `SN = 4095 = 0x0FFF` |
| `SrbSnWrapAt32` | Gửi 32 PDU qua SRB1 (5-bit SN) | `txNext()` quay về `0` sau 32 PDU |

---

### BufferPoolTest — 9 tests

Kiểm tra slab allocator — phần production-grade quan trọng nhất về memory.

| Test | Kiểm tra gì | Kết quả mong đợi |
|------|------------|-----------------|
| `AllocateReturnsNonNull` | Allocate 1 block | Pointer != nullptr |
| `AllocateDecrementsAvailable` | Allocate 2 rồi free 2 | `available()` đúng từng bước |
| `ExhaustionReturnsNullptr` | Pool 3 blocks, allocate 4 lần | Lần 4 trả `nullptr`, không crash; free 1 → allocate được lại |
| `BlocksAreWritable` | Ghi pattern khác nhau vào 4 blocks | Không có overlap — mỗi block giữ nguyên pattern của mình |
| `BufferGuardReleasesOnDestroy` | RAII: guard ra khỏi scope | Block tự động trả về pool, `available()` tăng lại |
| `BufferGuardReleaseTransfersOwnership` | Gọi `guard.release()` | Guard không free nữa; caller phải free thủ công |
| `ConcurrentAllocDeallocSafe` | 4 threads × 200 alloc/free | Không crash, không deadlock; cuối cùng `available() == 128` |
| `ZeroBlockSizeThrows` | `BufferPool(0, 4)` | Ném `std::invalid_argument` |
| `ZeroNumBlocksThrows` | `BufferPool(64, 0)` | Ném `std::invalid_argument` |

---

### MetricsTest — 7 tests

Kiểm tra accuracy của metrics — throughput, latency, packet loss.

| Test | Kiểm tra gì | Kết quả mong đợi |
|------|------------|-----------------|
| `InitialSnapshotIsZero` | Snapshot ngay sau khởi tạo | Tất cả counters = 0 |
| `TxCountsAccumulate` | `recordTx(100)` × 3 | `tx_packets==3`, `tx_bytes==600` |
| `RxCountsAccumulate` | 2 Rx với latency 1µs và 3µs | `avg_latency_us ≈ 2.0µs` |
| `PacketLossRateCalculated` | 9 Tx + 1 drop | `packet_loss_rate ≈ 0.10` (10%) |
| `ResetClearsAllCounters` | Tx rồi Drop rồi `reset()` | Counters về 0 |
| `ThroughputIsPositiveAfterDelay` | 1 Tx rồi sleep 10ms | `throughput_tx_bps > 0` |
| `NowNsIsMonotonicallyIncreasing` | Gọi `now_ns()` 2 lần, sleep giữa | `t2 > t1` |
