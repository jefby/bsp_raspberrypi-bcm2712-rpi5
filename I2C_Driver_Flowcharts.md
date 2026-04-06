# I2C驱动设计流程图详解

## 目录
1. [驱动架构图](#驱动架构图)
2. [初始化序列图](#初始化序列图)
3. [发送数据序列图](#发送数据序列图)
4. [接收数据序列图](#接收数据序列图)
5. [中断处理流程](#中断处理流程)
6. [状态机图](#状态机图)

---

## 驱动架构图

### 系统组件交互

```
┌────────────────────────────────────────────────────────────────┐
│                     应用层 (User Application)                   │
└────────────────────┬───────────────────────────────────────────┘
                     │
                     │ I2C API Calls
                     ▼
┌────────────────────────────────────────────────────────────────┐
│              QNX I2C Master Interface Layer                      │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ dwc_i2c_init()      - Initialize driver                │   │
│  │ dwc_i2c_fini()      - Finalize driver                  │   │
│  │ dwc_i2c_send()      - Send data                        │   │
│  │ dwc_i2c_recv()      - Receive data                     │   │
│  │ dwc_i2c_sendrecv()  - Send then receive (with RESTART)│   │
│  │ dwc_i2c_set_bus_speed()   - Configure speed           │   │
│  │ dwc_i2c_set_slave_addr()  - Set slave address         │   │
│  └─────────────────────────────────────────────────────────┘   │
└────────────────────┬───────────────────────────────────────────┘
                     │
                     │ Internal Protocol Control
                     ▼
┌────────────────────────────────────────────────────────────────┐
│           DWC I2C Protocol Controller Layer                     │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ dwc_i2c_bus_active()   - Prepare bus                    │   │
│  │ dwc_i2c_wait_complete()- Wait for transaction done      │   │
│  │ dwc_i2c_process_intr() - Handle interrupts              │   │
│  │ dwc_i2c_init_registers()- Initialize registers          │   │
│  │ dwc_i2c_enable()       - Enable/Disable adapter         │   │
│  └─────────────────────────────────────────────────────────┘   │
└────────────────────┬───────────────────────────────────────────┘
                     │
                     │ Register Access (Memory-mapped I/O)
                     ▼
┌────────────────────────────────────────────────────────────────┐
│         Hardware Abstraction Layer (HAL)                        │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ i2c_reg_read32(dev, offset)                             │   │
│  │   → Read from mmapped address                           │   │
│  │ i2c_reg_write32(dev, offset, value)                     │   │
│  │   → Write to mmapped address                            │   │
│  │ mmap_device_memory()  - Create virtual mapping          │   │
│  │ InterruptAttachEvent() - Attach to IRQ                  │   │
│  │ munmap_device_memory() - Unmap memory                   │   │
│  └─────────────────────────────────────────────────────────┘   │
└────────────────────┬───────────────────────────────────────────┘
                     │
                     │ Physical I2C Bus
                     ▼
┌────────────────────────────────────────────────────────────────┐
│        DWC I2C Hardware Controller (BCM2712)                    │
│  Physical Address: 0x????_????  (Device Tree定义)              │
│  IRQ: 0xa8 (168)                                               │
│  Memory Size: 0x1000 (4KB)                                      │
│                                                                 │
│  Register Set: 0x00-0xf8, 0x204, 0x800                         │
│  Interrupt Sources: STOP, ABORT, RX_FULL, TX_EMPTY             │
│  FIFO Depth: 256 bytes (TX/RX)                                 │
└────────────────────────────────────────────────────────────────┘
```

---

## 初始化序列图

### dwc_i2c_init() 详细流程

```
应用程序
    │
    │ dwc_i2c_init(argc, argv)
    │
    ▼─────────────────────────────────────────────────────
    分配 dwc_dev_t 结构体 (calloc)
    │
    ▼─────────────────────────────────────────────────────
    解析命令行选项 (dwc_i2c_parseopts)
    ├─ -c: 输入时钟频率
    ├─ -f: I2C速率 (100K或400K)
    ├─ -s: 从设备地址
    └─ -v: 详细输出级别
    │
    ▼─────────────────────────────────────────────────────
    探测设备参数 (dwc_i2c_probe_device)
    ├─ dev->map_size = 0x1000
    └─ dev->irq = 0xa8 (树莫派5)
    │
    ▼─────────────────────────────────────────────────────
    内存映射硬件寄存器 (mmap_device_memory)
    ├─ prot: PROT_READ | PROT_WRITE | PROT_NOCACHE
    ├─ flags: MAP_SHARED
    └─ dev->vbase = 映射虚拟地址
    │
    ▼─────────────────────────────────────────────────────
    验证硬件类型 (读 DW_IC_COMP_TYPE)
    ├─ 是否等于 DW_IC_COMP_TYPE_VALUE?
    └─ 不匹配则清理并失败返回
    │
    ▼─────────────────────────────────────────────────────
    中断附加 (InterruptAttachEvent)
    ├─ irq = dev->irq (0xa8)
    ├─ event = SIGEV_INTR
    ├─ flags = _NTO_INTR_FLAGS_TRK_MSK
    └─ dev->iid = 中断ID
    │
    ▼─────────────────────────────────────────────────────
    禁用I2C适配器 (dwc_i2c_enable, 0)
    │
    ▼─────────────────────────────────────────────────────
    初始化寄存器 (dwc_i2c_init_registers)
    │
    ├─ 获取输入时钟 (200MHz / 1000 = 200000 KHz)
    │
    ├─ 计算标准模式(100KHz)时序
    │  ├─ input: tHIGH=4.0us, tLOW=4.7us, tF=0.3us
    │  ├─ ss_hcnt = i2c_dw_scl_hcnt(200000, 4000, 300, ...)
    │  └─ ss_lcnt = i2c_dw_scl_lcnt(200000, 4700, 300, ...)
    │
    ├─ 计算快速模式(400KHz)时序
    │  ├─ input: tHIGH=0.6us, tLOW=1.3us, tF=0.3us
    │  ├─ fs_hcnt = i2c_dw_scl_hcnt(200000, 600, 300, ...)
    │  └─ fs_lcnt = i2c_dw_scl_lcnt(200000, 1300, 300, ...)
    │
    ├─ 写寄存器
    │  ├─ DW_IC_SS_SCL_HCNT ← ss_hcnt
    │  ├─ DW_IC_SS_SCL_LCNT ← ss_lcnt
    │  ├─ DW_IC_FS_SCL_HCNT ← fs_hcnt
    │  ├─ DW_IC_FS_SCL_LCNT ← fs_lcnt
    │  ├─ DW_IC_SDA_SETUP ← 值
    │  ├─ DW_IC_SDA_HOLD ← 值
    │  ├─ DW_IC_INTR_MASK ← 0 (禁用中断)
    │  ├─ DW_IC_RX_TL ← fifo_depth/2
    │  └─ DW_IC_TX_TL ← fifo_depth/2
    │
    └─ 设置默认配置
       ├─ scl_freq = 400000 (默认快速模式)
       ├─ slave_addr_fmt = I2C_ADDRFMT_7BIT (默认7位)
       └─ master_cfg = MASTER | SPEED_FAST | SLAVE_DISABLE
    │
    ▼─────────────────────────────────────────────────────
    返回 dev_hdl = (dwc_dev_t*)
    │
    失败分支 (goto fail_cleanup):
    ├─ 卸载映射内存 (munmap_device_memory)
    ├─ 分离中断 (InterruptDetach - 如已附加)
    └─ 释放设备结构 (free)
```

---

## 发送数据序列图

### dwc_i2c_send() 完整流程

```
应用程序
    │
    │ dwc_i2c_send(hdl, buf, len, stop)
    │
    ▼─────────────────────────────────────────────────────
    参数检查
    ├─ len <= 0? → 返回 I2C_STATUS_DONE
    └─ 否则继续
    │
    ▼─────────────────────────────────────────────────────
    初始化传输参数
    ├─ dev->txlen = len
    ├─ dev->rxlen = 0
    ├─ dev->xlen = len + 0 = len
    ├─ dev->wrlen = 0 (还未写FIFO)
    ├─ dev->rdlen = 0 (还未读FIFO)
    ├─ dev->txbuf = buf
    └─ dev->status = 0
    │
    ▼─────────────────────────────────────────────────────
    总线激活 dwc_i2c_bus_active(dev)
    │
    ├─ 等待总线空闲 (dwc_i2c_wait_bus_not_busy)
    │  ├─ 轮询 DW_IC_STATUS & ACTIVITY_BIT
    │  ├─ 超时: 2秒 (timeout=200, 10ms步长)
    │  └─ 如失败: 执行复位 dwc_i2c_reset()
    │
    ├─ 禁用适配器 (dwc_i2c_enable, 0)
    │  ├─ 写 DW_IC_ENABLE = 0
    │  ├─ 轮询 DW_IC_ENABLE_STATUS 确认禁用
    │  └─ 超时: 25ms
    │
    ├─ 设置从设备地址 (DW_IC_TAR)
    │  └─ 写 DW_IC_TAR = dev->slave_addr
    │
    ├─ 配置控制寄存器 (DW_IC_CON)
    │  └─ 写 DW_IC_CON = dev->master_cfg
    │     (包含: MASTER=1, SPEED=FAST, SLAVE_DISABLE=1, ...)
    │
    ├─ 禁用中断 (临时)
    │  └─ 写 DW_IC_INTR_MASK = 0
    │
    ├─ 启用适配器 (dwc_i2c_enable, 1)
    │  ├─ 写 DW_IC_ENABLE = 1
    │  ├─ 轮询 DW_IC_ENABLE_STATUS 确认启用
    │  └─ 超时: 25ms
    │
    └─ 总线激活返回 (成功则返回0)
    │
    ▼─────────────────────────────────────────────────────
    清除中断位
    ├─ 读 DW_IC_CLR_INTR (清除所有未决中断)
    └─ (清除操作是读操作的副作用)
    │
    ▼─────────────────────────────────────────────────────
    启用中断
    ├─ 写 DW_IC_INTR_MASK = DW_IC_INTR_DEFAULT_MASK
    │  (启用: RX_FULL, TX_EMPTY, TX_ABRT, STOP_DET)
    │
    └─ 硬件现在开始生成相关中断
    │
    ▼─────────────────────────────────────────────────────
    等待事务完成 dwc_i2c_wait_complete(dev)
    │
    └─ 中断处理循环
       │
       ├─ 等待中断信号
       │  └─ (系统信号或轮询)
       │
       ├─ 读 DW_IC_INTR_STAT (中断状态)
       │
       ├─ 条件分支:
       │
       │  ✓ 如 TX_EMPTY & (有数据要写)
       │    │
       │    ├─ 循环直到 FIFO满 或 写完所有数据:
       │    │  ├─ 获取下一字节: byte = *dev->txbuf++
       │    │  ├─ 检查是否最后一字节? (wrlen == xlen-1)
       │    │  │  ├─ 是: reg = byte | WRITE | STOP
       │    │  │  └─ 否: reg = byte | WRITE
       │    │  ├─ 写 DW_IC_DATA_CMD = reg
       │    │  └─ dev->wrlen += 1
       │    │
       │    └─ 当 wrlen >= xlen: 禁用 TX_EMPTY 中断
       │
       │  ✓ 如 STOP_DET (事务完成信号)
       │    │
       │    ├─ 读 DW_IC_CLR_STOP_DET (清中断)
       │    │
       │    ├─ (发送操作中通常无RX数据)
       │    │
       │    ├─ 设置 dev->status |= I2C_STATUS_DONE
       │    │
       │    ├─ 禁用中断: 写 DW_IC_INTR_MASK = 0
       │    │
       │    └─ 返回 (退出等待循环)
       │
       │  ✓ 如 TX_ABRT (错误)
       │    │
       │    ├─ 读 DW_IC_TX_ABRT_SOURCE → dev->abort_source
       │    │
       │    ├─ 读 DW_IC_CLR_TX_ABRT (清中断)
       │    │
       │    ├─ 映射错误:
       │    │  ├─ DW_IC_TX_ARBT_LOST → I2C_STATUS_ARBL
       │    │  ├─ DW_IC_TX_ABRT_ADDR_NOACK → I2C_STATUS_ADDR_NACK
       │    │  ├─ DW_IC_TX_ABRT_TXDATA_NOACK → I2C_STATUS_DATA_NACK
       │    │  └─ 其他 → I2C_STATUS_ABORT
       │    │
       │    ├─ 设置 dev->status = I2C_STATUS_DONE | 错误标志
       │    │
       │    ├─ 禁用中断: 写 DW_IC_INTR_MASK = 0
       │    │
       │    └─ 返回 (退出等待循环)
       │
       └─ 其他中断: 忽略(该操作不需要)
    │
    ▼─────────────────────────────────────────────────────
    禁用中断
    └─ 写 DW_IC_INTR_MASK = 0
    │
    ▼─────────────────────────────────────────────────────
    禁用I2C适配器
    └─ dwc_i2c_enable(dev, 0)
    │
    ▼─────────────────────────────────────────────────────
    返回传输状态
    └─ ret = I2C_STATUS_DONE (或错误标志组合)
```

---

## 接收数据序列图

### dwc_i2c_recv() 流程

```
应用程序
    │
    │ dwc_i2c_recv(hdl, buf, len, stop)
    │
    ▼─────────────────────────────────────────────────────
    参数检查
    ├─ len <= 0? → 返回 I2C_STATUS_DONE
    └─ 否则继续
    │
    ▼─────────────────────────────────────────────────────
    初始化传输参数
    ├─ dev->txlen = 0 (无发送)
    ├─ dev->rxlen = len
    ├─ dev->xlen = 0 + len = len
    ├─ dev->wrlen = 0 (还未写读命令)
    ├─ dev->rdlen = 0 (还未读数据)
    ├─ dev->rxbuf = buf
    └─ dev->status = 0
    │
    ▼─────────────────────────────────────────────────────
    [总线激活流程 - 同发送]
    
    ▼─────────────────────────────────────────────────────
    启用中断
    └─ 写 DW_IC_INTR_MASK = DW_IC_INTR_DEFAULT_MASK
    │
    ▼─────────────────────────────────────────────────────
    等待事务完成
    │
    └─ 中断处理循环
       │
       ├─ 如 TX_EMPTY & (有读命令要写)
       │  │
       │  ├─ 循环向FIFO写读命令:
       │  │  ├─ wrlen < txlen? (无发送时不执行)
       │  │  │
       │  │  ├─ 否则写读命令:
       │  │  │  ├─ 检查是否第一个读? (wrlen == txlen)
       │  │  │  │  ├─ 是: reg = READ | RESTART
       │  │  │  │  └─ 否: reg = READ
       │  │  │  │
       │  │  │  ├─ 检查是否最后一字节? (wrlen == xlen-1)
       │  │  │  │  ├─ 是: reg |= STOP
       │  │  │  │  └─ 否: 不加STOP
       │  │  │  │
       │  │  │  └─ 写 DW_IC_DATA_CMD = reg
       │  │  │
       │  │  └─ dev->wrlen += 1
       │  │
       │  └─ 写完所有数令后禁用 TX_EMPTY 中断
       │
       ├─ 如 RX_FULL (FIFO达到阈值)
       │  │
       │  ├─ 读 DW_IC_RXFLR (FIFO中数据个数)
       │  │
       │  ├─ 循环从FIFO读数据:
       │  │  ├─ 读 DW_IC_DATA_CMD → reg
       │  │  ├─ *dev->rxbuf++ = (uint8_t)(reg & 0xFF)
       │  │  ├─ dev->rdlen += 1
       │  │  │
       │  │  └─ 重复直到FIFO为空或读完数据
       │  │
       │  └─ 继续处理其他中断
       │
       ├─ 如 STOP_DET (事务完成)
       │  │
       │  ├─ 读 DW_IC_CLR_STOP_DET (清中断)
       │  │
       │  ├─ 如果还有未读数据 (rdlen < rxlen)
       │  │  ├─ 读 DW_IC_RXFLR (剩余数据)
       │  │  │
       │  │  ├─ 循环读剩余数据:
       │  │  │  ├─ 读 DW_IC_DATA_CMD
       │  │  │  ├─ *dev->rxbuf++ = 字节
       │  │  │  └─ dev->rdlen += 1
       │  │  │
       │  │  └─ 检查数据长度是否匹配
       │  │     ├─ (rdlen + numbers) != rxlen?
       │  │     └─ 是: 设置 status |= I2C_STATUS_ERROR
       │  │
       │  ├─ 设置 dev->status |= I2C_STATUS_DONE
       │  │
       │  ├─ 禁用中断: 写 DW_IC_INTR_MASK = 0
       │  │
       │  └─ 返回 (退出等待循环)
       │
       ├─ 如 TX_ABRT
       │  [错误处理流程 - 同发送]
       │
       └─ 其他中断: 忽略或处理
    │
    ▼─────────────────────────────────────────────────────
    [同发送的后续清理]
```

---

## 中断处理流程

### dwc_i2c_process_intr() 详细流程

```
硬件I2C控制器
    │
    │ 生成中断信号
    │
    ▼─────────────────────────────────────────────────────
应用层中断处理 (系统级)
    │
    └─ 调用 dwc_i2c_process_intr(dev)
       │
       ▼─────────────────────────────────────────────────
       读取中断状态寄存器
       └─ status = 读 DW_IC_INTR_STAT
          (位定义见 dwc_i2c.h)
       │
       ▼─────────────────────────────────────────────────
       条件分支处理 (按优先级)
       │
       ├┬─ [P1] TX_ABRT (错误中断 - 最高优先级)
       │├─ if (status & DW_IC_INTR_TX_ABRT)
       ││
       │├─ 读错误源
       ││  └─ abort_source = 读 DW_IC_TX_ABRT_SOURCE
       ││
       │├─ 清错误中断
       ││  └─ 读 DW_IC_CLR_TX_ABRT (副作用清除)
       ││
       │├─ 错误映射
       ││  ├─ DW_IC_TX_ARBT_LOST?
       ││  │  └─ dev->status = DONE | ARBL
       ││  ├─ DW_IC_TX_ABRT_ADDR_NOACK?
       ││  │  └─ dev->status = DONE | ADDR_NACK
       ││  ├─ DW_IC_TX_ABRT_TXDATA_NOACK?
       ││  │  └─ dev->status = DONE | DATA_NACK
       ││  └─ 其他错误?
       ││     └─ dev->status = DONE | ABORT
       ││
       │├─ 清除其他待决中断源
       ││  └─ 写 DW_IC_INTR_MASK = 0
       ││
       │└─ return EOK (事务结束)
       │
       │
       ├┬─ [P2] STOP_DET (停止检测 - 第二优先级)
       │├─ if (status & DW_IC_INTR_STOP_DET)
       ││
       │├─ 清停止中断
       ││  └─ 读 DW_IC_CLR_STOP_DET
       ││
       │├─ 读取待读数据 (如果有接收操作)
       ││  ├─ if (dev->rdlen < dev->rxlen)
       ││  │
       ││  ├─ numbers = 读 DW_IC_RXFLR (FIFO数据计数)
       ││  │
       ││  ├─ 验证数据长度
       ││  │  ├─ (rdlen + numbers) != rxlen?
       ││  │  └─ 否: 设置 status = ERROR
       ││  │
       ││  ├─ 循环读取数据
       ││  │  ├─ for (i=0; i<numbers; i++)
       ││  │  │  ├─ reg = 读 DW_IC_DATA_CMD
       ││  │  │  ├─ *rxbuf++ = (uint8_t)(reg & 0xFF)
       ││  │  │  └─ rdlen++
       ││  │  │
       ││  │  └─ 注意: 读DATA_CMD时会自动从FIFO弹出
       ││
       │├─ 设置完成标志
       ││  └─ dev->status |= I2C_STATUS_DONE
       ││
       │├─ 禁用所有中断
       ││  └─ 写 DW_IC_INTR_MASK = 0
       ││
       │└─ return EOK
       │
       │
       ├┬─ [P3] RX_FULL (接收FIFO满)
       │├─ if (status & DW_IC_INTR_RX_FULL)
       ││
       │├─ 检查是否需要读取
       ││  ├─ if (dev->rdlen < dev->rxlen)
       ││  │
       ││  ├─ numbers = 读 DW_IC_RXFLR
       ││  │
       ││  ├─ 边界检查
       ││  │  ├─ if ((rdlen + numbers) > rxlen)
       ││  │  │  ├─ numbers = rxlen - rdlen
       ││  │  │  └─ status |= I2C_STATUS_ERROR
       ││  │  │
       ││  │  └─ 并禁用 RX_FULL 中断
       ││  │     └─ reg = 读 DW_IC_INTR_MASK
       ││  │        写 DW_IC_INTR_MASK = reg & ~RX_FULL
       ││  │
       ││  ├─ 循环读数据
       ││  │  ├─ for (i=0; i<numbers; i++)
       ││  │  │  ├─ reg = 读 DW_IC_DATA_CMD
       ││  │  │  ├─ *rxbuf++ = reg & 0xFF
       ││  │  │  └─ rdlen++
       ││  │  │
       ││  │  └─ continue (不返回, 继续处理下一个中断)
       ││  │
       ││  └─ else (无RX操作但RX_FULL?)
       ││     └─ 禁用 RX_FULL 中断 (异常情况)
       ││
       │└─ continue (手续next interrupt check)
       │
       │
       ├┬─ [P4] TX_EMPTY (发送FIFO空)
       │├─ if (status & DW_IC_INTR_TX_EMPTY)
       ││
       │├─ 循环填充FIFO (直到满或数据完)
       ││  ├─ for (i=0; i<fifo_depth; i++)
       ││  │
       ││  ├─ if (wrlen >= xlen)
       ││  │  ├─ 无更多数据要写
       ││  │  ├─ 禁用 TX_EMPTY 中断
       ││  │  │  └─ reg = 读 DW_IC_INTR_MASK
       ││  │  │     写 DW_IC_INTR_MASK = reg & ~TX_EMPTY
       ││  │  │
       ││  │  └─ break
       ││  │
       ││  ├─ if xmit_FIFO_is_full(dev)
       ││  │  └─ break (FIFO满, 停止写)
       ││  │
       ││  ├─ 决定要写的数据/命令
       ││  │  ├─ 如果 wrlen < txlen (还有发送数据)
       ││  │  │  ├─ byte = *txbuf++
       ││  │  │  ├─ if (wrlen == xlen - 1)
       ││  │  │  │  └─ cmd = byte | WRITE | STOP
       ││  │  │  └─ else
       ││  │  │     └─ cmd = byte | WRITE
       ││  │  │
       ││  │  ├─ 否则 if (wrlen == txlen && txlen > 0) (发送完, 开始接收)
       ││  │  │  ├─ (这是 sendrecv 里的转换点)
       ││  │  │  ├─ if (wrlen == xlen - 1)
       ││  │  │  │  └─ cmd = READ | RESTART | STOP
       ││  │  │  └─ else
       ││  │  │     └─ cmd = READ | RESTART
       ││  │  │
       ││  │  └─ 否则 (纯接收)
       ││  │     ├─ if (wrlen == xlen - 1)
       ││  │     │  └─ cmd = READ | STOP
       ││  │     └─ else
       ││  │        └─ cmd = READ
       ││  │
       ││  ├─ 写入FIFO
       ││  │  └─ 写 DW_IC_DATA_CMD = cmd
       ││  │
       ││  └─ wrlen++
       ││
       │└─ continue (继续检查其他中断)
       │
       │
       ├─ [其他中断源] (未在上述处理)
       │  └─ 忽略或默认处理
       │
       └─ 返回 (中断处理完成)
```

---

## 状态机图

### I2C事务状态转移

```
┌──────────────┐
│   IDLE       │
│  (等待调用)  │◄─────────────┐
└──────┬───────┘              │
       │                      │
       │ dwc_i2c_send()       │
       │ dwc_i2c_recv()       │
       │ dwc_i2c_sendrecv()   │
       │                      │
       ▼                      │
┌──────────────────────────┐  │
│   BUSY_CHECK             │  │
│  总线忙检查及激活        │  │
└──────┬───────────────────┘  │
       │ OK                    │
       ▼                       │
┌──────────────────────────┐  │
│   INTR_ENABLED           │  │
│  启用中断,准备传输      │  │
└──────┬───────────────────┘  │
       │                       │
       ├─ TX_EMPTY            │
       │  └─► 写FIFO命令      │
       │      └─ wrlen++      │
       │                      │
       ├─ RX_FULL (可能)      │
       │  └─► 读FIFO数据      │
       │      └─ rdlen++      │
       │                      │
       ├─ STOP_DET 中断       │
       │  └─► 读剩余数据      │
       │      └─► DONE ───────┘
       │
       ├─ TX_ABRT 中断        │
       │  └─► 错误映射        │
       │      └─► DONE ───────┘
       │
       └─ (循环直到DONE或ABORT)
```

---

### 错误恢复状态机

```
┌─────────────────────┐
│ NORMAL_OPERATION    │
└──────────┬──────────┘
           │
           │ 检测总线忙
           ▼
    ┌──────────────┐
    │ BUSY_DETECTED│
    └──────┬───────┘
           │
    ┌──────▼──────────────────┐
    │ Retry #1 (Try to clear) │
    └──────┬───────────────────┘
           │ Still Busy?
           ├─ YES ──┐
           │        │
           └─ NO ──┐│
                   ││
    ┌──────────────┘│
    │ Retry #2      │
    └──────┬────────┘
           │ Still Busy?
           ├─ YES ──┐
           │        │
           └─ NO ──┐│
                   ││
    ┌──────────────┘│
    │ Retry #3      │
    │ + HW Reset    │
    └──────┬────────┘
           │ Still Busy?
           ├─ YES ──┐
           │        │
           └─ NO ──┐│
                   ││
    ┌──────────────┘│
    │ Final Check   │
    └──────┬────────┘
           │ Still Busy?
           ├─ YES ─► I2C_STATUS_BUSY ◄──────┘
           │
           └─ NO ─► Return 0 (Bus Ready)
```

---

## 寄存器操作时序

### 启用/禁用序列时序

```
写 DW_IC_ENABLE = 0 (禁用)
    │
    ├─ 立即返回 (异步)
    │
    ▼ (最多25ms)
轮询 DW_IC_ENABLE_STATUS
    │
    ├─ 检查 ENABLE_MASK 位
    │  ├─ == 0? (已禁用)
    │  │  └─► 返回成功
    │  │
    │  └─ != 0? (仍启用)
    │     ├─ 延迟 25us
    │     └─ 重试
    │
    ├─ 最多1000次重试
    │
    └─ 超时? (>25ms)
       └─► 返回 -ETIMEDOUT
```

### 总线状态检查时序

```
读 DW_IC_STATUS
    │
    ├─ 检查 ACTIVITY 位
    │  └─ 设置 = 总线忙
    │
    ├─ 设置?
    │  ├─ YES ────┐
    │  │          │
    │  └─ NO ─────┼─► 返回0 (Bus Idle)
    │             │
    ├─ 延迟 100us │
    │             │
    ├─ timeout-- │
    │             │
    └─ 重试 ◄─────┘
       (最多200次 = 2秒)
```

---

## 时钟和时序配置流程

### SCL计数计算

```
输入参数:
    ic_clk = 200000 KHz (200MHz)
    tsymbol = tHIGH / tLOW / (tLOW + tF) (ns)
    tf = fall time (ns)
    
计算 HCNT (SCL高脉宽计数):
    ├─ Safe 模式 (条件 = 0):
    │  └─ HCNT = (IC_CLK * (tsymbol + tf) + 500000) / 1000000 - 3 + offset
    │
    └─ Unsafe 模式 (条件 ≠ 0):
       └─ HCNT = (IC_CLK * tsymbol + 500000) / 1000000 - 8 + offset

计算 LCNT (SCL低脉宽计数):
    └─ LCNT = (IC_CLK * (tlow + tf) + 500000) / 1000000 - 1 + offset

写入寄存器:
    ├─ DW_IC_SS_SCL_HCNT ← 标准模式 HCNT
    ├─ DW_IC_SS_SCL_LCNT ← 标准模式 LCNT
    ├─ DW_IC_FS_SCL_HCNT ← 快速模式 HCNT
    └─ DW_IC_FS_SCL_LCNT ← 快速模式 LCNT

示例 (标准模式, Safe, offset=0):
    tHIGH = 4000ns, tLOW = 4700ns, tF = 300ns
    
    HCNT = (200 * (4 + 0.3) + 0.2) - 3 = 800 - 3 = 797
    LCNT = (200 * (4.7 + 0.3) + 0.2) - 1 = 1000 - 1 = 999
```

---

## FIFO操作逻辑

### TX FIFO 写操作

```
条件: TX_EMPTY 中断 触发

循环条件:
    ├─ i < fifo_depth (避免堵塞)
    ├─ wrlen < xlen (还有数据)
    └─ !xmit_FIFO_is_full() (FIFO未满)

每次迭代:
    ├─ 检查 wrlen 位置
    │
    ├─ 如 wrlen < txlen (纯发送或sendrecv的发送阶段)
    │  ├─ byte = *txbuf++
    │  ├─ 如 wrlen == xlen - 1 (最后字节)
    │  │  └─ cmd = byte | WRITE | STOP
    │  └─ 否则
    │     └─ cmd = byte | WRITE
    │
    ├─ 否则如 wrlen == txlen && txlen > 0 (sendrecv转换点)
    │  ├─ (发送完,开始接收)
    │  ├─ 如 wrlen == xlen - 1
    │  │  └─ cmd = READ | RESTART | STOP
    │  └─ 否则
    │     └─ cmd = READ | RESTART
    │
    └─ 否则 (纯接收)
       ├─ 如 wrlen == xlen - 1
       │  └─ cmd = READ | STOP
       └─ 否则
          └─ cmd = READ

写入:
    └─ DW_IC_DATA_CMD = cmd
       wrlen++
```

### RX FIFO 读操作

```
触发条件:
    ├─ RX_FULL 中断 (FIFO >= RX_TL)
    └─ STOP_DET 中断 (读剩余)

流程:
    ├─ numbers = DW_IC_RXFLR (FIFO中字节数)
    │
    ├─ 边界检查
    │  ├─ if (rdlen + numbers) > rxlen
    │  │  ├─ 多读了!设置ERROR
    │  │  └─ numbers = rxlen - rdlen
    │  │
    │  └─ if (rdlen + numbers) < rxlen
    │     ├─ 数据不足(还会继续读)
    │     └─ 继续
    │
    └─ 循环读取 (i=0; i<numbers; i++)
       ├─ reg = DW_IC_DATA_CMD (副作用: FIFO pop)
       ├─ *rxbuf++ = (uint8_t)(reg & 0xFF)
       └─ rdlen++
```

