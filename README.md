# USDE — Universal Signal Decoding Engine

跨平台、多语言汽车信号编解码引擎的核心 C++20 解析层（Layer 1: Parser Layer）。

## 项目概述

USDE 为自动化测试台架和智能座舱验证系统提供实时总线信号解析能力。本模块实现三种独立的数据库解析器，分别处理 CAN/CAN-FD、LIN 和 AUTOSAR 三种汽车总线格式，解析结果统一填充到通用内存模型中。

## 架构设计

```
┌─────────────────────────────────────────────────┐
│              Application Layer                   │
├──────────┬──────────┬──────────┬────────────────┤
│ DBC      │ LDF      │ ARXML    │ NetworkCluster │
│ Parser   │ Parser   │ Parser   │   (统一输出)    │
├──────────┴──────────┴──────────┴────────────────┤
│              usde_types.h (公共结构体)            │
└─────────────────────────────────────────────────┘
```

三种解析器完全独立，严禁耦合解析逻辑，仅共享输出数据结构。

## 文件结构

```
signal_resolve/
├── CMakeLists.txt              # CMake 构建配置 (C++20)
├── README.md
├── include/
│   ├── usde_types.h            # 公共数据结构: Signal, Frame, NetworkCluster
│   ├── dbc_parser.h            # DBC 解析器接口
│   ├── ldf_parser.h            # LDF 解析器接口
│   └── arxml_parser.h          # ARXML 解析器接口
├── src/
│   ├── dbc_parser.cpp          # DBC 解析器实现
│   ├── ldf_parser.cpp          # LDF 解析器实现
│   ├── arxml_parser.cpp        # ARXML 解析器实现
│   └── main.cpp                # 演示程序
└── test_data/
    ├── main.dbc                # CAN/CAN-FD 测试文件 (93KB, 185 frames)
    ├── Door.ldf                # LIN 测试文件 (7KB, 9 frames)
    └── test.arxml              # AUTOSAR 测试文件 (28MB, 25+ frames)
```

## 三种解析器详细设计

### 1. DBC 解析器 — CAN/CAN-FD

**API**: `bool usde::LoadDBC(const std::filesystem::path&, NetworkCluster&)`

**算法策略**:
- 逐行流式读取 (`std::ifstream`)，逐行解析 `BO_`（报文）和 `SG_`（信号）定义
- 正则无关的 token 化解析（避免 MSVC 正则引擎对 `@` 字符的兼容性问题）
- 使用 `std::from_chars` 进行高性能数值转换
- 支持 Motorola (Big-Endian `@0`) 和 Intel (Little-Endian `@1`) 字节序
- 支持复用信号标记 (`M`/`m`)

**解析结果**: 185 个报文，800+ 个信号

### 2. LDF 解析器 — LIN

**API**: `bool usde::LoadLDF(const std::filesystem::path&, NetworkCluster&)`

**算法策略**:
- 递归下降状态机，支持 `/* */` 和 `//` 注释剥离
- `ExtractBlock()` 函数按关键字定位顶层文本块（`Signals {}`, `Frames {}` 等）
- 自动识别十六进制 (`0x3C`) 和十进制 (`3`) 帧 ID
- 通过 `Signal_encoding_types` 和 `Signal_representation` 映射物理量（factor/offset/unit）

**解析结果**: 9 个报文，21 个信号

### 3. ARXML 解析器 — AUTOSAR

**API**: `bool usde::LoadARXML(const std::filesystem::path&, NetworkCluster&)`

**算法策略**:
- **内存高效方案**: 将文件整体加载后使用 `std::string::find`（SIMD 优化）定位目标标签，跳过无关元素
- 解析链路: `I-SIGNAL` → `I-SIGNAL-TO-I-PDU-MAPPING` → `PDU-TO-FRAME-MAPPING` → `CAN-FRAME-TRIGGERING` → CAN ID
- 支持 AUTOSAR R4.0 命名空间 (`AR:` 前缀自动剥离)
- 支持多种 PDU 类型: `NM-PDU`, `I-PDU`, `SECURED-I-PDU`, `CONTAINER-I-PDU`
- 支持 `MOST-SIGNIFICANT-BYTE-FIRST` / `MOST-SIGNIFICANT-BYTE-LAST` 字节序

**解析结果**: 25 个报文，200+ 个信号（28MB 文件 < 2s）

## 构建

```bash
# CMake
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 或直接使用 MSVC
cl /std:c++20 /utf-8 /EHsc src/*.cpp /I include /Fe:usde_demo.exe
```

## 运行

```bash
./build/Release/usde_demo.exe
```

输出示例:
```
=== DBC Parser (CAN/CAN-FD) ===
Cluster : CFCAN
Frames  : 185
  Frame 0x345  [AMP_CFCAN_FrP01]  DLC=8  Signals=1
    SG_ AMPWorkSta  bit=15  len=1  Motorola  factor=1  offset=0  [0..1]  ""

=== LDF Parser (LIN) ===
Cluster : Door_LIN
Frames  : 9
  Frame 0x3  [DWFL_01]  DLC=2  Signals=2
    SG_ DWFL_WinPos  bit=0  len=8  Intel  factor=1  offset=0  [0..0]  ""

=== ARXML Parser (AUTOSAR) ===
Cluster : AUTOSAR_ZXD
Frames  : 25
  Frame 0x4c0  [NmPDU_ADCANFD_CCP]  DLC=8  Signals=10
    SG_ isGW_NM_BSMtoRMS_AD  bit=16  len=1  Motorola  factor=1  offset=0  [0..0]  ""
```

## 统一输出内存模型

```cpp
namespace usde {
    enum class ByteOrder { INTEL, MOTOROLA };

    struct Signal {
        std::string name;
        uint32_t start_bit, bit_length;
        ByteOrder byte_order;
        double factor, offset, min_value, max_value;
        std::string unit;
        bool is_multiplexed;  uint32_t mux_value;
    };

    struct Frame {
        uint32_t id, dlc;
        std::string name;
        std::vector<Signal> signals;
    };

    struct NetworkCluster {
        std::string name;
        std::unordered_map<uint32_t, Frame> frames;
    };
}
```

## 编码规范

- C++20 标准 (`std::filesystem::path`, `std::string_view`)
- 线程安全：每次调用使用独立局部状态
- 完善的错误处理：语法错误时返回 `false`
- MSVC `/utf-8` 编译选项支持中文注释

## 后续规划

- Layer 2: pybind11 Python 桥接
- Layer 3: JNA Java 桥接
- Layer 4: 实时信号解码引擎（运行时 factor/offset 换算）
