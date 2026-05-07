# Production Test Structure Guide — LTE Protocol Stack

Tài liệu này tổng hợp các thay đổi cần áp dụng vào project hiện tại để tổ chức unit test theo chuẩn production-grade. Áp dụng được ngay với cấu trúc PDCP hiện có và scale tốt khi RLC, MAC được thêm vào sau.

---

## 1. Phân chia thư mục test

Cấu trúc hiện tại (`tests/` flat ở root) sẽ không scale khi project lớn lên. Cách tổ chức production phổ biến (srsRAN, OAI, ns-3 đều dùng pattern này) là **mirror cấu trúc `src/` vào `tests/`**, mỗi module có sub-directory riêng.

### Cấu trúc đề xuất

```
source/
├── src/
│   ├── common/
│   ├── pdcp/
│   ├── rlc/          (sắp tới)
│   └── mac/          (sắp tới)
└── tests/
    ├── CMakeLists.txt
    ├── cmake/
    │   └── AddUnitTest.cmake          ← Helper function dùng chung
    ├── unit/                          ← Unit tests (cô lập, fast, <100ms/test)
    │   ├── common/
    │   │   ├── CMakeLists.txt
    │   │   ├── buffer_pool_test.cpp
    │   │   └── metrics_test.cpp
    │   ├── pdcp/
    │   │   ├── CMakeLists.txt
    │   │   ├── pdcp_pdu_test.cpp
    │   │   ├── pdcp_tx_test.cpp
    │   │   ├── pdcp_rx_am_noreorder_test.cpp
    │   │   ├── pdcp_security_test.cpp
    │   │   └── pdcp_rohc_test.cpp
    │   ├── rlc/
    │   └── mac/
    ├── integration/                   ← Cross-layer tests (PDCP↔RLC↔MAC)
    │   ├── CMakeLists.txt
    │   ├── pdcp_loopback_test.cpp
    │   └── pdcp_rlc_integration_test.cpp
    ├── benchmarks/                    ← Performance/perf regression
    │   ├── buffer_pool_bench.cpp
    │   └── pdcp_throughput_bench.cpp
    ├── fixtures/                      ← Shared test helpers, mocks, test data
    │   ├── include/
    │   │   ├── mock_rlc.h
    │   │   ├── mock_lower_layer.h
    │   │   ├── pdu_generator.h
    │   │   └── test_helpers.h
    │   └── src/
    │       └── pdu_generator.cpp
    └── data/                          ← Test vectors (PCAP, hex dumps, 3GPP test vectors)
        ├── pdcp/
        └── rlc/
```

### Vì sao chia 2 tầng `unit/` và `integration/`?

Đây là **test pyramid** chuẩn — mỗi tầng có đặc tính khác nhau:

- **Unit tests**: chạy nhanh (<1s tổng), không phụ thuộc network/file/timing thật, được chạy mỗi lần save file. Mock tất cả dependency.
- **Integration tests**: chậm hơn (giây), test interaction giữa 2-3 layer (ví dụ PDCP gửi xuống RLC mock, rồi RLC thật xử lý). Chạy trước khi push.

Điều này quan trọng cho telecom vì test throughput thật sự cần chạy nhiều giây để có số liệu ổn định — không nên chạy chung với unit test mỗi lần Ctrl+S.

### Vai trò của `fixtures/`

Khi RLC/MAC vào project, nhiều test sẽ cần generate PDU, mock lower layer, mock upper layer. Để code dùng chung ở đây, không copy-paste giữa các test file.

---

## 2. Tổ chức GoogleTest khi số lượng test lớn

Khi có 50-100+ test files, làm 1 executable duy nhất là sai lầm phổ biến.

### Pattern: Một test executable per module (không phải per-file)

| Strategy | Build time | Run granularity | Recommend |
|---|---|---|---|
| 1 executable cho TẤT CẢ test | Chậm khi link, một file đổi link lại hết | Phải dùng `--gtest_filter` | ❌ Không nên |
| 1 executable per file (.cpp) | Link rất chậm, nhiều binary | Chạy được từng file riêng | ⚠️ Chỉ khi test rất isolate |
| **1 executable per module** | **Nhanh nhất, link song song** | **Chạy nhóm theo module** | ✅ **Production standard** |

