# USDE — Universal Signal Decoding Engine

跨平台、多语言汽车信号编解码引擎，核心层采用 C++20 编写。

## 项目概述

USDE 为自动化测试台架和智能座舱验证系统提供实时总线信号的 **解析（Parse）** 与 **编解码（Codec）** 能力。

- **Layer 1（Parser Layer）**: 三种独立的数据库解析器（DBC / LDF / ARXML），解析结果统一填充到 `NetworkCluster` 内存模型
- **Layer 2（Codec Engine Layer）**: 通用编解码引擎，完全不关心数据来源格式，仅根据信号元数据对原始字节流进行位运算和数学换算

## 架构设计

```
┌──────────────────────────────────────────────────────────────┐
│                     Application Layer                        │
├──────────────────────────────────────────────────────────────┤
│  Layer 2: Codec Engine                                       │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  DecodeFrame()    EncodeFrame()                        │  │
│  │  ExtractBits()    PackBits()                           │  │
│  │  RawToPhysical()  PhysicalToRaw()                      │  │
│  └────────────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────────────┤
│  Layer 1: Parser Layer                                       │
│  ┌──────────┬──────────┬──────────┬────────────────────────┐ │
│  │ DBC      │ LDF      │ ARXML    │ NetworkCluster         │ │
│  │ Parser   │ Parser   │ Parser   │   (统一内存模型)        │ │
│  └──────────┴──────────┴──────────┴────────────────────────┘ │
├──────────────────────────────────────────────────────────────┤
│  usde_types.h — Signal / Frame / NetworkCluster / ByteOrder  │
└──────────────────────────────────────────────────────────────┘
```

## 文件结构

```
signal_resolve/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── usde_types.h            # 公共数据结构: Signal, Frame, NetworkCluster
│   ├── codec_engine.h          # 编解码引擎接口 (Layer 2)
│   ├── dbc_parser.h            # DBC 解析器接口 (Layer 1)
│   ├── ldf_parser.h            # LDF 解析器接口 (Layer 1)
│   └── arxml_parser.h          # ARXML 解析器接口 (Layer 1)
├── src/
│   ├── codec_engine.cpp        # 编解码引擎实现
│   ├── dbc_parser.cpp          # DBC 解析器实现
│   ├── ldf_parser.cpp          # LDF 解析器实现
│   ├── arxml_parser.cpp        # ARXML 解析器实现
│   └── main.cpp                # 演示程序 + 测试用例
└── test_data/
    ├── main.dbc                # CAN/CAN-FD 测试文件 (93KB, 185 frames)
    ├── Door.ldf                # LIN 测试文件 (7KB, 9 frames)
    └── test.arxml              # AUTOSAR 测试文件 (28MB, 25+ frames)
```

---

## Layer 2: Codec Engine — 通用编解码引擎

**API**: `#include "codec_engine.h"`

```cpp
namespace usde {
    struct DecodedSignal {
        std::string name;
        double      physical_value;
        std::string unit;
    };

    class CodecEngine {
    public:
        // 解码: 原始字节流 -> 物理值（含 MUX 路由）
        static std::vector<DecodedSignal> DecodeFrame(
            const Frame& frame, const uint8_t* raw_bytes, size_t size);

        // 编码: 物理值 -> 原始字节流（含 MUX 路由）
        static bool EncodeFrame(
            const Frame& frame,
            const std::unordered_map<std::string, double>& signals_to_encode,
            uint8_t* out_bytes, size_t max_size);

        // 底层位运算（供单元测试）
        static uint64_t ExtractBits(const uint8_t* data, size_t size,
            uint32_t start_bit, uint32_t bit_length, ByteOrder byte_order);
        static void PackBits(uint8_t* data, size_t size,
            uint32_t start_bit, uint32_t bit_length,
            ByteOrder byte_order, uint64_t raw_value);
        static double    RawToPhysical(uint64_t raw, double factor, double offset);
        static uint64_t PhysicalToRaw(double physical, double factor, double offset);
    };
}
```

### 核心算法

#### 1. 跨字节位提取 (Bit-Stream Extractor & Packer)

汽车总线信号经常不按字节对齐（如 11 位信号跨越 3 个字节）。算法根据 `start_bit`、`bit_length` 和 `byte_order` 精准截取/写入位流。

**Intel (小端)**: `start_bit` = 信号 LSB 的绝对位位置，位序递增
```
byte[0]: bit 0~7   byte[1]: bit 8~15   byte[2]: bit 16~23
         LSB ──────────────────────────────────────> MSB
```

**Motorola (大端)**: `start_bit` = 信号 MSB 的 DBC 位编号，位序递减（DBC 标准：bit 0 = byte.MSB, bit 7 = byte.LSB）
```
byte[0]: bit 0(MSB)~7(LSB)   byte[1]: bit 8(MSB)~15(LSB)
         MSB <────────────────────────────────────── LSB
```

#### 2. 物理值线性转换 (Linear Transformer)

