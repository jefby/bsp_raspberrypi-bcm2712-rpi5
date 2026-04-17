# SPI DWC驱动流程图与时序分析

## 目录
1. [初始化时序](#初始化时序)
2. [配置管理流程](#配置管理流程)
3. [传输准备流程](#传输准备流程)
4. [中断驱动传输](#中断驱动传输)
5. [DMA传输流程](#dma传输流程)
6. [FIFO管理机制](#fifo管理机制)
7. [错误处理流程](#错误处理流程)
8. [从设备选择](#从设备选择)
9. [时钟配置](#时钟配置)
10. [性能优化](#性能优化)

---

## 初始化时序

### 驱动加载时序图

```
时间轴: 0ms → 10ms → 50ms → 100ms → 200ms
        │      │       │       │       │
        ▼      ▼       ▼       ▼       ▼

模块注册      结构分配      参数处理      硬件映射      设备创建
spi_init()    calloc()      dw_process   mmap_device    spi_create
设置函数      初始化默认    _args()      _memory()      _devs()
接口          值            解析选项      配置中断      创建设备

参数: cs_max=1, sste=false, loopback=false
状态: DW_SPI_DISABLE → DW_SPI_ENABLE
结果: bus->drvhdl = spi, 设备列表建立
```

### 硬件初始化详细时序

```
map_in_device_registers(spi)
│
├── 0-2ms: 内存映射
│   vbase = mmap_device_memory(NULL, SPI0_SIZE,
│           PROT_READ|PROT_WRITE|PROT_NOCACHE,
│           MAP_SHARED, pbase)
│   if (vbase == MAP_FAILED) return ENOMEM
│
├── 2-5ms: 控制器禁用
│   dw_write32(spi, DW_SPI_SSIENR, DW_SPI_DISABLE)
│   确保配置安全
│
├── 5-10ms: FIFO长度检测
│   spi->fifo_len = dw_spi_get_fifo_len(spi)
│   ├── 写入测试值到TXFTLR
│   ├── 读取确认写入成功
│   └── 二分法确定最大值
│
├── 10-20ms: 设备初始化
│   dw_init_device(spi)
│   ├── 设置默认CTRLR0
│   ├── 配置BAUDR
│   ├── 使能控制器
│   └── 验证状态
│
├── 20-30ms: 中断连接
│   dw_attach_intr(spi)
│   iid = InterruptAttachEvent(irq, &evt, _NTO_INTR_FLAGS_TRK_MSK)
│
└── 30-50ms: 从设备创建
    spi_create_devs(devlist)
    为每个配置的设备分配spi_dev_t结构
```

---

## 配置管理流程

### 参数解析流程图

```
dw_process_args(spi, options)
│
├── 初始化选项表
│   dw_opts[] = {
│       {"cs_max",     OPTION_CS_MAX},
│       {"sste",       OPTION_SSTE},
│       {"loopback",   OPTION_LOOPBACK},
│       {"dma_rx_chan", OPTION_RX_CHANNEL},
│       {"dma_tx_chan", OPTION_TX_CHANNEL}
│   }
│
├── 解析选项循环
│   while (options && *options)
│   │
│   ├── 分割选项
│   │   opt = getsubopt(&options, dw_opts, &value)
│   │   if (opt == -1) return EINVAL
│   │
│   ├── 处理具体选项
│   │   switch (opt)
│   │   │
│   │   ├── OPTION_CS_MAX
│   │   │   spi->cs_max = strtoul(value)
│   │   │   if (cs_max < 1 || > 32) EINVAL
│   │   │
│   │   ├── OPTION_SSTE
│   │   │   spi->sste = true
│   │   │
│   │   ├── OPTION_LOOPBACK
│   │   │   spi->loopback = true
│   │   │
│   │   └── DMA选项
│   │       spi->dma.rx/tx_channel = strtoul(value)
│   │
│   └── 验证参数范围
│
└── 返回配置状态
```

### 设备配置设置

```
dw_setcfg(hdl, spi_dev, cfg)
│
├── 参数验证阶段
│   ├── 时钟频率检查
│   │   if (cfg->clock_rate == 0) return EINVAL
│   │
│   ├── 数据宽度验证
│   │   width = cfg->mode & SPI_MODE_WORD_WIDTH_MASK
│   │   if (width < 3 || width > 32) return EINVAL
│   │
│   └── 模式参数检查
│       // CPOL/CPHA已在传输时处理
│
├── 配置缓存更新
│   spi_dev->devinfo.cfg = *cfg
│   spi_dev->cfg_cache = *cfg
│
├── 硬件参数预计算
│   // 为后续传输准备参数
│   // 实际硬件配置在传输时进行
│
└── 返回成功状态
    EOK
```

---

## 传输准备流程

### 传输参数计算时序

```
dw_prepare_for_transfer(spi, cfg, tnbytes, rnbytes)
│
├── 0-1μs: 数据宽度处理
│   nbits = cfg->mode & SPI_MODE_WORD_WIDTH_MASK
│   if (nbits < 3 || nbits > 32) return EINVAL
│   spi->dlen = (nbits + DW_NBIT_ROUNDER) / 8
│   spi->dscale = dscales[spi->dlen]
│   if (dlen == 3) dlen = 4  // 特殊处理
│
├── 1-2μs: 长度计算
│   nbytes = max(tnbytes, rnbytes)
│   spi->xlen = nbytes >> spi->dscale
│   if (xlen == 0 || nbytes != (xlen << dscale)) return EINVAL
│
├── 2-3μs: 时钟验证
│   if (cfg->clock_rate == 0) return EINVAL
│
├── 3-10μs: CTRLR0配置构建
│   spi->cr0 = DW_SPI_CTRLR0_FRF_MOTO_SPI << FRF_OFFSET
│            | DW_SPI_CTRLR0_TMOD_TR << TMOD_OFFSET
│            | DW_PSSI_CTRLR0_DFS_CONFIG(spi->dlen)
│   │
│   ├── CPOL设置
│   │   if (cfg->mode & SPI_MODE_CPOL_1)
│   │       cr0 |= DW_SPI_CTRLR0_SCPOL
│   │
│   ├── CPHA设置
│   │   if (cfg->mode & SPI_MODE_CPHA_1)
│   │       cr0 |= DW_SPI_CTRLR0_SCPHA
│   │
│   ├── 环回模式
│   │   if (spi->loopback)
│   │       cr0 |= DW_PSSI_CTRLR0_SRL
│   │
│   └── 从设备选择切换
│       if (spi->sste)
│           cr0 |= DW_SPI_SSTE
│
├── 10-15μs: 硬件寄存器配置
│   dw_write32(spi, DW_SPI_BAUDR, input_clk / cfg->clock_rate)
│   dw_write32(spi, DW_SPI_CTRLR0, spi->cr0)
│   dw_write32(spi, DW_SPI_CTRLR1, 0)
│
├── 15-20μs: 传输时间计算
│   spi->xtime_us = max(1, nbits * 1000 * 1000 * xlen / cfg->clock_rate)
│
└── 20-25μs: 状态重置
    spi->tlen = spi->rlen = 0
```

### 数据格式转换

```
数据字长度计算
│
├── 输入: nbits (3-32)
├── 计算: dlen = (nbits + 7) / 8
│   ├── 3-8位: dlen = 1 (uint8_t)
│   ├── 9-16位: dlen = 2 (uint16_t)
│   └── 17-32位: dlen = 4 (uint32_t)
│
├── 特殊处理: dlen == 3 → dlen = 4
│   // 3字节字特殊映射到4字节
│
└── 缩放因子: dscale = dscales[dlen]
    // 用于字节计数到字计数的转换
```

---

## 中断驱动传输

### 完整传输时序图

```
应用程序调用                    io-spi框架                    DWC SPI驱动                    硬件控制器
    │                              │                              │                              │
    │  spi_xfer(dev, buf,         │                              │                              │
    │           tnbytes, rnbytes) │                              │                              │
    │ ──────────────────────────►  │                              │                              │
    │                              │ spi_xfer()                   │                              │
    │                              │ - 参数验证                   │                              │
    │                              │ - 调用bus->funcs->xfer()     │                              │
    │                              │                              │                              │
    │                              │                              │ dw_xfer()                     │
    │                              │                              │ - 忙碌检查                    │
    │                              │                              │ - dw_prepare_for_transfer()   │
    │                              │                              │ - FIFO预填充                 │
    │                              │                              │ - 中断配置                   │
    │                              │                              │ - dw_spi_select_slave()       │
    │                              │                              │ - dw_wait()                   │
    │                              │                              │                              │
    │                              │                              │                              │ 硬件传输开始
    │                              │                              │                              │ - 时钟生成
    │                              │                              │                              │ - 数据移位
    │                              │                              │                              │ - FIFO操作
    │                              │                              │                              │                              │
    │                              │                              │                              │ TX FIFO空中断
    │                              │                              │                              │ - 触发TXEI
    │                              │                              │                              │ - 调用ISR
    │                              │                              │                              │                              │
    │                              │                              │ SPI中断处理                   │
    │                              │                              │ - 读取DW_SPI_ISR             │
    │                              │                              │ - 处理TXEI: 填充更多数据     │
    │                              │                              │ - 更新tlen计数器             │
    │                              │                              │ - 检查传输完成               │
    │                              │                              │                              │
    │                              │                              │                              │ RX FIFO满中断
    │                              │                              │                              │ - 触发RXFI
    │                              │                              │                              │ - 调用ISR
    │                              │                              │                              │                              │
    │                              │                              │ SPI中断处理                   │
    │                              │                              │ - 处理RXFI: 读取数据         │
    │                              │                              │ - 更新rlen计数器             │
    │                              │                              │ - 检查接收完成               │
    │                              │                              │                              │
    │                              │                              │ 传输完成检查                 │
    │                              │                              │ if (tlen >= xlen &&          │
    │                              │                              │      rlen >= xlen)          │
    │                              │                              │ - 清除中断掩码               │
    │                              │                              │ - 禁用控制器                 │
    │                              │                              │ - 唤醒等待线程               │
    │                              │                              │ ◄────────────────────────────
    │                              │ dw_wait()返回                │                              │
    │                              │ - 检查超时                   │                              │
    │                              │ - 返回状态                   │                              │
    │                              │ ◄────────────────────────────                              │
    │                              │ spi_xfer()返回               │                              │
    │                              │ - 传递结果                   │                              │
    │                              │ ◄────────────────────────────                              │
    │  spi_xfer()返回结果          │                              │                              │
    │ ◄────────────────────────────                              │                              │
```

### 中断处理状态机

```
SPI中断状态机
│
├── 读取中断状态
│   status = dw_read32(spi, DW_SPI_ISR)
│
├── 错误状态分支
│   ├── TXOI (发送溢出)
│   │   ├── 记录错误
│   │   ├── 设置error_code
│   │   └── 清除中断
│   │
│   ├── RXOI (接收溢出)
│   │   ├── 记录错误
│   │   └── 清除中断
│   │
│   ├── RXUI (接收下溢)
│   │   ├── 记录错误
│   │   └── 清除中断
│   │
│   └── MSTI (多主冲突)
│       ├── 记录错误
│       └── 清除中断
│
├── 数据传输分支
│   ├── TXEI (发送FIFO空)
│   │   ├── 计算剩余数据
│   │   │   remain = spi->xlen - spi->tlen
│   │   │
│   │   ├── 填充FIFO
│   │   │   len = min(remain, spi->fifo_len)
│   │   │   dw_write_fifo(spi, len)
│   │   │   spi->tlen += len
│   │   │
│   │   └── 检查完成
│   │       if (spi->tlen >= spi->xlen)
│   │           禁用TX中断
│   │
│   └── RXFI (接收FIFO满)
│       ├── 计算可读数据
│       │   rx_avail = dw_read32(spi, DW_SPI_RXFLR)
│       │
│       ├── 读取FIFO
│       │   len = min(rx_avail, spi->xlen - spi->rlen)
│       │   dw_read_fifo(spi, len)
│       │   spi->rlen += len
│       │
│       └── 检查完成
│           if (spi->rlen >= spi->xlen)
│               禁用RX中断
│
├── 传输完成检查
│   if (spi->tlen >= spi->xlen && spi->rlen >= spi->xlen)
│   │
│   ├── 清除所有中断
│   │   dw_write32(spi, DW_SPI_ICR, DW_SPI_INT_MASK)
│   │
│   ├── 禁用控制器
│   │   dw_spi_enable(spi, DW_SPI_DISABLE)
│   │
│   ├── 取消片选
│   │   dw_spi_deselect_slave(spi)
│   │
│   └── 唤醒等待线程
│       MsgSendPulse(dw_wait的连接ID)
│
└── 中断返回
    InterruptUnmask(irq, iid)
```

---

## DMA传输流程

### DMA传输时序

```
DMA传输请求
│
├── 验证DMA支持
│   if (!spi->dma.tx_ch_handle || !spi->dma.rx_ch_handle)
│       return ENOTSUP
│
├── 分配DMA缓冲区
│   dw_spi_dma_allocbuf(hdl, &dma_addr, len)
│   ├── dma_mem_alloc() - 连续内存
│   └── 复制用户数据到DMA缓冲区
│
├── 配置DMA传输
│   ├── TX传输配置
│   │   txfer.src_addrs = dma_addr.paddr
│   │   txfer.dst_addrs = spi->pbase + DW_SPI_DR
│   │   txfer.len = tnbytes
│   │   txfer.src_flags = DMA_ADDR_FLAG_INCREMENT
│   │   txfer.dst_flags = DMA_ADDR_FLAG_NO_INCREMENT
│   │
│   └── RX传输配置
│       rxfer.src_addrs = spi->pbase + DW_SPI_DR
│       rxfer.dst_addrs = dma_addr.paddr + tnbytes
│       rxfer.len = rnbytes
│       rxfer.src_flags = DMA_ADDR_FLAG_NO_INCREMENT
│       rxfer.dst_flags = DMA_ADDR_FLAG_INCREMENT
│
├── 启动DMA传输
│   dma_start_transfer(spi->dma.tx_ch_handle, &txfer)
│   dma_start_transfer(spi->dma.rx_ch_handle, &rxfer)
│
├── 等待DMA完成
│   dma_wait_event(spi->dma.tx_ch_handle)
│   dma_wait_event(spi->dma.rx_ch_handle)
│
├── 复制结果数据
│   memcpy(user_buf + tnbytes, dma_addr.vaddr + tnbytes, rnbytes)
│
├── 释放DMA缓冲区
│   dw_spi_dma_freebuf(hdl, &dma_addr)
│
└── 返回传输状态
```

### DMA与中断模式比较

| 特性 | 中断模式 | DMA模式 |
|------|----------|---------|
| CPU使用 | 中等(中断处理) | 低(DMA自主传输) |
| 延迟 | 低(实时响应) | 中等(DMA设置开销) |
| 吞吐量 | 中等 | 高(连续传输) |
| 内存使用 | 低 | 高(DMA缓冲区) |
| 适用场景 | 小数据包 | 大数据块传输 |

---

## FIFO管理机制

### FIFO阈值动态配置

```
FIFO阈值设置策略
│
├── 分析传输参数
│   total_words = spi->xlen
│   fifo_depth = spi->fifo_len
│
├── 计算预填充量
│   prefill_words = min(total_words, fifo_depth / 2)
│   // 预填充一半FIFO以启动传输
│
├── 设置中断阈值
│   if (total_words > prefill_words)
│   │
│   ├── TX阈值
│   │   tx_thresh = prefill_words
│   │   // 当TX FIFO ≤ tx_thresh时触发TXEI
│   │
│   └── RX阈值
│       rx_thresh = prefill_words - 1
│       // 当RX FIFO ≥ rx_thresh+1时触发RXFI
│   │
│   else // 小传输
│       只设置RX中断，TX一次性填充
│
└── 应用配置
    dw_write32(spi, DW_SPI_TXFTLR, tx_thresh)
    dw_write32(spi, DW_SPI_RXFTLR, rx_thresh)
```

### FIFO水位管理

```
发送FIFO管理
│
├── 初始预填充
│   dw_write_fifo(spi, prefill_words)
│   spi->tlen += prefill_words
│
├── TX中断响应
│   while (传输未完成 && FIFO有空间)
│   │
│   ├── 计算可发送字数
│   │   remain = spi->xlen - spi->tlen
│   │   space = spi->fifo_len - 当前TX水位
│   │   send_words = min(remain, space)
│   │
│   ├── 写入FIFO
│   │   dw_write_fifo(spi, send_words)
│   │   spi->tlen += send_words
│   │
│   └── 检查完成
│       if (spi->tlen >= spi->xlen)
│           禁用TX中断
│
└── 传输完成
```

```
接收FIFO管理
│
├── RX中断响应
│   while (接收未完成 && FIFO有数据)
│   │
│   ├── 检查可用数据
│   │   rx_level = dw_read32(spi, DW_SPI_RXFLR)
│   │   if (rx_level == 0) break
│   │
│   ├── 计算可读取字数
│   │   remain = spi->xlen - spi->rlen
│   │   read_words = min(rx_level, remain)
│   │
│   ├── 读取FIFO
│   │   dw_read_fifo(spi, read_words)
│   │   spi->rlen += read_words
│   │
│   └── 检查完成
│       if (spi->rlen >= spi->xlen)
│           禁用RX中断
│
└── 接收完成
```

---

## 错误处理流程

### 传输错误恢复

```
传输中检测到错误
│
├── 中断服务例程识别错误
│   status = dw_read32(spi, DW_SPI_ISR)
│   if (status & ERROR_MASK)
│   │
│   ├── 记录错误类型
│   │   if (status & DW_SPI_INT_TXOI) error = TX_OVERFLOW
│   │   if (status & DW_SPI_INT_RXOI) error = RX_OVERFLOW
│   │   if (status & DW_SPI_INT_RXUI) error = RX_UNDERFLOW
│   │   if (status & DW_SPI_INT_MSTI) error = MULTI_MASTER
│   │
│   ├── 清除错误中断
│   │   dw_write32(spi, DW_SPI_ICR, status & ERROR_MASK)
│   │
│   ├── 停止传输
│   │   dw_spi_enable(spi, DW_SPI_DISABLE)
│   │   dw_spi_deselect_slave(spi)
│   │
│   └── 设置错误状态
│       spi->error_code = error
│       唤醒等待线程(错误状态)
│
└── 上层错误处理
    dw_wait()返回错误码
```

### 超时处理机制

```
dw_wait(spi)超时处理
│
├── 计算超时时间
│   timeout_ns = spi->xtime_us * 1000 * 2  // 2倍传输时间
│   TimerTimeout(CLOCK_REALTIME, _NTO_TIMEOUT_INTR,
│               &event, &timeout, NULL)
│
├── 等待事件
│   InterruptWait(0, NULL)
│
├── 超时检查
│   if (超时发生)
│   │
│   ├── 记录超时错误
│   │   spi_slogf(_SLOG_ERROR, "SPI transfer timeout")
│   │
│   ├── 强制停止传输
│   │   dw_spi_enable(spi, DW_SPI_DISABLE)
│   │   dw_spi_deselect_slave(spi)
│   │
│   ├── 清除所有中断
│   │   dw_write32(spi, DW_SPI_ICR, DW_SPI_INT_MASK)
│   │
│   └── 返回超时错误
│       return ETIMEDOUT
│
└── 正常完成
    return EOK
```

### 控制器重置恢复

```
SPI控制器重置序列
│
├── 检测需要重置的条件
│   ├── 连续传输错误
│   ├── 控制器状态异常
│   └── 配置参数变更
│
├── 执行重置序列
│   ├── 禁用控制器
│   │   dw_write32(spi, DW_SPI_SSIENR, DW_SPI_DISABLE)
│   │
│   ├── 等待控制器空闲
│   │   while (dw_spi_busy(spi)) {
│   │       delay(1ms)
│   │   }
│   │
│   ├── 重新配置参数
│   │   dw_prepare_for_transfer(spi, cfg, tnbytes, rnbytes)
│   │
│   └── 重新启动传输
│       dw_xfer()重新调用
│
└── 重置完成
```

---

## 从设备选择

### 片选信号管理

```
从设备选择时序
│
├── 验证设备号
│   if (slv >= spi->cs_max) return EINVAL
│
├── 检查控制器忙碌
│   if (dw_spi_busy(spi)) return -1
│
├── 取消当前选择
│   dw_write32(spi, DW_SPI_SER, 0)
│   // 取消所有片选
│
├── 激活目标设备
│   dw_write32(spi, DW_SPI_SER, (1UL << slv))
│   // 设置对应位
│
├── 等待建立时间
│   // 硬件自动处理，建立时间通常很短
│
└── 返回选择状态
    0: 成功, -1: 失败
```

### 多设备管理

```
SPI总线设备管理
│
├── 设备注册
│   spi_create_devs(devlist)
│   for (每个配置设备)
│   │
│   ├── 分配设备结构
│   │   spi_dev_t *dev = calloc(sizeof(spi_dev_t))
│   │
│   ├── 设置设备信息
│   │   dev->devinfo.devno = 设备号
│   │   dev->devinfo.cfg = 默认配置
│   │   dev->devinfo.name = 设备名
│   │
│   └── 添加到列表
│       SLIST_INSERT_HEAD(devlist, dev, link)
│
├── 设备查找
│   根据devno查找对应的spi_dev_t
│
└── 设备配置
    每个设备可有独立的SPI配置参数
```

---

## 时钟配置

### 波特率计算

```
SPI时钟频率设置
│
├── 输入参数
│   target_freq = cfg->clock_rate  // 目标频率
│   input_clk = spi->bus->input_clk // 输入时钟
│
├── 计算分频器
│   if (target_freq == 0) return EINVAL
│   divider = input_clk / target_freq
│   │
│   ├── 范围检查
│   │   if (divider < DW_SCK_DIV_MIN) divider = DW_SCK_DIV_MIN
│   │   if (divider > DW_SCK_DIV_MAX) divider = DW_SCK_DIV_MAX
│   │
│   └── 写入寄存器
│       dw_write32(spi, DW_SPI_BAUDR, divider)
│
├── 实际频率计算
│   actual_freq = input_clk / divider
│
└── 精度验证
    error = abs(actual_freq - target_freq) / target_freq
    if (error > 0.05) 记录警告 // 5%误差容限
```

### 时钟模式配置

```
SPI时钟模式设置 (CTRLR0)
│
├── CPOL (时钟极性)
│   if (cfg->mode & SPI_MODE_CPOL_1)
│       cr0 |= DW_SPI_CTRLR0_SCPOL  // 空闲时钟高电平
│   else
│       // 空闲时钟低电平 (默认)
│
├── CPHA (时钟相位)
│   if (cfg->mode & SPI_MODE_CPHA_1)
│       cr0 |= DW_SPI_CTRLR0_SCPHA  // 第二个时钟沿采样
│   else
│       // 第一个时钟沿采样 (默认)
│
└── 四种SPI模式
    模式0: CPOL=0, CPHA=0 (最常用)
    模式1: CPOL=0, CPHA=1
    模式2: CPOL=1, CPHA=0
    模式3: CPOL=1, CPHA=1
```

---

## 性能优化

### 传输流水线优化

```
SPI传输流水线
│
├── 预填充阶段
│   传输开始前填充TX FIFO
│   减少初始中断延迟
│
├── 中断批处理
│   每次中断处理多个FIFO字
│   减少中断频率
│
├── 阈值优化
│   根据传输大小动态调整FIFO阈值
│   小传输: 一次性填充
│   大传输: 分批处理
│
└── 缓存对齐
    确保数据缓冲区缓存对齐
    提高内存访问效率
```

### DMA vs 中断模式选择

```
传输模式选择算法
│
├── 分析传输参数
│   size = max(tnbytes, rnbytes)
│   freq = cfg->clock_rate
│
├── 计算传输时间
│   xfer_time_us = size * 8 * 1000000 / freq
│
├── 选择策略
│   if (size > DMA_THRESHOLD && DMA可用)
│   │   // 大传输使用DMA
│   │   使用DMA模式
│   │
│   elif (xfer_time_us < INTERRUPT_THRESHOLD)
│   │   // 快传输使用轮询
│   │   使用轮询模式
│   │
│   else
│       // 中等传输使用中断
│       使用中断模式
│
└── 模式切换
    根据策略选择相应传输函数
```

### 内存访问优化

```
数据访问优化
│
├── 缓冲区对齐
│   确保用户缓冲区按数据字对齐
│   减少未对齐访问开销
│
├── 预取策略
│   在中断中预取下一批数据
│   减少缓存未命中
│
├── 字节序处理
│   根据数据宽度优化字节序转换
│   使用编译器内置函数
│
└── 零拷贝优化
    DMA模式下直接使用用户缓冲区
    避免额外的数据复制
```

---

**总结**

SPI DWC驱动的流程设计体现了嵌入式SPI控制器的复杂性和优化需求。从初始化到传输完成，再到错误恢复，每一个环节都经过精心设计以确保性能、可靠性和灵活性。

关键流程特点：
- **流水线传输**: 预填充和中断批处理优化
- **多模式支持**: 中断、DMA、轮询三种传输模式
- **动态配置**: 根据传输参数优化FIFO阈值
- **错误恢复**: 多层次的错误检测和恢复机制
- **设备管理**: 灵活的从设备选择和管理

这些流程确保了驱动在各种SPI应用场景下的高效性和可靠性。