# Raspberry Pi 5 DWC I2C 驱动分析文档

## 目录
1. [概述](#概述)
2. [驱动架构](#驱动架构)
3. [设计原理](#设计原理)
4. [核心流程](#核心流程)
5. [关键数据结构](#关键数据结构)
6. [功能模块分析](#功能模块分析)
7. [中断处理机制](#中断处理机制)
8. [时钟与速度配置](#时钟与速度配置)
9. [错误处理](#错误处理)
10. [设备初始化流程](#设备初始化流程)

---

## 概述

### 驱动基本信息
- **驱动名称**: DWC I2C Master Driver（Designware I2C驱动）
- **版本**: 1.0（实验版本 SQML Level 1）
- **目标平台**: Raspberry Pi 5 (BCM2712)
- **开发方**: BlackBerry Limited
- **许可证**: Apache License 2.0
- **编程语言**: C
- **架构**: ARM 64-bit (aarch64)

### 驱动功能
该驱动为树莓派5在QNX操作系统上提供I2C主控模式的支持，支持：
- I2C总线通信（主控模式）
- 标准速率（100KHz）和快速速率（400KHz）
- 7位和10位从设备地址格式
- 中断驱动的数据传输
- 完整的错误检测和恢复机制

---

## 驱动架构

### 分层架构设计

```
┌─────────────────────────────────────────┐
│        应用层 (I2C应用程序)              │
│     调用 send/recv/set_speed 接口       │
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│    中间件层 (I2C Master 抽象接口)       │
│  - 发送操作 (dwc_i2c_send)              │
│  - 接收操作 (dwc_i2c_recv)              │
│  - 复合操作 (dwc_i2c_sendrecv)         │
│  - 速度设置 (dwc_i2c_set_bus_speed)    │
│  - 地址配置 (dwc_i2c_set_slave_addr)   │
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│      协议层 (DWC I2C控制器驱动)         │
│  - 总线管理 (dwc_i2c_bus_active)       │
│  - 等待机制 (dwc_i2c_wait_complete)    │
│  - 中断处理 (dwc_i2c_process_intr)     │
│  - 寄存器配置 (dwc_i2c_init_registers) │
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│       硬件抽象层 (HAL)                  │
│  - 寄存器读写 (i2c_reg_read32/write32)  │
│  - 内存映射 (mmap_device_memory)       │
│  - 中断附加 (InterruptAttachEvent)     │
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│      物理硬件 (DWC I2C控制器)           │
│     - I2C总线 (SDA/SCL)                 │
│     - 寄存器组                          │
│     - 中断线路                          │
└─────────────────────────────────────────┘
```

### 核心组件

| 组件 | 文件 | 功能 |
|------|------|------|
| 初始化管理 | `init.c` | 驱动初始化、寄存器配置、中断附加 |
| 收发控制 | `send.c`, `recv.c`, `sendrecv.c` | I2C数据发送与接收 |
| 等待机制 | `wait.c` | 事务完成等待、中断处理 |
| 时钟配置 | `clock.c` | 输入时钟获取 |
| 速度控制 | `bus_speed.c` | I2C总线速率设置 |
| 地址配置 | `slave_addr.c` | 从设备地址设置 |
| 库函数 | `lib.c` | 驱动接口注册 |
| 驱动信息 | `info.c`, `version.c` | 驱动版本和信息查询 |
| 清理管理 | `detach.c` | 驱动清理卸载 |

---

## 设计原理

### 1. 设备抽象

驱动通过 `dwc_dev_t` 结构体对I2C控制器进行了完整的抽象，包含：
- **硬件接口信息**：物理地址、虚拟地址、内存大小、中断号
- **时钟参数**：输入时钟频率、SCL时序参数
- **从设备配置**：从地址、地址格式（7/10位）
- **传输信息**：发送/接收缓冲区、数据长度、FIFO操作计数
- **状态管理**：中断状态、错误标志、总线状态

### 2. 中断驱动型设计

驱动采用中断驱动的异步传输模式：
- **TX_EMPTY中断**：FIFO为空时填充数据
- **RX_FULL中断**：FIFO达到阈值时读取数据
- **STOP_DET中断**：I2C停止信号检测到事务完成
- **TX_ABRT中断**：检测错误（地址无应答、数据无应答等）

### 3. 事务状态管理

```
初始化状态 → 总线检查 → 总线激活 → 中断启用 → 数据传输
                                        ↓
                                   中断处理循环
                                        ↓
                                   事务完成 → 清理
```

### 4. 时序参数设计

I2C时序受多个因素影响：
- **输入时钟**：200MHz（树莫派5）
- **速率选择**：标准模式(100kHz) 或 快速模式(400kHz)
- **时序容限**：tHIGH、tLOW、tHD;STA、tF等

驱动采用数学公式计算最优的HCNT/LCNT值：

```
HCNT = (IC_CLK * tSYMBOL) / 1000000 - offset
LCNT = (IC_CLK * (tLOW + tF)) / 1000000 - 1 + offset
```

---

## 核心流程

### 1. 驱动初始化流程图

```
dwc_i2c_init(argc, argv)
    ↓
解析命令行选项 (dwc_i2c_parseopts)
    ↓
申请设备结构 (calloc)
    ↓
探测设备参数 (dwc_i2c_probe_device)
    ├─ 设置默认内存大小(0x1000)
    └─ 设置默认IRQ号
    ↓
内存映射 (mmap_device_memory)
    ├─ 映射控制器寄存器
    └─ 获取虚拟基地址
    ↓
验证硬件类型 (DW_IC_COMP_TYPE)
    └─ 确认是DWC I2C控制器
    ↓
中断附加 (InterruptAttachEvent)
    └─ 连接到系统中断
    ↓
禁用I2C适配器
    ↓
寄存器初始化 (dwc_i2c_init_registers)
    ├─ 配置时钟参数
    ├─ 设置SCL时序(SS/FS)
    └─ 配置FIFO阈值
    ↓
返回设备句柄
```

### 2. 数据发送流程

```
dwc_i2c_send(hdl, buf, len, stop)
    ↓
设置传输参数
    ├─ txlen = len
    ├─ rxlen = 0
    ├─ xlen = len
    └─ txbuf = buf
    ↓
总线激活 (dwc_i2c_bus_active)
    ├─ 等待总线空闲
    ├─ 禁用I2C适配器
    ├─ 设置从设备地址
    ├─ 配置主控模式和速率
    └─ 启用I2C适配器
    ↓
清除中断 & 启用中断掩码
    ├─ 读清DW_IC_CLR_INTR
    └─ 写DW_IC_INTR_MASK = DEFAULT_MASK
    ↓
等待事务完成 (dwc_i2c_wait_complete)
    ├─ TX_EMPTY: 向FIFO写数据
    ├─ STOP_DET: 事务完成
    └─ TX_ABRT: 错误处理
    ↓
禁用中断
    └─ 写DW_IC_INTR_MASK = 0
    ↓
禁用I2C适配器
    ↓
返回状态
```

### 3. 数据接收流程

```
dwc_i2c_recv(hdl, buf, len, stop)
    ↓
设置传输参数
    ├─ txlen = 0
    ├─ rxlen = len
    ├─ xlen = len
    └─ rxbuf = buf
    ↓
总线激活 (dwc_i2c_bus_active)
    [同发送流程]
    ↓
启用中断
    ↓
等待事务完成 (dwc_i2c_wait_complete)
    ├─ TX_EMPTY: 向FIFO写读命令
    ├─ RX_FULL: 从FIFO读数据
    ├─ STOP_DET: 事务完成，读剩余数据
    └─ TX_ABRT: 错误处理
    ↓
禁用中断 & 禁用I2C适配器
    ↓
返回状态
```

### 4. 中断处理流程

```
dwc_i2c_process_intr(dev)
    ↓
读取中断状态 (DW_IC_INTR_STAT)
    ↓
条件分支:
    │
    ├─ TX_ABRT中断
    │   ├─ 读取错误源 (DW_IC_TX_ABRT_SOURCE)
    │   ├─ 清除中断 (DW_IC_CLR_TX_ABRT)
    │   ├─ 设置错误状态
    │   │   ├─ 仲裁丢失 → I2C_STATUS_ARBL
    │   │   ├─ 地址无应答 → I2C_STATUS_ADDR_NACK
    │   │   ├─ 数据无应答 → I2C_STATUS_DATA_NACK
    │   │   └─ 其他错误 → I2C_STATUS_ABORT
    │   └─ 禁用中断 & 返回
    │
    ├─ STOP_DET中断
    │   ├─ 清除STOP中断
    │   ├─ 读取剩余RX数据
    │   │   ├─ 检查数据长度
    │   │   └─ 从DW_IC_DATA_CMD读取
    │   ├─ 设置完成状态
    │   └─ 禁用中断 & 返回
    │
    ├─ RX_FULL中断
    │   ├─ 读取FIFO数据数量
    │   ├─ 边界检查
    │   ├─ 循环读取DW_IC_DATA_CMD
    │   └─ 更新计数器
    │
    └─ TX_EMPTY中断
        ├─ 循环向FIFO写数据/命令
        ├─ 区分操作类型
        │   ├─ 写数据 + 可选STOP条件
        │   ├─ 读命令 + 可选RESTART条件
        │   └─ 可选STOP条件
        └─ 检查FIFO是否满
```

---

## 关键数据结构

### dwc_dev_t - 设备控制块

```c
typedef struct {
    /* 硬件接口 */
    uint64_t        pbase;              // 物理基地址
    uintptr_t       vbase;              // 虚拟基地址(内存映射后)
    uint64_t        map_size;           // 映射大小 (0x1000)
    int             irq;                // 中断号 (0xa8)

    /* 中断 */
    int             iid;                // 中断ID

    /* 从设备地址 */
    unsigned        slave_addr;         // 从设备I2C地址
    i2c_addrfmt_t   slave_addr_fmt;     // 地址格式(7/10位)

    /* SCL时序参数 */
    struct scl_timing_param fast;       // 快速模式时序
    struct scl_timing_param std;        // 标准模式时序

    /* 时钟 */
    uint32_t        clock_khz;          // 输入时钟(KHz) - 200000
    uint32_t        ss_hcnt;            // 标准模式SCL高电平计数
    uint32_t        ss_lcnt;            // 标准模式SCL低电平计数
    uint32_t        fs_hcnt;            // 快速模式SCL高电平计数
    uint32_t        fs_lcnt;            // 快速模式SCL低电平计数
    uint32_t        scl_freq;           // 当前SCL频率(100K/400K)
    uint32_t        sda_hold_time;      // SDA保持时间

    /* 事务信息 */
    uint8_t         *txbuf;             // 发送缓冲区指针
    uint8_t         *rxbuf;             // 接收缓冲区指针
    uint32_t        xlen;               // 总事务长度
    uint32_t        txlen;              // 要发送的字节数
    uint32_t        rxlen;              // 要接收的字节数
    uint32_t        wrlen;              // 已写入FIFO的字节数
    uint32_t        rdlen;              // 已从FIFO读取的字节数

    /* 状态 */
    uint32_t        verbose;            // 调试输出级别
    uint32_t        master_cfg;         // 主控配置寄存器值
    uint32_t        status;             // 事务状态标志
    uint32_t        abort_source;       // 错误源标志
    uint32_t        fifo_depth;         // FIFO深度
    uint32_t        fixed_scl;          // 是否使用固定SCL值
} dwc_dev_t;
```

### i2c_dev_t - I2C设备

```c
typedef struct i2c_dev {
    iofunc_attr_t       hdr;            // I/O函数属性(QNX)
    dispatch_t          *dpp;           // 消息分发器
    resmgr_context_t    *ctp;           // 资源管理器上下文
    int                 id;             // 设备ID
    i2c_master_funcs_t  mfuncs;         // I2C主控操作函数表

    void                *buf;           // 通用缓冲区
    uint32_t            buflen;         // 缓冲区长度
    uint32_t            bus_speed;      // 当前总线速率
    uint32_t            default_bus_speed;  // 默认总线速率
    uint32_t            verbosity;      // 详细级别

    void                *hdl;           // dwc_dev_t句柄
} i2c_dev_t;
```

### scl_timing_param - 时序参数结构

```c
struct scl_timing_param {
    uint32_t high;      // SCL高电平时间(ns)
    uint32_t low;       // SCL低电平时间(ns)
    uint32_t fall;      // 下降时间(ns)
};
```

---

## 功能模块分析

### 1. 寄存器访问模块

**功能**：提供原子的寄存器读写操作

```c
// 读取32位寄存器值
uint32_t i2c_reg_read32(dwc_dev_t* dev, uint32_t offset)
    → *(volatile uint32_t *)(dev->vbase + offset)

// 写入32位寄存器值
void i2c_reg_write32(dwc_dev_t* dev, uint32_t offset, uint32_t value)
    → *(volatile uint32_t *)(dev->vbase + offset) = value
```

**应用场景**：读写所有I2C控制器寄存器

---

### 2. I2C启用/禁用模块

**函数**：`dwc_i2c_enable(dev, enable)`

**功能**：控制I2C适配器的启用/禁用

**流程**：
1. 写DW_IC_ENABLE寄存器
2. 轮询DW_IC_ENABLE_STATUS_ENABLE标志位
3. 最多重试1000次(25us延迟)
4. 超时返回-ETIMEDOUT

**时序容限**：25us × 1000 = 25ms超时

---

### 3. 总线激活模块

**函数**：`dwc_i2c_bus_active(dev)`

**关键步骤**：
1. **总线忙检查**：轮询DW_IC_STATUS中的ACTIVITY位
   - 如果总线忙，最多重试3次
   - 每次失败前尝试硬件复位
   
2. **适配器禁用**：写DW_IC_ENABLE = 0
   
3. **地址配置**：
   - 7位地址：直接写DW_IC_TAR
   - 10位地址：DW_IC_TAR | DW_IC_TAR_10BITADDR_MASTER
   
4. **配置控制寄存器**：
   - 主控模式(MASTER bit)
   - 速率模式(STD/FAST)
   - 从地址位宽(7/10bit)
   - RESTART支持
   
5. **适配器启用**：写DW_IC_ENABLE = 1

---

### 4. 传输等待模块

**函数**：`dwc_i2c_wait_complete(dev)`

**功能**：等待I2C事务完成，包处理所有中断

**工作流程**：
1. 循环处理中断
2. 调用`dwc_i2c_process_intr(dev)`处理每个中断源
3. 当收到STOP_DET或TX_ABRT中断时返回

---

### 5. SCL时序计算模块

**关键函数**：
- `i2c_dw_scl_hcnt()`：计算SCL高电平计数
- `i2c_dw_scl_lcnt()`：计算SCL低电平计数

**设计理念**：
- 需满足I2C规范的时序要求(tHIGH、tLOW、tF等)
- 同时避免违反tHD;STA(START后DATA保持时间)

**两种策略**：
- **理想模式**：优化tHIGH，可能违反tHD;STA
- **安全模式**：平衡tHD;STA和tHIGH要求(默认)

**公式**：
```
HCNT(unsafe) = IC_CLK * tHIGH / 1000000 - 8 + offset
HCNT(safe) = IC_CLK * (tHIGH + tF) / 1000000 - 3 + offset

LCNT = IC_CLK * (tLOW + tF) / 1000000 - 1 + offset
```

---

### 6. 从设备地址配置

**函数**：`dwc_i2c_set_slave_addr(hdl, addr, fmt)`

**功能**：设置I2C从设备地址

**实现**：
- 7位地址：清除DW_IC_CON_10BITADDR_MASTER
- 10位地址：置位DW_IC_CON_10BITADDR_MASTER
- 保存地址和格式到dev结构体

---

### 7. 总线速率配置

**函数**：`dwc_i2c_set_bus_speed(hdl, speed, ospeed)`

**支持的速率**：
- 100000 Hz(标准模式)：设置DW_IC_CON_SPEED_STD
- 400000 Hz(快速模式)：设置DW_IC_CON_SPEED_FAST

**实现**：修改master_cfg寄存器中的SPEED域

---

## 中断处理机制

### 中断源配置

```c
#define DW_IC_INTR_DEFAULT_MASK \
    (DW_IC_INTR_RX_FULL     |   // Rx FIFO到达阈值
     DW_IC_INTR_TX_EMPTY    |   // Tx FIFO为空
     DW_IC_INTR_TX_ABRT     |   // 传输中止(错误)
     DW_IC_INTR_STOP_DET)       // STOP条件检测
```

### 中断处理优先级

1. **TX_ABRT** (最高优先级)
   - 错误中断，立即停止传输
   - 清除错误源寄存器
   - 映射错误为I2C_STATUS_*标志

2. **STOP_DET**
   - 事务完成信号
   - 清除剩余RX FIFO
   - 设置完成状态

3. **RX_FULL**
   - 接收FIFO达到阈值
   - 读取数据到应用缓冲区

4. **TX_EMPTY** (最低优先级)
   - 发送FIFO状态允许
   - 写入数据或命令到FIFO

### 错误处理映射

| 错误源 | I2C状态 | 原因 |
|-------|--------|------|
| DW_IC_TX_ARBT_LOST | I2C_STATUS_ARBL | 仲裁丢失 |
| DW_IC_TX_ABRT_ADDR_NOACK | I2C_STATUS_ADDR_NACK | 地址无应答 |
| DW_IC_TX_ABRT_TXDATA_NOACK | I2C_STATUS_DATA_NACK | 数据无应答 |
| 其他 | I2C_STATUS_ABORT | 通用传输中止 |

---

## 时钟与速度配置

### 输入时钟获取

```c
uint32_t dwc_i2c_get_input_clock(dwc_dev_t *dev)
{
    return DW_IC_CLOCK_PARAMS_200MHZ / 1000;  // 200000 KHz
}
```

Raspberry Pi 5固定使用200MHz系统时钟

### 时序参数初始化

在`dwc_i2c_init_registers()`中设置：

**标准模式(100KHz)时序**：
```c
dev->std.high = 4000;   // 4.0 us (tHIGH)
dev->std.low = 4700;    // 4.7 us (tLOW)
dev->std.fall = 300;    // 0.3 us (tF)
```

**快速模式(400KHz)时序**：
```c
dev->fast.high = 600;   // 0.6 us (tHIGH)
dev->fast.low = 1300;   // 1.3 us (tLOW)
dev->fast.fall = 300;   // 0.3 us (tF)
```

### 计算示例

对于标准模式100KHz：
```
IC_CLK = 200000 KHz = 200 * 10^6 Hz
tHIGH = 4000 ns = 4 * 10^-6 s

HCNT_safe = (200 * 4 + 0.2) / 1 - 3 = 800 - 3 = 797
LCNT = (200 * (4.7 + 0.3) + 0.2) / 1 - 1 = 1000 - 1 = 999
```

---

## 错误处理

### 总线忙恢复

当检测到总线繁忙时，驱动实施渐进式恢复：

```
初次检查 (总线忙?)
    ↓
重试1次
    ↓
重试2次
    ↓
重试3次 + 执行硬件复位
    ↓ (仍然繁忙?)
最后尝试
    ↓
失败 → 返回I2C_STATUS_BUSY
```

### 超时处理

**模块** | **超时** | **操作**
--|--|--
使能/禁用 | 25ms | 轮询状态寄存器
总线准备 | 2s | 等待ACTIVITY标志清除
事务完成 | (可配置) | 中断驱动

### 传输错误恢复

对于错误的I2C事务：
1. 检测TX_ABRT中断
2. 读取详细的错误源代码
3. 清除中断标志
4. 映射错误为应用可理解的状态

---

## 设备初始化流程

### 初始化参数

驱动支持以下命令行选项：

```
选项 | 含义 | 示例
--|--|--
-c | 输入时钟(Hz) | -c 200000000
-f | SCL频率(Hz) | -f 400000
-h | 帮助信息 | -h
-I | (保留) | 
-s | 从地址(十进制/0x十六) | -s 0x50
-v | 详细级别 | -vvv
```

### 初始化检查清单

```
□ 内存映射硬件寄存器
□ 验证DWC I2C组件类型
□ 附加系统中断
□ 配置时钟参数(输入时钟)
□ 计算标准模式SCL计数(HCNT/LCNT_SS)
□ 计算快速模式SCL计数(HCNT/LCNT_FS)
□ 初始化FIFO阈值
□ 禁用I2C适配器(准备使用)
```

### 寄存器初始化

| 寄存器 | 初始值 | 目的 |
|-------|-------|------|
| DW_IC_SS_SCL_HCNT | 计算值 | 标准速率SCL高脉宽 |
| DW_IC_SS_SCL_LCNT | 计算值 | 标准速率SCL低脉宽 |
| DW_IC_FS_SCL_HCNT | 计算值 | 快速速率SCL高脉宽 |
| DW_IC_FS_SCL_LCNT | 计算值 | 快速速率SCL低脉宽 |
| DW_IC_TX_TL | fifo_depth/2 | TX FIFO阈值 |
| DW_IC_RX_TL | fifo_depth/2 | RX FIFO阈值 |
| DW_IC_SDA_HOLD | (默认) | SDA保持时间 |

---

## 关键API接口

### 驱动入口函数

```c
// 初始化驱动
void* dwc_i2c_init(int argc, char *argv[])
    ← 返回dwc_dev_t*句柄，失败返回NULL

// 卸载驱动
void dwc_i2c_fini(void *hdl)
    ← 清理设备资源
```

### I2C数据传输

```c
// 发送数据
i2c_status_t dwc_i2c_send(void *hdl, void *buf, uint32_t len, uint32_t stop)

// 接收数据
i2c_status_t dwc_i2c_recv(void *hdl, void *buf, uint32_t len, uint32_t stop)

// 发送+接收(重启)
i2c_status_t dwc_i2c_sendrecv(void *hdl, void *txbuf, uint32_t txlen,
                               void *rxbuf, uint32_t rxlen, uint32_t stop)
```

### 配置接口

```c
// 设置总线速率(100000或400000 Hz)
int32_t dwc_i2c_set_bus_speed(void *hdl, uint32_t speed, uint32_t *ospeed)

// 设置从设备地址
int32_t dwc_i2c_set_slave_addr(void *hdl, uint32_t addr, i2c_addrfmt_t fmt)
```

### 信息查询

```c
// 获取驱动版本
int32_t dwc_i2c_version_info(i2c_libversion_t *version)

// 获取驱动能力
int32_t dwc_i2c_driver_info(void *hdl, i2c_driver_info_t *info)
```

---

## 典型使用例

### 应用程序流程

```
1. 调用 dwc_i2c_init("-s 0x50 -f 400000")
   ↓ 返回设备句柄 dev_hdl

2. 设置从设备地址 dwc_i2c_set_slave_addr(dev_hdl, 0x50, I2C_ADDRFMT_7BIT)

3. 设置总线速率 dwc_i2c_set_bus_speed(dev_hdl, 400000, NULL)

4. 发送命令 dwc_i2c_send(dev_hdl, cmd_buf, cmd_len, 0)

5. 接收数据 dwc_i2c_recv(dev_hdl, data_buf, data_len, 1)
   [STOP=1表示事务结束]

6. 卸载驱动 dwc_i2c_fini(dev_hdl)
```

---

## 文件树结构

```
src/hardware/i2c/dwc/
├── Makefile                    # 编译配置
├── dwc_i2c.h                   # 寄存器定义
├── proto.h                     # 数据结构与函数声明
├── init.c                      # 初始化、寄存器配置
├── lib.c                       # 库函数接口注册
├── send.c                      # 发送实现
├── recv.c                      # 接收实现
├── sendrecv.c                  # 复合操作
├── wait.c                      # 等待与中断处理
├── bus_speed.c                 # 总线速率配置
├── clock.c                     # 时钟管理
├── slave_addr.c                # 地址配置
├── info.c                      # 驱动信息
├── version.c                   # 版本信息
├── common_options.c            # 命令行解析
├── detach.c                    # 清理和卸载
└── aarch64/                    # ARM 64位相关文件
    └── rpi5.le/                # Raspberry Pi 5小端配置
        ├── Makefile
        ├── variant.h           # 平台特定配置
        └── i2c-dwc-rpi5.pinfo  # 驱动信息文件
```

---

## 性能特性

- **FIFO深度**：256字节(可配置的阈值)
- **中断驱动**：支持异步传输
- **时钟**：200MHz输入，支持100K/400K两档
- **地址格式**：7位、10位均支持
- **最大传输长度**：受缓冲区大小限制(通常几KB)

---

## 已知限制

1. **SQML Level 1**：实验版本，非生产级质量
2. **主控模式仅**：不支持从控模式
3. **两档速率**：仅支持100K和400K，不支持高速(HS)
4. **无DMA支持**：使用PIO中断驱动方式
5. **无动态时钟**：输入时钟固定为200MHz

---

## 总结

这是一个设计完善的DWC I2C驱动实现，采用了：
- ✅ 分层架构设计
- ✅ 中断驱动异步传输
- ✅ 完整的错误处理机制
- ✅ 灵活的时序参数配置
- ✅ 清晰的代码组织

通过这个驱动，Raspberry Pi 5可以在QNX系统上进行可靠的I2C通信。

