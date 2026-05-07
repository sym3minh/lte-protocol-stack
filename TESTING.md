# Build & Test Guide

## Bước 1 — Configure

```bat
D:\lte-stack> cmake -S source -B build -G Ninja
```

> Dùng Ninja thay Make để build song song nhanh hơn.  
> Nếu chưa có Ninja, dùng generator mặc định: bỏ `-G Ninja`.

---

## Bước 2 — Build

```bat
:: Build toàn bộ project
D:\lte-stack> cmake --build build

:: Build chỉ một nhóm (không đụng tới module khác)
D:\lte-stack> cmake --build build --target pdcp_unit_tests
D:\lte-stack> cmake --build build --target common_unit_tests
D:\lte-stack> cmake --build build --target integration_tests

:: Build tất cả unit tests
D:\lte-stack> cmake --build build --target all_unit_tests

:: Build mọi thứ (unit + integration)
D:\lte-stack> cmake --build build --target all_tests
```

---

## Bước 3 — Chạy tests

### Chạy tất cả

```bat
D:\lte-stack> ctest --test-dir build --output-on-failure
```

### Lọc theo layer / loại test

```bat
:: Chỉ unit tests (bỏ qua integration)
D:\lte-stack> ctest --test-dir build -L unit --output-on-failure

:: Chỉ PDCP tests (unit + integration)
D:\lte-stack> ctest --test-dir build -L pdcp --output-on-failure

:: Chỉ common (BufferPool, Metrics)
D:\lte-stack> ctest --test-dir build -L common --output-on-failure

:: Chỉ integration tests
D:\lte-stack> ctest --test-dir build -L integration --output-on-failure

:: Tất cả trừ integration
D:\lte-stack> ctest --test-dir build -LE integration --output-on-failure
```

### Chạy song song

```bat
D:\lte-stack> ctest --test-dir build -L unit -j8 --output-on-failure
```

### Lọc đến từng test case

```bat
:: Tất cả test trong một fixture
D:\lte-stack> ctest --test-dir build -R "PdcpTxTest\." --output-on-failure
D:\lte-stack> ctest --test-dir build -R "ProcFixture\." --output-on-failure
D:\lte-stack> ctest --test-dir build -R "BufferPoolTest\." --output-on-failure
D:\lte-stack> ctest --test-dir build -R "MetricsTest\." --output-on-failure
D:\lte-stack> ctest --test-dir build -R "PdcpLoopbackTest\." --output-on-failure

:: Một test đơn lẻ
D:\lte-stack> ctest --test-dir build -R "PdcpTxTest\.SequenceNumberStartsAtZero"
D:\lte-stack> ctest --test-dir build -R "PdcpLoopbackTest\.SimpleStringMinh"
```

### Chạy binary trực tiếp (verbose output từ GoogleTest)

```bat
D:\lte-stack\build\bin> pdcp_tx_test.exe
D:\lte-stack\build\bin> pdcp_rx_am_noreorder_test.exe
D:\lte-stack\build\bin> buffer_pool_test.exe
D:\lte-stack\build\bin> metrics_test.exe
D:\lte-stack\build\bin> pdcp_loopback_test.exe

:: Với gtest filter
D:\lte-stack\build\bin> pdcp_rx_am_noreorder_test.exe --gtest_filter="ProcFixture.*"
D:\lte-stack\build\bin> pdcp_rx_am_noreorder_test.exe --gtest_filter="EntityFixture.*"
```

---

## Tổng hợp label

| Label         | Nội dung                                      |
|---------------|-----------------------------------------------|
| `unit`        | Tất cả unit tests                             |
| `integration` | Cross-layer tests (loopback, PDCP↔RLC...)     |
| `common`      | BufferPool, MetricsCollector                  |
| `pdcp`        | Tất cả tests liên quan PDCP                   |