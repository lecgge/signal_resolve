# USDE Python API Reference

## 安装

```bash
pip install .
```

## 快速开始

```python
from usde import Network

net = Network()
net.load_dbc("test_data/main.dbc")

# 解码
raw = [0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
signals = net.decode_frame(0x345, raw)
for s in signals:
    print(f"{s.name} = {s.value} {s.unit}")

# 编码
encoded = net.encode_frame(0x345, {"AMPWorkSta": 1.0})
```

---

## `class Network`

信号网络，对应一个或多个数据库文件解析后的内存模型。线程安全：每个实例拥有独立的 C++ 状态。

```python
net = Network(name="MyNetwork")
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `name` | `str` | 网络名称（可选，默认 `""`） |

### 数据库加载

#### `load_dbc(path)`

加载 CAN/CAN-FD 数据库（DBC 格式）。

```python
ok = net.load_dbc("test_data/main.dbc")
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `path` | `str` | DBC 文件路径 |
| 返回 | `bool` | 成功返回 `True` |

#### `load_ldf(path)`

加载 LIN 数据库（LDF 格式）。

```python
ok = net.load_ldf("test_data/Door.ldf")
```

#### `load_arxml(path)`

加载 AUTOSAR 系统描述文件（ARXML 格式）。支持 100MB+ 大文件。

```python
ok = net.load_arxml("test_data/test.arxml")
```

### 查询

#### `frame_count`

返回已加载的报文数量。

```python
print(net.frame_count)  # → 185
```

| 类型 | 说明 |
|------|------|
| `int` | 报文数量 |

#### `frame_info(frame_id)`

获取指定报文的元数据。

```python
info = net.frame_info(0x345)
print(info.name)   # → AMP_CFCAN_FrP01
print(info.dlc)    # → 8
for s in info.signals:
    print(s.name, s.start_bit, s.bit_length, s.byte_order)
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `frame_id` | `int` | 报文 ID（CAN ID 或 LIN PID） |
| 返回 | `FrameInfo` | 报文元数据，ID 不存在时抛出 `KeyError` |

#### `frame_ids()`

返回所有已加载的报文 ID 列表。

```python
ids = net.frame_ids()
# → [0, 1, 2, 3, ..., 0x7B9]
```

### 编解码

#### `decode_frame(frame_id, raw_bytes)`

将原始字节流解码为物理信号值（含多路复用 MUX 路由）。

```python
raw = [0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
signals = net.decode_frame(0x345, raw)
for s in signals:
    print(f"{s.name} = {s.value} {s.unit}")
# → AMPWorkSta = 1.0
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `frame_id` | `int` | 报文 ID |
| `raw_bytes` | `bytes \| bytearray \| list[int]` | 原始 CAN/LIN 字节流 |
| 返回 | `list[DecodedSignal]` | 解码后的物理信号列表 |

#### `encode_frame(frame_id, signals)`

将物理信号值编码为原始字节流。

```python
encoded = net.encode_frame(0x345, {"AMPWorkSta": 1.0})
# → [0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `frame_id` | `int` | 报文 ID |
| `signals` | `dict[str, float]` | 信号名 → 物理值 |
| 返回 | `list[int]` | DLC 长度的原始字节列表 |

---

## `class DecodedSignal`

单个已解码物理信号。

```python
for s in signals:
    print(s.name)   # → "AMPWorkSta"
    print(s.value)  # → 1.0
    print(s.unit)   # → "" (from DBC unit field)
```

| 属性 | 类型 | 说明 |
|------|------|------|
| `name` | `str` | 信号名称 |
| `value` | `float` | 物理值（已应用 factor/offset，已 clamp） |
| `unit` | `str` | 单位（来自 DBC，无单位时为空字符串） |

---

## `class FrameInfo`

报文元数据。

| 属性 | 类型 | 说明 |
|------|------|------|
| `id` | `int` | CAN ID 或 LIN PID |
| `name` | `str` | 报文名称 |
| `dlc` | `int` | 数据长度（字节数） |
| `signals` | `list[SignalInfo]` | 信号元数据列表 |

---

## `class SignalInfo`

信号元数据。

| 属性 | 类型 | 说明 |
|------|------|------|
| `name` | `str` | 信号名称 |
| `start_bit` | `int` | 起始位 |
| `bit_length` | `int` | 位长度 |
| `byte_order` | `str` | `"Intel"` 或 `"Motorola"` |
| `factor` | `float` | 线性转换系数 |
| `offset` | `float` | 线性转换偏移量 |
| `min_value` | `float` | 物理最小值 |
| `max_value` | `float` | 物理最大值 |
| `unit` | `str` | 单位 |

---

## 多版本支持

`usde/` 目录包含多个 `.pyd` 文件（`cp38` ~ `cp314`），Python `import` 自动匹配版本：

```
usde/
├── __init__.py
├── usde_python.cp38-win_amd64.pyd    # Python 3.8
├── usde_python.cp39-win_amd64.pyd    # Python 3.9
├── usde_python.cp310-win_amd64.pyd   # Python 3.10
├── usde_python.cp311-win_amd64.pyd   # Python 3.11
├── usde_python.cp312-win_amd64.pyd   # Python 3.12
├── usde_python.cp313-win_amd64.pyd   # Python 3.13
└── usde_python.cp314-win_amd64.pyd   # Python 3.14
```

导入失败时会显示可用的版本号，帮助定位问题。