Trong CMake, mỗi sub-folder (`tests/unit/pdcp/`) tạo 1 executable. `tests/CMakeLists.txt` ở root chỉ làm orchestration.

### Helper function để giảm boilerplate

Khi có nhiều test target, không lặp `add_executable + target_link_libraries + gtest_discover_tests` mỗi lần. Viết 1 function CMake dùng chung:

```cmake
# tests/cmake/AddUnitTest.cmake


Sau đó mỗi `CMakeLists.txt` con chỉ cần:

```cmake
# tests/unit/pdcp/CMakeLists.txt
lte_add_unit_test(
    NAME    pdcp_tx_test
    SOURCES pdcp_tx_test.cpp
    LIBS    pdcp_lib
    LABELS  unit;pdcp
)
```

### `gtest_discover_tests` vs `add_test` — quan trọng

Dùng `gtest_discover_tests` (CMake ≥ 3.10), **không phải** `add_test`:

- `add_test`: CTest chỉ thấy 1 test "pdcp_tx_test" — dù bên trong có 50 `TEST_F`. Failure 1 test = cả binary đỏ.
- `gtest_discover_tests`: CTest thấy từng `TEST_F` riêng lẻ → có thể chạy `ctest -R PdcpTx.HandlesEmptySdu` để chạy 1 test duy nhất, parallel tốt hơn, report đẹp hơn.

---

## 3. Có cần build lại tất cả không?

**Không.** Đây là điểm mạnh của kiến trúc multi-target.

### Build & chạy theo nhóm

```bash
# Build CHỈ tests của PDCP (không đụng RLC, MAC, common)
cmake --build build --target pdcp_tx_test pdcp_rx_am_noreorder_test pdcp_security_test

# Hoặc gom thành alias target (xem dưới)
cmake --build build --target pdcp_tests

# Chạy chỉ test PDCP — qua label
ctest --test-dir build -L pdcp --output-on-failure

# Chạy unit tests, không integration
ctest --test-dir build -L unit -j8

# Chạy 1 test case duy nhất trong file
ctest --test-dir build -R "PdcpTx.HandlesEmptySdu"

# Chạy mọi thứ trừ benchmarks
ctest --test-dir build -LE benchmark
```

### Tạo meta target cho từng nhóm

Trong `tests/unit/pdcp/CMakeLists.txt`, gom lại:

```cmake
add_custom_target(pdcp_tests
    DEPENDS
        pdcp_tx_test
        pdcp_rx_am_noreorder_test
        pdcp_security_test
        pdcp_pdu_test
)
```

Giờ `cmake --build build --target pdcp_tests` chỉ build những gì liên quan PDCP.

### Vì sao "chỉ build cái cần" hoạt động?

CMake/Ninja sẽ tính dependency graph. Nếu sửa `pdcp_entity.cpp`:

- `pdcp_lib` rebuild (chỉ file đó + link lại lib).
- Các test executable link với `pdcp_lib` rebuild (chỉ link, không recompile test source).
- `common_lib`, `rlc_lib`, `mac_lib` **không bị đụng tới**.

Đây là lý do mỗi module phải là **library riêng** (`pdcp_lib`, `rlc_lib`, `common_lib`...) — không phải `add_executable(test ${ALL_SOURCES})`. Project hiện tại đã làm đúng phần này.

### Dùng Ninja, không phải Make

```bash
cmake -S source -B build -G Ninja
cmake --build build -j
```

Ninja parallel tốt hơn Make rất nhiều cho project nhiều target nhỏ.

---

## 4. Checklist áp dụng cho project hiện tại

Thứ tự áp dụng:

1. Refactor `tests/` thành `tests/unit/{module}/`, `tests/integration/`, `tests/fixtures/`.
2. Viết `lte_add_unit_test()` helper trong `tests/cmake/AddUnitTest.cmake`.
3. Dùng `gtest_discover_tests` thay vì `add_test`.
4. Gắn `LABELS` cho mỗi test (`unit`, `pdcp`, `rlc`...) để filter qua `ctest -L`.
5. Tạo meta-target `pdcp_tests`, `all_unit_tests` để build theo nhóm.
6. Move `pdcp_loopback_test.cpp` từ `tests/` sang `tests/integration/` (đây là cross-layer test, không phải unit test).
