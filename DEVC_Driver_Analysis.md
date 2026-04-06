# Raspberry Pi 5 PL011 UART/Serial 驱动分析文档

## 目录
1. [概述](#概述)
2. [驱动架构](#驱动架构)
3. [设计原理](#设计原理)
4. [核心流程](#核心流程)
5. [关键数据结构](#关键数据结构)
6. [功能模块分析](#功能模块分析)
7. [中断处理机制](#中断处理机制)
8. [DMA传输支持](#dma传输支持)
9. [串口配置](#串口配置)
10. [错误处理与调试](#错误处理与调试)

---

## 概述

### 驱动基本信息
- **驱动名称**: PL011 UART Serial Device Driver (`devc-serpl011`)
- **版本**: 1.0（实验版本 SQML Level 1）
- **目标平台**: Raspberry Pi 5 (BCM2712)
- **硬件控制器**: ARM PL011 UART (Primary Cell Proprietary License)
- **开发方**: BlackBerry Limited (QNX Software Systems基础)
- **许可证**: Apache License 2.0
- **编程语言**: C
- **架构**: ARM 64-bit (aarch64)
- **支持功能**: 
  - 串口(UART)通信
  - 标准波特率(300-3000000)
  - 流控制(RTS/CTS, XON/XOFF)
  - DMA传输(可选)
  - 中断驱动型数据收发

### 驱动功能
该驱动为Raspberry Pi 5在QNX系统上提供高性能的UART/Serial通信支持，特别是：
- **多端口支持**：6个UART端口同时管理
- **灵活的波特率**：从300到3000000 bps
- **硬件流控制**：RTS/CTS自动管理
- **缓冲区管理**：独立的接收/发送/规范缓冲区
- **DMA加速**（可选）：用于高-throughput应用
- **调试支持**：跟踪和日志记录机制

---

## 驱动架构

### 分层架构设计

```
┌────────────────────────────────────────────────────────────────┐
│                  应用层 (User Applications)                     │
│             (终端仿真器、日志、通信程序等)                        │
└─────────────────────┬──────────────────────────────────────────┘
                      │
                      │ QNX I/O Interface (/dev/ser*)
                      ▼
┌────────────────────────────────────────────────────────────────┐
│              QNX I/O Character Driver Interface                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ iochar_send_event()   - 发送事件到应用                   │  │
│  │ iochar_process_packet()- 处理接收数据                    │  │
│  │ io-char library       - 字符设备抽象                     │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────┬──────────────────────────────────────────┘
                      │
                      │ Internal Protocol Control
                      ▼
┌────────────────────────────────────────────────────────────────┐
│         UART Driver Abstraction (devc-serpl011)               │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ create_device()       - 创建UART设备                     │  │
│  │ ser_stty()            - 设置串口参数(波特率/数据位等)     │  │
│  │ ser_attach_intr()     - 附加中断处理                     │  │
│  │ tx_interrupt()        - 发送中断处理                     │  │
│  │ rx_interrupt()        - 接收中断处理                     │  │
│  │ tto()                 - TTY输出处理                      │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────┬──────────────────────────────────────────┘
                      │
                      │ Register Access (Memory-mapped I/O)
                      ▼
┌────────────────────────────────────────────────────────────────┐
│        Hardware Abstraction Layer (HAL)                        │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ read_port()   - 从寄存器读取值 (in8/16/32)              │  │
│  │ write_port()  - 写入寄存器值 (out8/16/32)               │  │
│  │ mmap          - 内存映射硬件寄存器                       │  │
│  │ interrupt attach - 中断处理附加                         │  │
│  │ DMA library (可选) - DMA传输加速                         │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────┬──────────────────────────────────────────┘
                      │
                      │ Physical UART Bus
                      ▼
┌────────────────────────────────────────────────────────────────┐
│       PL011 UART Hardware Controller (BCM2712)                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ Physical Address: 0x10009000 (uart0), 0x1000A000 (uart1)│  │
│  │ IRQ: 36 (uart0), 37 (uart1)                              │  │
│  │ FIFO Depth: 32 bytes (TX/RX)                             │  │
│  │ Register Set: Data, Control, Status, Interrupt, ...      │  │
│  │ Signal Lines: TX, RX, RTS, CTS, DTR, DCD, DSR, RI       │  │
│  └──────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────┘
```

### 核心组件及文件映射

| 组件 | 文件 | 功能 |
|-------|------|------|
| 主程序入口 | `main.c` | 驱动初始化、选项解析、设备创建 |
| 设备管理 | `init.c` | 设备结构初始化、缓冲区分配、DMA设置 |
| 寄存器访问 | `init.c` | 读写寄存器接口 |
| 中断处理 | `intr.c` | RX/TX中断处理、DMA中断处理、定时器 |
| 发送控制 | `tto.c` | TTY输出、流控制、信号线控制 |
| 选项解析 | `options.c` | 命令行参数解析、设备配置 |
| 串口配置 | `externs.h`, `proto.h` | 数据结构定义、接口声明 |
| 平台适配 | `variant.c`, `variant.h` | 平台特定代码、中断初始化 |

---

## 设计原理

### 1. 中断驱动型数据收发

驱动采用完全的中断驱动设计：

```
应用层: write(/dev/ser0) 
    ↓
驱动缓冲区: obuf (输出缓冲)
    ↓
UART控制器 TX FIFO
    ↓
TX中断: FIFO变空
    ↓
驱动: 从obuf补充数据到FIFO
    ↓
硬件: 继续发送
```

**优点**：
- 不浪费CPU轮询
- 高效的缓冲区管理
- 自适应流控制

### 2. QNX字符驱动框架集成

驱动集成了QNX的 `io-char` 库，提供：
- 规范模式(Canonical mode)支持
- 行编辑功能
- 软件流控制(XON/XOFF)
- 信号处理(SIGINT等)

### 3. 灵活的缓冲区设计

三层缓冲区结构：
```
┌─────────────────────────┐
│  输入缓冲 (ibuf)        │  应用读取数据
│  大小: 2048字节(可配)   │
│  高水位: 自动流控       │
└────────┬────────────────┘
         │
         │ 规范处理
         ▼
┌─────────────────────────┐
│  规范缓冲 (cbuf)        │  行编辑、删除行等
│  大小: 256字节(可配)    │
└────────┬────────────────┘
         │
         │ 行/字符输入
         ▼
┌─────────────────────────┐
│  输出缓冲 (obuf)        │  应用写入数据
│  大小: 2048字节(可配)   │
└─────────────────────────┘
```

### 4. 多端口支持架构

```
TTYCTRL (控制器)
    ▼
DEV_PL011 [0] → uart0 (uart0.0)
    ↓
DEV_PL011 [1] → uart1 (uart1.0)
    ↓
DEV_PL011 [2] → uart2 (uart2.0)
    ↓
...最多6个端口
```

每个设备有独立的：
- 基地址和中断号
- 中断ID (iid)
- FIFO深度配置
- 波特率、数据位、奇偶校验
- 流控制参数

### 5. DMA传输抽象(可选)

当启用DMA时：
```
应用 → obuf (输出缓冲)
        ↓
    DMA引擎设置
        ↓
物理地址 buf_tx → UART
        ↓
DMA完成中断
        ↓
下一批数据
```

优点：
- 减少CPU干预
- 适合高速串口(>921600 bps)
- 可配置传输大小

---

## 核心流程

### 1. 驱动启动流程

```
main(argc, argv)
    │
    ├─ ttc(TTC_INIT_PROC, &ttyctrl, DEFAULT_PRIORITY)
    │  └─ 初始化TTY控制器结构
    │     ├─ 设置最大设备数: 6
    │     ├─ 设置默认优先级: 24
    │     └─ 准备资源管理器
    │
    ├─ enable_perms()
    │  └─ 获取系统权限(I/O访问、中断等)
    │
    ├─ options(argc, argv)
    │  │  解析命令行参数
    │  │
    │  ├─ 迭代找设备
    │  │  ├─ query_default_device(dip, link)
    │  │  │  ├─ uart0: port=0x10009000, intr=36
    │  │  │  └─ uart1: port=0x1000A000, intr=37
    │  │  │
    │  │  ├─ 创建设备初始化结构 (TTYINIT_PL011)
    │  │  │  ├─ 输入缓冲大小: 2048
    │  │  │  ├─ 输出缓冲大小: 2048
    │  │  │  ├─ 规范缓冲大小: 256
    │  │  │  ├─ 默认波特率: 38400
    │  │  │  ├─ 默认时钟: 24MHz
    │  │  │  └─ 默认分频: 16
    │  │  │
    │  │  └─ create_device(&devinit)
    │  │     └─ [设备创建流程]
    │  │
    │  └─ 统计设备数 numports
    │
    ├─ 检查: numports > 0?
    │  ├─ 否 → 打印错误并退出
    │  └─ 是 → 继续
    │
    └─ ttc(TTC_INIT_START, &ttyctrl, 0)
       └─ 启动I/O资源管理器
          ├─ 注册设备 (/dev/ser0, /dev/ser1, ...)
          └─ 开始处理客户端请求
```

### 2. 设备创建流程

```
create_device(TTYINIT_PL011 *dip)
    │
    ├─ malloc(DEV_PL011)
    │  └─ 分配设备结构
    │
    ├─ 内存映射寄存器
    │  ├─ mmap_device_memory(phys_addr, ...)
    │  └─ dev->base = 虚拟地址
    │
    ├─ 分配缓冲区
    │  ├─ ibuf (输入): 2048 字节
    │  ├─ obuf (输出): 2048 字节
    │  └─ cbuf (规范): 256 字节
    │
    ├─ 复制配置参数
    │  ├─ 波特率、数据位、奇偶校验
    │  ├─ 流控制标志
    │  ├─ 中断号、时钟频率
    │  └─ 设备名 (/dev/ser0)
    │
    ├─ DMA初始化 (如果启用)
    │  ├─ get_dmafuncs()
    │  │  └─ 获取DMA库函数
    │  │
    │  ├─ dev->dmafuncs.init()
    │  │  └─ 初始化DMA引擎
    │  │
    │  ├─ 分配DMA缓冲区
    │  │  ├─ buf_rx[2] - RX双缓冲
    │  │  └─ buf_tx - TX缓冲
    │  │
    │  └─ 创建DMA通道
    │     ├─ 物理地址映射
    │     ├─ 传输长度设置
    │     └─ 中断事件关联
    │
    ├─ 初始化UART控制寄存器
    │  ├─ RX FIFO禁用(等待配置)
    │  ├─ TX FIFO阈值设置
    │  ├─ 中断掩码清除
    │  └─ UART禁用
    │
    ├─ 计算波特率参数
    │  ├─ IBRD = clk / (16 * baud)
    │  ├─ FBRD = ((clk % (16 * baud)) * 64 + (16 * baud / 2)) / (16 * baud)
    │  └─ 设置LCR_H(数据位、停止位、奇偶校验)
    │
    ├─ 设置串口参数 ser_stty(dev)
    │  ├─ 写IBRD、FBRD寄存器
    │  ├─ 写LCR_H寄存器
    │  ├─ 配置流控制
    │  └─ 启用UART
    │
    ├─ 附加中断处理 ser_attach_intr(dev)
    │  ├─ InterruptAttachEvent(intr, &event, flags)
    │  ├─ 设置中断处理函数
    │  └─ dev->iid = 中断ID
    │
    └─ 添加到设备链表
       └─ ttyctrl.device_list
```

### 3. 发送数据流程

```
应用程序
    │
    │ write("/dev/ser0", data, len)
    │
    ▼─────────────────────────────────────
IOCHAR库: iochar_output()
    │
    ├─ 数据复制到 obuf
    └─ 调用 tto(TTO_DATA)
       │
       ▼─────────────────────────────────
tto(TTYDEV *ttydev, TTO_DATA, arg1)
    │
    ├─ [流控制检查]
    │  ├─ 是否启用发送流控制?
    │  ├─ 对方是否请求停止(XOFF)?
    │  └─ 是否使用DMA发送?
    │
    ├─ 如果DMA启用:
    │  │
    │  ├─ 检查DMA状态
    │  │  └─ DMA_TX_ACTIVE? 等待完成
    │  │
    │  ├─ 填充DMA缓冲区
    │  │  ├─ byte = *obuf++
    │  │  └─ buf_tx[byte_cnt++] = byte
    │  │
    │  └─ 当DMA缓冲区满或无更多数据
    │     ├─ 调用DMA引擎
    │     ├─ 设置物理地址: src=buf_tx, dst=UART_FR
    │     ├─ 设置长度: byte_cnt
    │     ├─ 发起传输
    │     └─ 设置 DMA_TX_ACTIVE
    │
    └─ 如果无DMA (PIO模式):
       │
       ├─ 循环填充TX FIFO
       │  ├─ 读UART_FR寄存器
       │  │  └─ 检查TXFF(TX FIFO Full)位
       │  │
       │  ├─ FIFO未满?
       │  │  ├─ byte = *obuf++
       │  │  └─ 写 UART_DR = byte
       │  │
       │  └─ FIFO满?
       │     └─ 停止填充,启用TX中断
       │
       ├─ 启用TX中断 (TIS_TX位)
       │  └─ 写 UART_IMSC寄存器
       │
       └─ 返回已发送字节数
```

### 4. 接收数据流程

```
硬件UART
    │
    │ RX触发(数据到达或超时)
    │
    ▼─────────────────────────────────
硬件中断
    │
    └─ 中断处理程序
       │
       ├─ 读 UART_MIS (中断状态)
       │  └─ 检查RxInt位
       │
       └─ 调用 rx_interrupt(dev)
          │
          ├─ 循环处理FIFO中的数据
          │  │
          │  ├─ 读 UART_DR
          │  │  ├─ byte = UART_DR & 0xFF
          │  │  ├─ 错误标志 = UART_DR[11:8]
          │  │  │  ├─ OE (过溢)
          │  │  │  ├─ BE (中止)
          │  │  │  ├─ PE (奇偶校验错误)
          │  │  │  └─ FE (帧错误)
          │  │  │
          │  │  └─ 错误处理
          │  │     ├─ 计数错误
          │  │     ├─ 日志记录
          │  │     └─ 继续处理
          │  │
          │  ├─ 检查缓冲区空间
          │  │  └─ ibuf->cnt还有空间?
          │  │
          │  ├─ 添加到输入缓冲
          │  │  └─ *ibuf->head++ = byte
          │  │
          │  └─ 检查高水位
          │     ├─ ibuf->cnt >= highwater?
          │     │  └─ 启用RTS流控(如果需要)
          │     │
          │     └─ 发送事件到应用
          │        └─ iochar_send_event(&dev->tty)
          │
          └─ 如果无DMA
             └─ 返回 (中断后更新)
```

### 5. 中断处理流程

```
硬件中断 (IRQ 36/37)
    │
    ├─ 读 UART_MIS (Masked Interrupt Status)
    │  ├─ RxInt (接收)
    │  ├─ TxInt (发送)
    │  ├─ RtInt (接收超时)
    │  ├─ FeInt (帧错误)
    │  ├─ PeInt (奇偶校验错误)
    │  ├─ BeInt (中止)
    │  └─ OeInt (过溢)
    │
    ├─ [条件分支处理]
    │
    ├─ 如 RxInt 或 RtInt
    │  │
    │  └─ rx_interrupt(dev)
    │     ├─ 读UART_FR检查RXFE(RX FIFO Empty)
    │     ├─ 循环读FIFO
    │     ├─ 检查错误标志
    │     ├─ 添加到ibuf
    │     └─ 返回true(有新数据)
    │
    ├─ 如 TxInt
    │  │
    │  └─ tx_interrupt(dev)
    │     ├─ 读UART_FR检查TXFE(TX FIFO Empty)
    │     ├─ TX FIFO有空?
    │     │  ├─ 从obuf读取数据
    │     │  ├─ 写入UART_DR
    │     │  └─ 重复填充
    │     ├─ obuf空?
    │     │  └─ 禁用TX中断
    │     └─ 返回true(已发送)
    │
    ├─ 如 FeInt/PeInt/BeInt/OeInt (重点)
    │  │
    │  └─ 错误处理
    │     ├─ 清除错误中断位
    │     ├─ 计数统计
    │     ├─ 日志记录
    │     └─ 错误回调 (如果配置)
    │
    └─ [收集事件状态]
       │
       ├─ 如果新数据到达
       │  └─ iochar_send_event(&dev->tty)
       │     └─ 唤醒等待读取的应用
       │
       └─ 返回 IST_UNBLOCK (继续中断处理)
```

---

## 关键数据结构

### DEV_PL011 - UART设备控制块

```c
typedef struct dev_pl011 {
    // 字符设备接口
    TTYDEV              tty;                // QNX TTY数据结构
    struct dev_pl011    *next;              // 设备链表指针
    
    // 硬件配置
    uintptr_t           base;               // 虚拟基地址
    unsigned            intr;               // 中断号(36或37)
    int                 port_size;          // 端口大小(字节宽度)
    int                 iid;                // 中断ID
    
    // FIFO配置
    int                 fifosize;           // FIFO深度(32字节)
    unsigned            fifo;               // FIFO配置标志
    unsigned            fifo_reg;           // FIFO寄存器值(1/2/1/4)
    
    // 波特率与时钟
    unsigned            clk;                // 输入时钟(Hz) - 24MHz
    unsigned            div;                // 分频器(16)
    unsigned            baud;               // 当前波特率
    
    // 寄存器缓存
    unsigned            lcr_h;              // 线控制寄存器H值
    unsigned            ibrd;               // 整数波特率除数
    unsigned            fbrd;               // 小数波特率除数
    unsigned            cr;                 // 控制寄存器
    unsigned            imr;                // 中断掩码寄存器
    
    // 模式与配置
    unsigned            loopback;           // 环回模式(测试)
    unsigned            drt;                // 数据就绪超时(ms)
    unsigned            prio;               // 驱动优先级
    unsigned            is_debug_console;   // 是否调试控制台
    
    // DMA配置
    unsigned            dma_enable;         // DMA使能标志
    unsigned            dma_state;          // DMA状态(ACTIVE/IDLE)
    unsigned            dma_request_rx;     // RX DMA请求号
    unsigned            dma_request_tx;     // TX DMA请求号
    unsigned            dma_xfer_size;      // 单次传输大小
    unsigned            chan_rx;            // RX DMA通道号
    unsigned            chan_tx;            // TX DMA通道号
    
    // DMA运行时数据
#ifdef USE_DMA
    dma_addr_t          buf_rx[2];          // RX双缓冲物理地址
    dma_addr_t          src_rx;             // RX源地址
    dma_addr_t          buf_tx;             // TX缓冲物理地址
    dma_addr_t          dst_tx;             // TX目标地址
    dma_transfer_t      tinfo_tx;           // TX传输信息
    dma_transfer_t      tinfo_rx;           // RX传输信息
    int                 tx_chid;            // TX DMA通道ID
    int                 tx_coid;            // TX通道连接ID
    struct sigevent     event;              // DMA完成事件
    unsigned            buffer0;            // 当前缓冲区选择
    void                *dma_chn_tx;        // TX DMA通道句柄
    void                *dma_chn_rx;        // RX DMA通道句柄
    dma_functions_t     dmafuncs;           // DMA函数指针表
    pthread_mutex_t     dma_lock;           // DMA操作同步锁
#endif
} DEV_PL011;
```

### TTYINIT_PL011 - 设备初始化结构

```c
typedef struct ttyinit_pl011 {
    // 标准TTY初始化参数(来自io-char)
    TTYINIT     tty;
    
    // 标准字段:
    //   uint32_t port;          物理I/O地址
    //   uint32_t intr;          中断号
    //   uint32_t baud;          默认波特率
    //   uint32_t isize;         输入缓冲大小
    //   uint32_t osize;         输出缓冲大小
    //   uint32_t csize;         规范缓冲大小
    //   uint32_t clk;           输入时钟频率
    //   uint32_t div;           分频器
    //   char     name[16];      设备名(/dev/ser)
    //   uint32_t fifo;          FIFO标志
    
    // PL011特定参数
    unsigned    drt;             // 数据就绪超时(ms)
    unsigned    prio;            // 优先级
    unsigned    loopback;        // 环回模式标志
    unsigned    fifo_reg;        // FIFO配置(1/2/1/4)
    
    // DMA参数
    unsigned    dma_enable;      // DMA使能
    unsigned    dma_request_rx;  // RX DMA请求号
    unsigned    dma_request_tx;  // TX DMA请求号
    unsigned    dma_xfer_size;   // 传输大小
    unsigned    chan_rx;         // RX通道
    unsigned    chan_tx;         // TX通道
    unsigned    is_debug_console;// 调试控制台
} TTYINIT_PL011;
```

### TTYDEV - QNX字符设备结构(来自io-char库)

```c
typedef struct ttydev {
    // 缓冲区
    struct {
        unsigned char *buff;    // 缓冲区指针
        unsigned char *head;    // 当前写入位置
        unsigned char *tail;    // 当前读取位置
        int            size;    // 缓冲区大小
        int            cnt;     // 当前数据计数
    } ibuf, obuf, cbuf;         // 输入、输出、规范缓冲区
    
    // 串口参数
    unsigned       baud;        // 波特率
    unsigned       fifo;        // FIFO标志
    unsigned       flags;       // 驱动标志(EDIT_INSERT等)
    unsigned       c_cflag;     // 控制标志
    unsigned       c_iflag;     // 输入标志
    unsigned       c_lflag;     // 本地标志
    unsigned       c_oflag;     // 输出标志
    
    // 流控制
    unsigned       highwater;   // 高水位(启用流控)
    unsigned       lowwater;    // 低水位(禁用流控)
    int            un.s.tx_tmr; // 发送超时计数器
    
    // 统计和调试
    unsigned       verbose;     // 详细级别
    char           name[16];    // 设备名(/dev/ser0)
} TTYDEV;
```

---

## 功能模块分析

### 1. 寄存器访问模块

**函数**: `read_port()`, `write_port()`

**功能**: 提供抽象的寄存器读写接口，支持8/16/32位访问

```c
void write_port(DEV_PL011 *dev, int reg, unsigned val)
{
    switch (dev->port_size) {
        case 2:
            out32(dev->base + reg, val);  // 32位写
            break;
        case 1:
            out16(dev->base + reg, val);  // 16位写
            break;
        default:
            out8(dev->base + reg, val);   // 8位写
            break;
    }
}
```

**使用场景**:
- 设置波特率寄存器 (UART_IBRD, UART_FBRD)
- 读取状态标志 (UART_FR, UART_MIS)
- 控制硬件信号 (UART_CR 中的DTR/RTS)

---

### 2. 中断附加模块

**函数**: `ser_attach_intr()`

**功能**: 将硬件中断连接到驱动程序

```
ser_attach_intr(DEV_PL011 *dev)
    │
    ├─ 创建中断事件 (sigevent)
    │  └─ 指向中断处理函数
    │
    ├─ InterruptAttachEvent(irq, &event, flags)
    │  └─ dev->iid = 中断ID
    │
    ├─ 启用中断掩码
    │  └─ 写UART_IMSC寄存器
    │
    └─ variant_intr_init(dev)
       └─ 平台特定初始化
```

---

### 3. 串口参数配置模块

**函数**: `ser_stty()`

**功能**: 配置UART控制器的所有串口参数

```
ser_stty(DEV_PL011 *dev)
    │
    ├─ 禁用UART (CR &= ~UART_CR_UARTEN)
    │
    ├─ 计算波特率参数
    │  ├─ ibrd = clk / (16 * baud)
    │  └─ fbrd = ((clk % (16 * baud)) * 64 + ...) / (16 * baud)
    │
    ├─ 配置线控制
    │  ├─ 数据位 (5/6/7/8) → LCR_H[6:5]
    │  ├─ 停止位 (1/2) → LCR_H[3]
    │  ├─ 奇偶校验类型 → LCR_H[2:1]
    │  └─ 帧错误处理 → LCR_H[4]
    │
    ├─ 配置FIFO
    │  ├─ FIFO启用 → CR[4]
    │  ├─ RX触发级 → IFLS[5:3]
    │  └─ TX触发级 → IFLS[2:0]
    │
    ├─ 配置流控制
    │  ├─ RTS使能 → CR[14]
    │  ├─ CTS使能 → CR[15]
    │  └─ DTR(如果支持) → CR[0]
    │
    ├─ 配置中断掩码
    │  ├─ RX就绪
    │  ├─ TX空
    │  └─ 错误中断
    │
    └─ 启用UART (CR |= UART_CR_UARTEN)
```

---

### 4. 发送数据处理模块

**函数**: `tx_interrupt()`, `tto(TTO_DATA)`

**核心逻辑**:
```
当应用程序写入数据时
    │
    ├─ 数据进入 obuf (输出缓冲)
    │
    ├─ 检查流控制状态
    │  ├─ 对方要求停止(XOFF)? → 不发送
    │  └─ 否则继续
    │
    ├─ 填充UART TX FIFO
    │  ├─ 读UART_FR (帧寄存器)
    │  ├─ 检查TXFF (TX FIFO Full)
    │  │  └─ 如满则停止
    │  └─ 每次写一字节到UART_DR
    │
    ├─ FIFO填满?
    │  └─ 启用TxInt中断
    │
    └─ TX FIFO变空时
       └─ txInt中断触发
          ├─ 继续填充FIFO
          ├─ obuf变空?
          │  └─ 禁用TxInt中断
          └─ 通知应用(写完成)
```

---

### 5. 接收数据处理模块

**函数**: `rx_interrupt()`

**核心逻辑**:
```
硬件UART中断 (RxInt 或 RtInt)
    │
    └─ rx_interrupt(DEV_PL011 *dev)
       │
       ├─ 循环条件: RXFE位未设置(FIFO非空)
       │
       ├─ 读 UART_DR (数据+错误标志)
       │  ├─ byte = DR & 0xFF (实际数据)
       │  └─ errors = DR[11:8]
       │     ├─ OE (过溢)
       │     ├─ BE (中止)
       │     ├─ PE (奇偶校验)
       │     └─ FE (帧错误)
       │
       ├─ 错误处理
       │  ├─ 清除error_flag
       │  ├─ 计数统计
       │  └─ 必要时日志
       │
       ├─ 缓冲区空间检查
       │  ├─ ibuf满?
       │  │  └─ 启用RTS流控
       │  └─ 否则继续
       │
       └─ 添加到ibuf
          ├─ *ibuf.head++ = byte
          ├─ ibuf.cnt++
       └─ 返回true(有新数据)
```

---

### 6. DMA传输模块(可选)

**启用条件**: 编译时 `#define USE_DMA`

**流程**:
```
初始化:
    ├─ get_dmafuncs() → 获取DMA库
    ├─ dmafuncs.init() → 初始化
    ├─ 分配DMA缓冲区
    │  └─ dma_addr_t buf_tx/rx
    └─ 创建DMA通道

发送(DMA_TX):
    ├─ 填充buf_tx缓冲
    │  └─ 从obuf复制数据
    ├─ 配置DMA传输
    │  ├─ src = buf_tx物理地址
    │  ├─ dst = UART_DR
    │  └─ length = 字节数
    ├─ 发起DMA传输
    │  └─ dmafuncs.xfer_start()
    └─ 等待DMA完成中断

接收(DMA_RX):
    ├─ 配置DMA传输
    │  ├─ src = UART_DR
    │  └─ dst = buf_rx[双缓冲]
    ├─ 发起DMA传输
    │  └─ 硬件自动填充缓冲
    └─ DMA完成中断
       └─ 处理数据到ibuf
```

---

## 中断处理机制

### 中断源及优先级

| 中断源 | 位 | 含义 | 处理 |
|--------|-----|------|------|
| RxInt | 4 | RX FIFO到达触发级 | rx_interrupt() |
| TxInt | 5 | TX FIFO下降到触发级 | tx_interrupt() |
| RtInt | 6 | RX超时(无新数据一段时间) | rx_interrupt() |
| FeInt | 7 | 帧错误 | 清中断,计数 |
| PeInt | 8 | 奇偶校验错误 | 清中断,计数 |
| BeInt | 9 | 中止条件 | 清中断,计数 |
| OeInt | 10 | FIFO过溢 | 清中断,计数 |

### 中断处理架构

```
硬件中断 (IRQ 36/37)
    │
    ├─ ISR (中断服务程序)
    │  ├─ 读UART_MIS (中断状态)
    │  ├─ 清中断标志
    │  └─ 返回 IST_UNBLOCK
    │
    └─ 高级中断处理线程
       │
       ├─ 条件分支
       │
       ├─ 如RxInt/RtInt
       │  └─ rx_interrupt(dev)
       │     ├─ 读FIFO全部数据
       │     └─ 添加到ibuf
       │
       ├─ 如TxInt
       │  └─ tx_interrupt(dev)
       │     ├─ 填充TX FIFO
       │     └─ 注意obuf是否为空
       │
       ├─ 如Fe/Pe/Be/OeInt
       │  └─ 错误计数和日志
       │
       └─ 发送事件到应用
          └─ iochar_send_event(&dev->tty)
```

---

## DMA传输支持

### DMA配置参数

```c
// 命令行参数(options.c)
-D enable           // 启用DMA (1=RX, 2=TX, 3=双向)
-Rd <request_id>    // RX DMA请求号
-Rt <request_id>    // TX DMA请求号
-Rc <chan_id>       // RX DMA通道
-Rs <size>          // DMA传输大小
```

### DMA缓冲区管理

```
双缓冲RX:
    buf_rx[0] (物理地址)
        ↓
    DMA填充
        ↓
    若满→缓冲区1,启动缓冲区0
    若有新数据→缓冲区切换

单缓冲TX:
    buf_tx (物理地址)
        ↓
    DMA传输
        ↓
    完成→填充下一批
```

### DMA优势

- **高速率支持**: >921600 bps
- **CPU卸载**: 减少中断和数据复制
- **恒定延迟**: 可预测的传输时间
- **多端口并行**: 各端口独立DMA通道

---

## 串口配置

### 波特率计算

```
公式:
    UART_CLK = 24 MHz (Raspberry Pi 5默认)
    Divisor = UART_CLK / (16 × BaudRate)
    IBRD = floor(Divisor)
    FBRD = round((Divisor - IBRD) × 64)

示例 (38400 bps):
    Divisor = 24000000 / (16 × 38400) = 39.0625
    IBRD = 39
    FBRD = round(0.0625 × 64) = 4
    实际波特率 = 24000000 / (16 × (39 + 4/64)) ≈ 38400
```

### 线控制寄存器(LCR_H)

| 位 | 字段 | 值 | 含义 |
|----|------|-----|------|
| [1:0] | WLEN | 00-11 | 5-8位数据 |
| [2] | FEN | 0/1 | FIFO禁用/启用 |
| [3] | STP2 | 0/1 | 1/2个停止位 |
| [4] | EPS | 0/1 | 奇偶校验偶/奇 |
| [5] | PEN | 0/1 | 奇偶校验禁用/启用 |
| [6] | BRK | 0/1 | 无中止/发送中止 |
| [7] | SPS | 0/1 | 固定奇偶/粘性奇偶 |

### 控制寄存器(CR)

| 位 | 字段 | 值 | 含义 |
|----|------|-----|------|
| [0] | UARTEN | 0/1 | UART禁用/启用 |
| [8] | RXE | 0/1 | 接收禁用/启用 |
| [9] | TXE | 0/1 | 发送禁用/启用 |
| [14] | RTS | 0/1 | RTS低/高 |
| [15] | CTS | 0/1 | CTS禁用/启用 |

---

## 错误处理与调试

### 错误类型

| 错误 | 标志位 | 含义 | 恢复 |
|-----|-------|------|------|
| 过溢 (OE) | UART_DR[11] | RX FIFO满,新数据丢失 | 减速接收应用 |
| 中止 (BE) | UART_DR[10] | 接收到中止条件 | 恢复同步 |
| 奇偶 (PE) | UART_DR[9] | 奇偶校验失败 | 请求重传 |
| 帧 (FE) | UART_DR[8] | 帧格式错误 | 检查波特率 |

### 调试支持

```c
// 编译时启用详细跟踪
#define MDEBUG

// 跟踪事件
TraceEvent(_NTO_TRACE_INSERTSUSEREVENT, event_id, arg1, arg2);

// 系统日志
slogf(_SLOG_SETCODE(_SLOGC_CHAR, 0), _SLOG_ERROR,
      "%s: error message", __FUNCTION__);
```

### 常见问题排查

| 问题 | 原因 | 解决方案 |
|------|------|---------|
| 无法接收数据 | RX禁用或信号问题 | 检查RXE且RX引脚连接 |
| 数据损失 | FIFO溢出 | 增加缓冲区,启用流控制 |
| 波特率不对 | IBRD/FBRD计算错误 | 验证时钟频率(clk参数) |
| 流控制不工作 | RTS/CTS未启用 | 检查c_cflag中CRTSCTS |

---

## 总结

该PL011 UART驱动是一个功能完整、设计合理的串口通信驱动,具有以下特点:

✅ **高效架构**: 完全中断驱动,无忙轮询
✅ **灵活配置**: 支持多种波特率、数据格式
✅ **高性能**: 可选DMA加速,适合高速率应用
✅ **QNX集成**: 与io-char框架紧密集成,支持规范模式
✅ **多端口**: 单驱动支持6个UART端口
✅ **可靠通信**: 完整的错误检测和流控制
✅ **易于维护**: 清晰的代码结构和丰富的调试支持

通过该驱动,Raspberry Pi 5可以在QNX系统上进行稳定、高效的串口通信。
