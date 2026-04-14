# SPI DWC驱动架构与设计分析

## 目录
1. [概述](#概述)
2. [架构设计](#架构设计)
3. [核心组件](#核心组件)
4. [数据结构](#数据结构)
5. [初始化流程](#初始化流程)
6. [传输机制](#传输机制)
7. [中断处理](#中断处理)
8. [DMA支持](#dma支持)
9. [配置管理](#配置管理)
10. [错误处理](#错误处理)

---

## 概述

### SPI DWC驱动简介

SPI (Serial Peripheral Interface) DWC驱动实现了对DesignWare SPI控制器的完整支持。该驱动是QNX Neutrino操作系统中SPI总线的主控制器驱动，支持高速同步串行通信。

**关键特性:**
- **多模式支持**: 支持所有SPI时钟模式(CPOL/CPHA)
- **灵活数据宽度**: 3-32位可配置数据帧
- **高速传输**: 支持高达50MHz的时钟频率
- **多从设备**: 支持32个片选信号
- **中断驱动**: 高效的中断处理机制
- **DMA支持**: 可选的DMA传输以提高性能

**版本信息:**
- 软件成熟度: SQML Level 1 (实验性)
- 控制器: DesignWare SPI Controller
- 架构: 主控制器模式
- 兼容性: QNX io-spi框架

---

## 架构设计

### 分层架构

```
┌─────────────────────────────────────┐
│         应用程序层                   │
│  (SPI设备驱动、用户程序)             │
├─────────────────────────────────────┤
│         io-spi框架层                 │
│  - 设备抽象                          │
│  - 配置管理                          │
│  - 传输调度                          │
├─────────────────────────────────────┤
│         DWC SPI驱动层                │
│  - 硬件控制                          │
│  - 中断处理                          │
│  - DMA管理                           │
├─────────────────────────────────────┤
│         DesignWare SPI控制器         │
│  - 硬件IP核                          │
│  - FIFO缓冲                          │
│  - 时钟生成                          │
└─────────────────────────────────────┘
```

### 驱动组件关系

```
SPI总线结构
    │
    ├── SPI控制器 (dw_spi_t)
    │   ├── 硬件映射
    │   ├── 配置参数
    │   ├── FIFO管理
    │   └── DMA控制
    │
    └── SPI设备列表
        ├── 设备1 (spi_dev_t)
        │   ├── 设备配置
        │   ├── 片选信号
        │   └── 传输参数
        │
        ├── 设备2 (spi_dev_t)
        │   └── ...
        │
        └── 设备N
```

---

## 核心组件

### 1. SPI控制器结构 (dw_spi_t)

主控制器数据结构，管理整个SPI控制器的状态和配置：

```c
typedef struct
{
    spi_ctrl_t     *spi_ctrl;          // SPI控制器指针
    spi_bus_t      *bus;               // 总线结构指针

    int            iid;                // 中断ID
    uint64_t       pbase;              // 物理基地址
    uintptr_t      vbase;              // 虚拟基地址
    uint64_t       map_size;           // 映射大小

    void           *pbuf;              // 传输缓冲区
    uint32_t       xlen;               // 总传输长度(字)
    uint32_t       tlen;               // 已传输长度
    uint32_t       rlen;               // 已接收长度
    uint32_t       dlen;               // 数据字长度(字节)
    uint32_t       dscale;             // 字节到字的移位值

    uint32_t       cr0;                // CTRLR0配置值
    uint32_t       cs_max;             // 最大片选数
    bool           sste;               // 从设备选择切换使能
    uint32_t       fifo_len;           // FIFO长度

    bool           loopback;           // 环回模式
    uint64_t       xtime_us;           // 传输时间(微秒)

#ifdef DW_DMA
    bool           dma_active;         // DMA激活状态
    dw_dma_t       dma;                // DMA配置
#endif
} dw_spi_t;
```

**关键字段说明:**
- **pbase/vbase**: 硬件寄存器映射地址
- **xlen/tlen/rlen**: 传输进度跟踪
- **dlen/dscale**: 数据格式转换参数
- **cr0**: 控制器配置寄存器缓存
- **fifo_len**: 硬件FIFO深度

### 2. DMA支持结构 (dw_dma_t)

DMA传输的配置和控制结构：

```c
typedef struct {
    dma_functions_t     dma_funcs;     // DMA函数接口
    dma_attach_flags    dma_flags;     // DMA附加标志
    void                *rx_ch_handle; // RX通道句柄
    void                *tx_ch_handle; // TX通道句柄
    uint32_t            rx_channel;    // RX通道ID
    uint32_t            tx_channel;    // TX通道ID
} dw_dma_t;
```

### 3. 传输缓冲区管理

驱动使用统一的缓冲区管理机制：

```c
// 缓冲区指针
spi->pbuf = buf;           // 用户缓冲区
spi->xlen = nbytes >> dscale;  // 总字数
spi->tlen = 0;             // 已发送字数
spi->rlen = 0;             // 已接收字数
```

**数据缩放计算:**
```c
const uint32_t dscales[] = {0, 0, 1, 2, 2};
// dscale = dscales[dlen]
// dlen=1(8位): dscale=0, 字节=字
// dlen=2(16位): dscale=1, 2字节=1字
// dlen=4(32位): dscale=2, 4字节=1字
```

---

## 数据结构

### SPI配置结构 (spi_cfg_t)

定义SPI传输的参数配置：

```c
typedef struct spi_cfg {
    uint32_t    mode;           // SPI模式(CPOL/CPHA/数据宽度)
    uint32_t    clock_rate;     // 时钟频率
    uint32_t    cs_delay;       // 片选延迟
    uint32_t    inter_delay;    // 传输间延迟
    uint32_t    word_delay;     // 字间延迟
} spi_cfg_t;
```

**模式字段位定义:**
```c
#define SPI_MODE_CPOL_1          (1 << 0)    // 时钟极性
#define SPI_MODE_CPHA_1          (1 << 1)    // 时钟相位
#define SPI_MODE_WORD_WIDTH_MASK (0x1F << 2) // 数据宽度掩码
#define SPI_MODE_WORD_WIDTH(n)   ((n) << 2)  // 数据宽度设置
```

### SPI设备结构 (spi_dev_t)

表示连接到SPI总线的从设备：

```c
typedef struct spi_dev {
    spi_devinfo_t   devinfo;    // 设备信息
    void            *drvhdl;    // 驱动句柄
    uint32_t        flags;      // 设备标志
} spi_dev_t;
```

### SPI总线结构 (spi_bus_t)

管理整个SPI总线的状态：

```c
typedef struct spi_bus {
    spi_ctrl_t      *spi_ctrl;      // SPI控制器
    spi_funcs_t     *funcs;         // 函数接口
    spi_dev_t       *devlist;       // 设备列表
    void            *drvhdl;        // 驱动句柄
    uint32_t        input_clk;      // 输入时钟
    uint32_t        irq;            // 中断号
    uint64_t        pbase;          // 物理基地址
    char            *bs;            // 板级配置
    struct sigevent evt;            // 信号事件
} spi_bus_t;
```

---

## 初始化流程

### 1. 模块加载初始化

```
spi_init(bus)
│
├── 参数验证
│   └── 检查bus结构有效性
│
├── 分配驱动结构
│   dw_spi_t *spi = calloc(sizeof(dw_spi_t))
│   └── 初始化默认值
│
├── 设置函数接口
│   ├── bus->funcs->spi_fini = dw_fini
│   ├── bus->funcs->xfer = dw_xfer
│   ├── bus->funcs->setcfg = dw_setcfg
│   └── bus->funcs->drvinfo = dw_drvinfo
│
├── 处理配置参数
│   dw_process_args(spi, bus->bs)
│   ├── 解析cs_max, sste, loopback等选项
│   └── 设置DMA通道(如果启用)
│
└── 硬件初始化
    ├── 映射寄存器
    ├── 配置中断
    ├── 初始化设备
    └── 创建从设备
```

### 2. 硬件映射和初始化

```
map_in_device_registers(spi)
│
├── 内存映射
│   spi->vbase = mmap_device_memory(NULL, spi->map_size,
│           PROT_READ|PROT_WRITE|PROT_NOCACHE, MAP_SHARED, spi->pbase)
│
├── 禁用控制器
│   dw_spi_enable(spi, DW_SPI_DISABLE)
│
├── 获取FIFO长度
│   spi->fifo_len = dw_spi_get_fifo_len(spi)
│
└── 设备初始化
    dw_init_device(spi)
    ├── 配置默认参数
    └── 使能控制器
```

### 3. 从设备创建

```
spi_create_devs(bus->devlist)
│
├── 遍历设备列表
│   for (每个配置的设备)
│   │
│   ├── 分配spi_dev_t结构
│   ├── 设置设备信息
│   │   ├── 设备号(devno)
│   │   ├── 配置参数(cfg)
│   │   └── 设备名称
│   │
│   └── 添加到设备列表
│
└── 返回设备数量
```

---

## 传输机制

### 1. 传输准备阶段

```
dw_prepare_for_transfer(spi, cfg, tnbytes, rnbytes)
│
├── 数据宽度验证
│   nbits = cfg->mode & SPI_MODE_WORD_WIDTH_MASK
│   if (nbits < 3 || nbits > 32) return EINVAL
│
├── 计算数据参数
│   ├── spi->dlen = (nbits + 7) / 8
│   ├── spi->dscale = dscales[spi->dlen]
│   └── spi->xlen = nbytes >> spi->dscale
│
├── 时钟频率验证
│   if (cfg->clock_rate == 0) return EINVAL
│
├── 构建CTRLR0配置
│   spi->cr0 = DW_SPI_CTRLR0_FRF_MOTO_SPI << FRF_OFFSET
│   │         | DW_SPI_CTRLR0_TMOD_TR << TMOD_OFFSET
│   │         | DW_PSSI_CTRLR0_DFS_CONFIG(spi->dlen)
│   │
│   ├── 设置CPOL/CPHA
│   │   if (cfg->mode & SPI_MODE_CPOL_1) cr0 |= SCPOL
│   │   if (cfg->mode & SPI_MODE_CPHA_1) cr0 |= SCPHA
│   │
│   └── 设置特殊模式
│       if (loopback) cr0 |= SRL
│       if (sste) cr0 |= SSTE
│
├── 配置硬件寄存器
│   ├── dw_write32(spi, DW_SPI_BAUDR, input_clk / cfg->clock_rate)
│   ├── dw_write32(spi, DW_SPI_CTRLR0, spi->cr0)
│   └── dw_write32(spi, DW_SPI_CTRLR1, 0)
│
└── 计算传输时间
    spi->xtime_us = max(1, nbits * 1000 * 1000 * xlen / clock_rate)
```

### 2. 数据传输流程

```
dw_xfer(hdl, spi_dev, buf, tnbytes, rnbytes)
│
├── 忙碌检查
│   if (dw_spi_busy(spi)) return EBUSY
│
├── 传输准备
│   dw_prepare_for_transfer(spi, cfg, tnbytes, rnbytes)
│
├── FIFO预填充
│   len = min(xlen, fifo_len / 2)
│   dw_write_fifo(spi, len)
│   spi->tlen += len
│
├── 中断配置
│   if (tlen < xlen) {
│       // 配置TX/RX中断
│       dw_write32(spi, DW_SPI_IMR, DW_SPI_INT_MASK)
│       dw_write32(spi, DW_SPI_TXFTLR, thresh)
│       dw_write32(spi, DW_SPI_RXFTLR, thresh - 1)
│   } else {
│       // 只配置RX中断
│       dw_write32(spi, DW_SPI_IMR, RX_INT_MASK)
│   }
│
├── 选择从设备
│   dw_spi_select_slave(spi, spi_dev->devinfo.devno)
│
├── 等待传输完成
│   dw_wait(spi)
│
└── 清理和返回
    dw_spi_enable(spi, DW_SPI_DISABLE)
```

### 3. FIFO操作

**写入FIFO:**
```c
void dw_write_fifo(const dw_spi_t *const spi, const uint32_t len)
{
    const uint8_t *buf = spi->pbuf;
    uint32_t count = len;

    switch (spi->dlen) {
        case 1:  // 8位模式
            dw_write_fifo_uint8(spi, buf + spi->tlen, count);
            break;
        case 2:  // 16位模式
            dw_write_fifo_uint16(spi, (uint16_t*)(buf + spi->tlen * 2), count);
            break;
        case 4:  // 32位模式
            dw_write_fifo_uint32(spi, (uint32_t*)(buf + spi->tlen * 4), count);
            break;
    }
    spi->tlen += count;
}
```

**读取FIFO:**
```c
void dw_read_fifo(const dw_spi_t *const spi, const uint32_t len)
{
    uint8_t *buf = spi->pbuf;
    uint32_t count = len;

    switch (spi->dlen) {
        case 1:
            dw_read_fifo_uint8(spi, buf + spi->rlen, count);
            break;
        case 2:
            dw_read_fifo_uint16(spi, (uint16_t*)(buf + spi->rlen * 2), count);
            break;
        case 4:
            dw_read_fifo_uint32(spi, (uint32_t*)(buf + spi->rlen * 4), count);
            break;
    }
    spi->rlen += count;
}
```

---

## 中断处理

### 中断服务例程

```
SPI中断处理流程
│
├── 读取中断状态
│   const uint32_t status = dw_read32(spi, DW_SPI_ISR)
│
├── 错误中断处理
│   ├── TX溢出 (TXOI): 记录错误
│   ├── RX溢出 (RXOI): 记录错误
│   ├── RX下溢 (RXUI): 记录错误
│   └── 多主冲突 (MSTI): 记录错误
│
├── 数据传输中断
│   ├── TX FIFO空 (TXEI)
│   │   ├── 写入更多数据到TX FIFO
│   │   ├── 更新tlen计数器
│   │   └── 检查是否传输完成
│   │
│   └── RX FIFO满 (RXFI)
│       ├── 从RX FIFO读取数据
│       ├── 更新rlen计数器
│       └── 检查是否接收完成
│
├── 传输完成检查
│   if (tlen >= xlen && rlen >= xlen)
│       ├── 清除所有中断
│       ├── 禁用SPI控制器
│       └── 唤醒等待线程
│
└── 中断返回
    InterruptUnmask(spi->bus->irq, spi->iid)
```

### 中断掩码配置

```c
// 传输进行中 - 启用所有中断
dw_write32(spi, DW_SPI_IMR, DW_SPI_INT_MASK);

// 只接收 - 只启用接收相关中断
dw_write32(spi, DW_SPI_IMR, DW_SPI_INT_RXUI |
                            DW_SPI_INT_RXOI |
                            DW_SPI_INT_RXFI);

// 传输完成 - 禁用所有中断
dw_write32(spi, DW_SPI_IMR, 0);
```

### FIFO阈值设置

```c
// TX FIFO阈值 - 当TX FIFO ≤ thresh时触发中断
dw_write32(spi, DW_SPI_TXFTLR, thresh);

// RX FIFO阈值 - 当RX FIFO ≥ thresh+1时触发中断
dw_write32(spi, DW_SPI_RXFTLR, thresh - 1);
```

---

## DMA支持

### DMA初始化

```
dw_spi_init_dma(spi)
│
├── 获取DMA函数
│   get_dmafuncs(&spi->dma.dma_funcs, sizeof(dma_functions_t))
│
├── 分配DMA通道
│   ├── spi->dma.rx_ch_handle = dma_channel_attach(...)
│   └── spi->dma.tx_ch_handle = dma_channel_attach(...)
│
├── 配置DMA标志
│   spi->dma.dma_flags = DMA_ATTACH_EVENT_ON_COMPLETE |
│                       DMA_ATTACH_ANY_CHANNEL
│
└── 设置完成回调
    dma_callback_t callback = { .func = dma_complete_handler }
```

### DMA传输流程

```
dw_spi_dmaxfer(hdl, spi_dev, addr, tnbytes, rnbytes)
│
├── 验证DMA可用性
│   if (!dma_channels_allocated) return ENOTSUP
│
├── 配置传输参数
│   dma_transfer_t txfer = {
│       .src_addrs = addr->paddr,
│       .dst_addrs = spi->pbase + DW_SPI_DR,
│       .len = tnbytes,
│       .src_flags = DMA_ADDR_FLAG_NO_INCREMENT,
│       .dst_flags = DMA_ADDR_FLAG_NO_INCREMENT
│   }
│
├── 启动DMA传输
│   dma_start_transfer(spi->dma.tx_ch_handle, &txfer)
│   dma_start_transfer(spi->dma.rx_ch_handle, &rxfer)
│
├── 等待DMA完成
│   dma_wait_event(spi->dma.tx_ch_handle)
│   dma_wait_event(spi->dma.rx_ch_handle)
│
└── 清理和返回
```

### DMA缓冲区管理

```c
// 分配DMA缓冲区
int dw_spi_dma_allocbuf(void *hdl, dma_addr_t *addr, uint32_t len)
{
    dw_spi_t *spi = hdl;

    // 分配连续内存
    addr->vaddr = dma_mem_alloc(len, &addr->paddr);
    if (addr->vaddr == NULL) return ENOMEM;

    addr->len = len;
    return EOK;
}

// 释放DMA缓冲区
int dw_spi_dma_freebuf(void *hdl, dma_addr_t *addr)
{
    dma_mem_free(addr->vaddr, addr->len);
    return EOK;
}
```

---

## 配置管理

### 设备配置

```
dw_setcfg(hdl, spi_dev, cfg)
│
├── 验证配置参数
│   ├── 时钟频率范围
│   ├── 数据宽度有效性
│   └── 模式参数检查
│
├── 更新设备配置
│   spi_dev->devinfo.cfg = *cfg
│
├── 缓存配置值
│   spi_dev->cfg_cache = *cfg
│
└── 返回成功
```

### 驱动信息查询

```
dw_drvinfo(hdl, info)
│
├── 填充驱动信息
│   info->version = DRIVER_VERSION
│   info->name = "dwc-spi"
│   info->feature = SPI_FEATURE_DMA | SPI_FEATURE_INTR
│
├── 报告能力
│   info->max_speed = 50000000  // 50MHz
│   info->max_word_width = 32
│   info->min_word_width = 3
│
└── 报告支持的模式
    info->mode = SPI_MODE_CPOL_MASK | SPI_MODE_CPHA_MASK |
                 SPI_MODE_WORD_WIDTH_MASK
```

### 设备信息查询

```
dw_devinfo(hdl, spi_dev, info)
│
├── 获取设备配置
│   const spi_cfg_t *cfg = &spi_dev->devinfo.cfg
│
├── 报告设备能力
│   info->max_speed = cfg->clock_rate
│   info->word_width = cfg->mode & SPI_MODE_WORD_WIDTH_MASK
│   info->mode = cfg->mode
│
├── 报告时序参数
│   info->cs_delay = cfg->cs_delay
│   info->inter_delay = cfg->inter_delay
│   info->word_delay = cfg->word_delay
│
└── 报告设备状态
    info->status = SPI_DEV_READY
```

---

## 错误处理

### 错误检测和分类

```c
// 中断中的错误处理
if (status & DW_SPI_INT_TXOI) {
    spi_slogf(_SLOG_ERROR, "TX FIFO overflow");
    error_code = SPI_ERR_TX_OVERFLOW;
}

if (status & DW_SPI_INT_RXOI) {
    spi_slogf(_SLOG_ERROR, "RX FIFO overflow");
    error_code = SPI_ERR_RX_OVERFLOW;
}

if (status & DW_SPI_INT_RXUI) {
    spi_slogf(_SLOG_ERROR, "RX FIFO underflow");
    error_code = SPI_ERR_RX_UNDERFLOW;
}

if (status & DW_SPI_INT_MSTI) {
    spi_slogf(_SLOG_ERROR, "Multi-master contention");
    error_code = SPI_ERR_MULTI_MASTER;
}
```

### 传输超时处理

```
dw_wait(spi)
│
├── 计算超时时间
│   timeout_ns = spi->xtime_us * 1000 * 2  // 2倍传输时间
│
├── 等待传输完成
│   TimerTimeout(CLOCK_REALTIME, _NTO_TIMEOUT_INTR, &event, &timeout, NULL)
│   InterruptWait(0, NULL)
│
├── 检查完成状态
│   if (spi->tlen >= spi->xlen && spi->rlen >= spi->xlen)
│       return EOK
│
├── 超时处理
│   ├── 记录错误日志
│   ├── 禁用控制器
│   ├── 重置传输状态
│   └── 返回ETIMEDOUT
│
└── 错误返回
```

### 控制器重置

```c
// 设备重置函数
void dw_device_reset(const dw_spi_t *const spi)
{
    dw_spi_enable(spi, DW_SPI_DISABLE);
    dw_spi_enable(spi, DW_SPI_ENABLE);
}

// 错误恢复
if (传输失败) {
    dw_device_reset(spi);
    // 重新初始化传输
    dw_prepare_for_transfer(spi, cfg, tnbytes, rnbytes);
}
```

---

**总结**

SPI DWC驱动是QNX中功能完整的SPI控制器驱动实现，提供了从基本数据传输到高级DMA支持的完整功能集。通过精心设计的架构和高效的中断处理机制，实现了高性能的SPI通信。

关键设计特点：
- **灵活配置**: 支持多种SPI模式和数据格式
- **高效传输**: 中断驱动和DMA双重机制
- **健壮错误处理**: 完善的错误检测和恢复
- **模块化设计**: 清晰的组件分离和接口抽象
- **性能优化**: FIFO阈值管理和传输流水线

这为QNX系统提供了稳定可靠的SPI通信支持，适用于各种嵌入式应用场景。