```
解码: Physical = Raw × Factor + Offset
编码: Raw = round((Physical - Offset) / Factor)
```

编码/解码后均进行 `std::clamp` 边界裁剪，确保不超过信号定义的 `min_value` / `max_value`。

#### 3. 多路复用路由 (Multiplexing Router)

```
Step 1: 扫描 is_mux_decoder == true 的信号，提取 MUX 选择器原始值
Step 2: 遍历所有信号:
        - 跳过 is_mux_decoder 信号本身
        - 跳过 is_multiplexed == true && mux_value != 选择器值 的信号
        - 解码/编码剩余信号
```

### 设计约束

- **零动态内存分配**: `DecodeFrame` / `EncodeFrame` 底层位运算不使用 `new`/`delete` 或 `std::vector` 扩容
- **严格边界检查**: 对 `size` 进行校验，防止越界读写；处理 `bit_length > 64` 等极端情况
- **线程安全**: 所有方法均为无状态的 `static` 函数

---

## Layer 1: Parser Layer — 数据库解析层

### 1. DBC 解析器 — CAN/CAN-FD

**API**: `bool usde::LoadDBC(const std::filesystem::path&, NetworkCluster&)`

- 逐行流式读取，正则无关的 token 化解析
- 使用 `std::from_chars` 高性能数值转换
- 支持 Motorola (`@0`) 和 Intel (`@1`) 字节序、复用信号标记 (`M`/`m`)

**解析结果**: 185 个报文，800+ 个信号

### 2. LDF 解析器 — LIN

**API**: `bool usde::LoadLDF(const std::filesystem::path&, NetworkCluster&)`

- 递归下降状态机，支持 `/* */` 和 `//` 注释剥离
- `ExtractBlock()` 按关键字定位顶层文本块
- 自动识别十六进制/十进制帧 ID，通过 `Signal_encoding_types` 映射物理量

**解析结果**: 9 个报文，21 个信号

### 3. ARXML 解析器 — AUTOSAR

**API**: `bool usde::LoadARXML(const std::filesystem::path&, NetworkCluster&)`

- 内存高效方案: 整体加载后使用 `std::string::find`（SIMD 优化）定位目标标签
- 解析链路: `I-SIGNAL` → `I-SIGNAL-TO-I-PDU-MAPPING` → `PDU-TO-FRAME-MAPPING` → `CAN-FRAME-TRIGGERING` → CAN ID
- 支持 AUTOSAR R4.0 命名空间、多种 PDU 类型

**解析结果**: 25 个报文，200+ 个信号（28MB 文件 < 2s）

---

## 统一内存模型

```cpp
namespace usde {
    enum class ByteOrder : uint8_t { INTEL = 0, MOTOROLA = 1 };

    struct Signal {
        std::string name;
        uint32_t start_bit   = 0;    // 起始位
        uint32_t bit_length  = 0;    // 位长度
        ByteOrder byte_order = ByteOrder::INTEL;
        double factor        = 1.0;  // 系数
        double offset        = 0.0;  // 偏移量
        double min_value     = 0.0;
        double max_value     = 0.0;
        std::string unit;
        bool     is_multiplexed  = false;  // 是否多路复用受控信号
        uint32_t mux_value       = 0;      // 对应的复用值
        bool     is_mux_decoder  = false;  // 是否是 MUX 选择器信号
    };

    struct Frame {
        uint32_t id   = 0;   // CAN ID / LIN PID
        uint32_t dlc  = 0;   // Data Length Code (bytes)
        std::string name;
        std::vector<Signal> signals;
    };

    struct NetworkCluster {
        std::string name;
        std::unordered_map<uint32_t, Frame> frames;   // Key: Frame ID
    };
}
```

## 构建与运行

```bash
# CMake
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 运行
./build/Release/usde_demo.exe
```

输出包含两部分：
1. **Parser Demo**: 解析 `test_data/` 中的真实 DBC/LDF/ARXML 文件
2. **Codec Tests**: 6 项编解码验证测试（Intel/Motorola 跨字节、MUX 路由、编码-解码往返、边界条件）

## 编码规范

- C++20 标准（`std::filesystem::path`, `std::string_view`, `std::llround`）
- 线程安全：每次调用使用独立局部状态
- 完善的错误处理：语法错误时返回 `false`
- MSVC `/utf-8` 编译选项

## 验证结果

三层架构已通过端到端验证：

| 层级 | 测试数 | 结果 |
|------|--------|------|
| C++ Codec Engine | 8 | 8 PASS |
| Python (pybind11) | 24 | 24 PASS |
| Java (JNA) | 9 | 9 PASS |

验证覆盖：
- Intel/Motorola 字节序的跨字节位提取与打包
- 线性变换（factor/offset）的编码-解码往返
- 多路复用（MUX）路由逻辑
- 边界值（60-bit offset、零长度信号）

## 后续规划

- Layer 4: 实时信号监控与异常检测
