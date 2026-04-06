# DEVC驱动流程图与硬件规格参考

## 目录
1. [驱动启动序列图](#驱动启动序列图)
2. [设备创建流程](#设备创建流程)
3. [数据发送/接收序列](#数据发送接收序列)
4. [中断处理流程](#中断处理流程)
5. [PL011 UART寄存器](#pl011-uart寄存器)
6. [硬件接口时序](#硬件接口时序)

---

## 驱动启动序列图

### 主程序执行流程

```
start
    │
    ▼─────────────────────────────────────
初始化TTY控制器
    │
    ├─ ttyctrl.max_devs = 6
    ├─ 清除设备链表
    └─ 初始化互斥锁和信号量
    │
    ▼─────────────────────────────────────
获取执行权限
    │
    └─ enable_perms()
       ├─ 获取IO权限 (I/O instructions)
       ├─ 获取中断处理权限
       └─ 设置能力标志
    │
    ▼─────────────────────────────────────
解析命令行参数
    │
    └─ options(argc, argv)
       │
       ├─ 初始化默认设备参数 (TTYINIT_PL011)
       │  ├─ port = 0,    intr = 0
       │  ├─ baud = 38400
       │  ├─ isize = 2048, osize = 2048, csize = 256
       │  ├─ clk = 24000000 (24MHz)
       │  ├─ div = 16
       │  ├─ drt = 0,  prio = 0
       │  └─ dma_enable = 0
       │
       ├─ 循环处理参数
       │  ├─ -p <port> : 指定I/O端口
       │  ├─ -i <irq>  : 指定中断号
       │  ├─ -b <baud> : 波特率 (38400)
       │  ├─ -d <data> : 数据位 (8)
       │  ├─ -s <stop> : 停止位 (1)
       │  ├─ -p <parity> : 奇偶校验 (n)
       │  ├─ -c <clk> : 输入时钟 (24000000)
       │  ├─ -D <dma> : DMA配置
       │  └─ -v : 详细输出
       │
       ├─ 查询默认设备
       │  │
       │  ├─ 调用 query_default_device(dip, 0)
       │  │  │
       │  │  ├─ case 0:
       │  │  │  ├─ dip->tty.port = 0x10009000
       │  │  │  ├─ dip->tty.intr = 36
       │  │  │  └─ return (void *)1
       │  │  │
       │  │  └─ case 1:
       │  │     ├─ dip->tty.port = 0x1000A000
       │  │     ├─ dip->tty.intr = 37
       │  │     └─ return (void *)2
       │  │
       │  ├─ 如果返回NULL → 跳过该设备
       │  │
       │  └─ 创建设备 create_device(dip)
       │     └─ [参见设备创建流程]
       │
       └─ 返回已创建设备数 numports
    │
    ▼─────────────────────────────────────
检查设备数量
    │
    ├─ numports > 0 ?
    │  ├─ 否: 打印错误 "No serial ports found"
    │  │       exit(0)
    │  │
    │  └─ 是: 继续
    │
    ▼─────────────────────────────────────
启动I/O资源管理器
    │
    └─ ttc(TTC_INIT_START, &ttyctrl, 0)
       │
       ├─ 注册所有设备给I/O框架
       │  ├─ /dev/ser0
       │  ├─ /dev/ser1
       │  ├─ /dev/ser2
       │  ├─ ...
       │  └─ /dev/serN
       │
       ├─ 创建I/O线程
       │  └─ 优先级: 24 (DEFAULT_PRIORITY)
       │
       ├─ 监听客户端请求
       │  ├─ open, read, write, ioctl...
       │  └─ 分发到对应处理函数
       │
       └─ timer_thread启动 (如启用DMA)
          ├─ 周期 = drt 毫秒
          ├─ 发送脉冲事件
          └─ 用于RX超时处理

    │
    ▼─────────────────────────────────────
驱动运行
    │
    └─ 循环处理客户端请求
       ├─ 读取请求
       ├─ 调用处理函数
       ├─ 返回响应
       └─ [直到驱动卸载]
```

---

## 设备创建流程

### create_device() 详细步骤

```
create_device(TTYINIT_PL011 *dip)
    │
    ▼──────────────────────────────────────────
step 1: 分配设备结构
    │
    └─ dev = malloc(sizeof(DEV_PL011))
       ├─ 清零内存
       └─ 初始化设备指针
    │
    ▼──────────────────────────────────────────
step 2: 内存映射寄存器
    │
    └─ 虚拟地址 = mmap_device_memory(
         physaddr   = dip->tty.port  (0x10009000)
         size       = 0x1000 (4KB)
         prot       = PROT_READ | PROT_WRITE | PROT_NOCACHE
         flags      = MAP_SHARED
         offset     = 0)
       │
       ├─ 成功?
       │  └─ dev->base = 虚拟地址
       │
       └─ 失败?
          └─ printf("Memory MAP failed")
             exit(1)
    │
    ▼──────────────────────────────────────────
step 3: 分配缓冲区
    │
    ├─ 输入缓冲 (ibuf)
    │  ├─ dev->tty.ibuf.buff = malloc(isize = 2048)
    │  ├─ dev->tty.ibuf.head = buff
    │  ├─ dev->tty.ibuf.tail = buff
    │  └─ dev->tty.ibuf.cnt = 0
    │
    ├─ 输出缓冲 (obuf)
    │  ├─ dev->tty.obuf.buff = malloc(osize = 2048)
    │  ├─ dev->tty.obuf.head = buff
    │  ├─ dev->tty.obuf.tail = buff
    │  └─ dev->tty.obuf.cnt = 0
    │
    └─ 规范缓冲 (cbuf)
       ├─ dev->tty.cbuf.buff = malloc(csize = 256)
       ├─ dev->tty.cbuf.head = buff
       ├─ dev->tty.cbuf.tail = buff
       └─ dev->tty.cbuf.cnt = 0
    │
    ▼──────────────────────────────────────────
step 4: 复制配置参数
    │
    ├─ dev->tty.baud = dip->tty.baud (38400)
    ├─ dev->tty.fifo = dip->tty.fifo
    ├─ dev->tty.c_cflag = dip->tty.c_cflag
    ├─ dev->tty.c_iflag = dip->tty.c_iflag
    ├─ dev->tty.c_lflag = dip->tty.c_lflag
    ├─ dev->tty.c_oflag = dip->tty.c_oflag
    ├─ dev->intr = dip->tty.intr (36/37)
    ├─ dev->clk = dip->tty.clk (24000000)
    ├─ dev->div = dip->tty.div (16)
    ├─ dev->fifosize = FIFOSIZE (32)
    ├─ dev->drt = dip->drt
    ├─ dev->prio = dip->prio
    └─ dev->loopback = dip->loopback
    │
    ▼──────────────────────────────────────────
step 5: 初始化UART寄存器
    │
    ├─ 禁用UART
    │  └─ write_port(dev, PL011_CR, 0)
    │
    ├─ 清除中断
    │  └─ write_port(dev, PL011_ICR, 0x7FF)
    │
    ├─ 配置FIFO
    │  └─ fifo_reg = (depth/2) << 4
    │
    └─ 禁用中断掩码
       └─ write_port(dev, PL011_IMSC, 0)
    │
    ▼──────────────────────────────────────────
step 6: 计算波特率参数
    │
    ├─ divisor = clk / (16 * baud)
    │  │
    │  └─ 例: 24000000 / (16 * 38400) = 39.0625
    │
    ├─ dev->ibrd = floor(divisor) = 39
    │
    └─ dev->fbrd = round((divisor - ibrd) * 64)
       └─ round(0.0625 * 64) = 4
    │
    ▼──────────────────────────────────────────
step 7: 初始化串口参数
    │
    └─ ser_stty(dev)
       │
       ├─ 禁用UART (CR = 0)
       │
       ├─ 写入波特率参数
       │  ├─ write_port(dev, PL011_IBRD, ibrd)
       │  └─ write_port(dev, PL011_FBRD, fbrd)
       │
       ├─ 配置线控制寄存器 (LCR_H)
       │  ├─ 数据位: 8 → WLEN = 11
       │  ├─ 停止位: 1 → STP2 = 0
       │  ├─ 奇偶调查: 无 → PEN = 0
       │  └─ FIFO启用: FEN = 1
       │
       ├─ 配置控制寄存器 (CR)
       │  ├─ RXE = 1 (接收启用)
       │  ├─ TXE = 1 (发送启用)
       │  ├─ UARTEN = 1 (UART启用)
       │  └─ RTS/CTS = 1 (流控制启用)
       │
       └─ 配置中断掩码 (IMSC)
          ├─ RxInt (接收)
          ├─ TxInt (发送)
          ├─ RtInt (接收超时)
          └─ 错误中断
    │
    ▼──────────────────────────────────────────
step 8: DMA初始化 (如启用)
    │
    ├─ get_dmafuncs(&dmafuncs, size)
    │  └─ 加载DMA库函数指针
    │
    ├─ dmafuncs.init()
    │  └─ 初始化DMA引擎
    │
    ├─ 分配DMA缓冲区
    │  ├─ buf_rx[0] = 物理地址
    │  ├─ buf_rx[1] = 物理地址
    │  └─ buf_tx = 物理地址
    │
    ├─ 创建DMA通道
    │  ├─ tx_chid = ChannelCreate()
    │  ├─ tx_coid = ConnectAttach()
    │  └─ 设置事件回调
    │
    └─ 初始化互斥锁 (dma_lock)
    │
    ▼──────────────────────────────────────────
step 9: 附加中断处理
    │
    └─ ser_attach_intr(dev)
       │
       ├─ 创建中断事件
       │  └─ sigevent = 指向中断处理函数
       │
       ├─ 附加中断
       │  └─ dev->iid = InterruptAttachEvent(irq, &event, flags)
       │
       ├─ 启用中断
       │  └─ InterruptUnmask(dev->intr, dev->iid)
       │
       └─ variant_intr_init(dev)
          └─ 平台特定初始化
    │
    ▼──────────────────────────────────────────
step 10: 添加到设备链表
    │
    ├─ dev->next = NULL
    └─ dev->tty.name = "/dev/ser0" (示例)
    │
    ▼──────────────────────────────────────────
设备创建完成
    │
    └─ return (设备指针存储在链表中)
```

---

## 数据发送/接收序列

### 发送数据时序 (Write → UART_TX)

```
应用程序: write("/dev/ser0", "Hello", 5)
    │
    ▼───────────────────────
IOCHAR库层
    │
    ├─ 数据验证和流控制检查
    ├─ 复制数据到 obuf (输出缓冲)
    │  └─ obuf.cnt += 5
    │
    └─ 调用 tto(dev, TTO_DATA, arg)
       │
       ▼───────────────────────
驱动层: tto(TTO_DATA)
       │
       ├─ [流控制检查]
       │  ├─ XOFF状态? 不发送
       │  └─ 否则继续
       │
       ├─ 循环填充UART TX FIFO
       │  │
       │  ├─ 读 UART_FR (帧寄存器)
       │  │  └─ 检查 TXFF (TX FIFO Full)
       │  │
       │  ├─ FIFO未满 (TXFF=0)?
       │  │  ├─ byte = *obuf.head++
       │  │  ├─ write_port(dev, PL011_DR, byte)
       │  │  │  └─ 硬件直接送出
       │  │  └─ obuf.cnt--, obuf.cnt > 0? 继续
       │  │
       │  └─ FIFO满 (TXFF=1)?
       │     └─ 停止填充
       │
       ├─ 启用TX中断
       │  └─ write_port(dev, PL011_IMSC, imr |= TxInt)
       │
       └─ 返回已发送字节数
       │
       ▼───────────────────────
硬件UART
       │
       ├─ FIFO中有数据
       │  └─ 逐字节移出到TX线
       │
       └─ TX FIFO变空
          └─ TxInt中断触发
             │
             ▼───────────────────────
中断处理线程
             │
             └─ tx_interrupt(dev)
                │
                ├─ 循环读UART_FR检查TXFE (TX FIFO Empty)?
                │  └─ 如是: 有空间继续填充
                │
                ├─ 从obuf读下一字节
                │  ├─ byte = *obuf.head++
                │  ├─ write_port(dev, PL011_DR, byte)
                │  └─ obuf.cnt--
                │
                ├─ obuf为空?
                │  └─ 禁用TxInt中断
                │     └─ write_port(dev, PL011_IMSC, imr &= ~TxInt)
                │
                └─ 通知应用: 数据已发送
                   └─ iochar_send_event(&dev->tty)
```

### 接收数据时序 (UART_RX → Read)

```
硬件UART
    │
    ├─ 数据到达RX引脚
    │  └─ 接收的字节进入RX FIFO
    │
    └─ RX触发条件满足
       ├─ FIFO达到触发级 (1/4, 1/2等)
       │  或
       └─ RX超时 (无新数据一段时间)
          │
          └─ RxInt 或 RtInt中断触发
             │
             ▼────────────────────────
中断处理线程
             │
             └─ rx_interrupt(dev)
                │
                ├─ 循环条件: RXFE未设置 (FIFO非空)?
                │
                ├─ 读 UART_DR 获取数据+错误标志
                │  ├─ byte = DR & 0xFF (实际数据)
                │  └─ errors = DR[11:8]
                │     ├─ OE (OverRun Error)
                │     ├─ BE (Break Error)
                │     ├─ PE (Parity Error)
                │     └─ FE (Frame Error)
                │
                ├─ [错误处理]
                │  ├─ 有错误?
                │  │  ├─ 清除错误标志
                │  │  ├─ 计数统计
                │  │  ├─ 必要时日志记录
                │  │  └─ 可能丢弃数据
                │  │
                │  └─ 无错误: 继续
                │
                ├─ [缓冲区检查]
                │  ├─ ibuf还有空间?
                │  │  └─ 是: 添加数据
                │  │
                │  └─ ibuf满?
                │     └─ 启用RTS流控
                │        └─ 告诉对方停止发送
                │
                ├─ 添加到输入缓冲
                │  ├─ *ibuf.head++ = byte
                │  ├─ ibuf.cnt++
                │  └─ 更新highwater流控制
                │
                └─ 返回 true(有新数据)
                   │
                   ▼────────────────────────
后续处理
                   │
                   ├─ 如有新数据到达
                   │  └─ iochar_send_event(&dev->tty)
                   │     └─ 唤醒应用读取请求
                   │
                   └─ 应用程序
                      │
                      ├─ read("/dev/ser0", buf, len)
                      │  └─ 从ibuf复制数据
                      │
                      └─ 返回读取的字节数
```

---

## 中断处理流程

### 中断处理详细状态机

```
硬件中断 (IRQ 36/37)
    │
    ▼─────────────────────────────════════════════
中断服务程序 (ISR)
    │
    ├─ 保存CPU寄存器状态
    ├─ 标志中断已发生
    └─ 返回 IST_UNBLOCK (让线程处理)
       │
       ▼─────────────────────────────════════════════
高优先级中断服务线程
       │
       ├─ 读 UART_MIS (中断状态)
       │  │
       │  ├─ [RxInt or RtInt] ────┐
       │  ├─ [TxInt] ─────────┐   │
       │  ├─ [FeInt] ─────────┤   │
       │  ├─ [PeInt] ─────────┤   │
       │  ├─ [BeInt] ─────────┤   │
       │  └─ [OeInt] ─────────┤   │
       │                       │   │
       │  ┌─────────────────────┘   │
       │  │                         │
       │  ▼                         ▼
       │ ┌──────────────────┐   ┌──────────────────┐
       │ │ TX中断处理       │   │ RX中断处理       │
       │ ├──────────────────┤   ├──────────────────┤
       │ │ if (TxInt)       │   │ if (RxInt/RtInt) │
       │ │  {               │   │  {               │
       │ │  tx_interrupt()  │   │  rx_interrupt()  │
       │ │  }               │   │  }               │
       │ └──────────────────┘   └──────────────────┘
       │           │                    │
       │           └────┬─────────────────┘
       │                │
       │         ┌──────▼──────┐
       │         │ 错误处理    │
       │         ├─────────────┤
       │         │ if (错误位) │
       │         │  {          │
       │         │  清除中断   │
       │         │  计数统计   │
       │         │  日志记录   │
       │         │  }          │
       │         └─────────────┘
       │                │
       │    ┌───────────┴───────────┐
       │    │ 收集处理结果           │
       │    ├───────────────────────┤
       │    │ 是否有新事件?         │
       │    │  - 新数据到达        │
       │    │  - 数据发送完成      │
       │    │  - 错误状态          │
       │    │  - 信号线变化        │
       │    └───────────────────────┘
       │                │
       │                ▼
       │    ┌───────────────────────┐
       │    │ 评估流控制状态         │
       │    ├───────────────────────┤
       │    │ ibuf.cnt >= highwater?│
       │    │  └─ 启用RTS流控       │
       │    │ ibuf.cnt <= lowwater? │
       │    │  └─ 禁用RTS流控       │
       │    └───────────────────────┘
       │                │
       │                ▼
       │    ┌───────────────────────┐
       │    │ 通知应用               │
       │    ├───────────────────────┤
       │    │ iochar_send_event()   │
       │    │  └─ 唤醒读取请求      │
       │    └───────────────────────┘
       │                │
       └────────────────┴────────────────
                        │
                        ▼─────────────────┐
                    返回处理              │
                    │                    │
                    └────────────────────┘
                                    │
                               驱动继续
```

---

## PL011 UART寄存器

### 寄存器映射表

```
偏移   寄存器名称          类型   描述
────────────────────────────────────────────────
0x00   UARTDR              R/W   数据寄存器
0x04   UARTRSR/UARTECR     R/W   接收状态/错误清除
0x18   UARTFR              RO    标志寄存器
0x20   UARTILPR            R/W   低功耗计数器
0x24   UARTIBRD            R/W   整数波特率除数
0x28   UARTFBRD            R/W   小数波特率除数
0x2C   UARTLCR_H           R/W   线控制
0x30   UARTCR              R/W   控制寄存器
0x34   UARTIFLS            R/W   中断FIFO级别
0x38   UARTIMSC            R/W   中断掩码
0x3C   UARTRIS             RO    原始中断状态
0x40   UARTMIS             RO    掩码中断状态
0x44   UARTICR             WO    中断清除
0x48   UARTDMACR           R/W   DMA控制
```

### UARTDR (0x00) - 数据寄存器

```
位    字段      含义
───────────────────────────────
[7:0] DATA      接收/发送数据字节
[8]   FE        帧错误(RO)
[9]   PE        奇偶错误(RO)
[10]  BE        中止错误(RO)
[11]  OE        溢出错误(RO)
```

### UARTFR (0x18) - 标志寄存器(只读)

```
位    字段      含义
───────────────────────────────
[0]   CTS       清除发送(0=流控制off, 1=流控制on)
[3]   RXFE      RX FIFO空 (0=有数据, 1=空)
[4]   RXFF      RX FIFO满 (1=满)
[5]   TXFE      TX FIFO空 (1=空,可发送)
[6]   TXFF      TX FIFO满 (1=满,不可发送)
[7]   BUSY      UART忙 (1=传输中)
[8]   DCD       数据载波检测
[9]   DSR       数据就绪
[10]  RI        振铃指示
```

### UARTLCR_H (0x2C) - 线控制寄存器

```
位    字段      值     含义
──────────────────────────────────────
[1:0] WLEN      00     5位数据
                01     6位数据
                10     7位数据
                11     8位数据
[2]   FEN       0/1    FIFO禁用/启用
[3]   STP2      0/1    1/2个停止位
[4]   EPS       0/1    偶/奇奇偶校验
[5]   PEN       0/1    奇偶禁用/启用
[6]   BRK       0/1    无中止/发送中止
[7]   SPS       0/1    固定/粘性奇偶
```

### UARTCR (0x30) - 控制寄存器

```
位    字段      含义
────────────────────────────────
[0]   UARTEN    UART启用 (1=启用)
[5]   RTS       请求发送 (0=高, 1=低)
[8]   RXE       接收启用 (1=启用)
[9]   TXE       发送启用 (1=启用)
[14]  RTSEN     RTS硬件流控启用
[15]  CTSEN     CTS硬件流控启用
```

### UARTIMSC (0x38) - 中断掩码寄存器

```
位    字段      含义
────────────────────────────────
[0]   RIMIM     RI中断掩码
[1]   CTSMIM    CTS中断掩码
[2]   DCDMIM    DCD中断掩码
[3]   DSRMIM    DSR中断掩码
[4]   RXIM      RX FIFO中断掩码
[5]   TXIM      TX FIFO中断掩码
[6]   RTIM      RX超时中断掩码
[7]   FEIM      帧错误中断掩码
[8]   PEIM      奇偶错误中断掩码
[9]   BEIM      中止错误中断掩码
[10]  OEIM      溢出错误中断掩码
```

---

## 硬件接口时序

### 波特率配置时序

```
原始时钟: 24 MHz
目标波特率: 38400 bps

计算过程:
    divisor = 24000000 / (16 × 38400)
            = 24000000 / 614400
            = 39.0625

    IBRD = ⌊39.0625⌋ = 39
    FBRD = round((39.0625 - 39) × 64)
         = round(0.0625 × 64)
         = 4

验证:
    实际波特率 = 24000000 / (16 × (39 + 4/64))
                = 24000000 / (16 × 39.0625)
                = 24000000 / 625
                = 38400 bps ✓

写入寄存器:
    UARTIBRD = 0x27 (39十进制)
    UARTFBRD = 0x04 (4十进制)
```

### TX FIFO填充时序

```
时间线:
T0:     应用write("Hello", 5)
        │
        ├─ 数据→obuf
T1:     └─ tto(TTO_DATA) 调用
        │
        ├─ 检查TXFF (TX FIFO Full)
T2:     ├─ TXFF=0 (未满)
        │
        ├─ byte='H' → UARTDR
T3:     ├─ byte='e' → UARTDR
        ├─ byte='l' → UARTDR
        ├─ byte='l' → UARTDR
T4:     ├─ byte='o' → UARTDR
        │
        ├─ obuf.cnt=0
T5:     ├─ 启用TxInt中断
        │
T6:     硬件开始发送
        ├─ 'H' → TX引脚
T7:     ├─ 'e' → TX引脚
        ...
Tn:     └─ TX FIFO变空
        │
Tn+1:   TxInt中断触发
        ├─ obuf仍为空
        └─ 禁用TxInt中断

总发送时间 ≈ 5字节 × (10位/字节) / 38400 ≈ 1.3ms
```

### 流控制(RTS/CTS)时序

```
发送方                      接收方
(BCM2712)                  (远程设备)

    RTS=低  ────────────┐
                        ├─ CTS检测
    等待CTS    ◄────────┤
                        │
    CTS=低 触发  ◄──────┤
    │                   │
    ├─ TxInt启用
    └─ FIFO填充
       ├─ byte0→DR
       ├─ byte1→DR
       ...
       └─ FIFO满

    监听CTS
       │
       ├─ CTS变高?
       │  └─ 停止发送,等待
       │
       └─ CTS变低?
          └─ 继续发送

接收缓冲区状态:
    ibuf.cnt < highwater
       └─ RTS=低
           └─ 信号发送方继续

    ibuf.cnt >= highwater
       └─ RTS=高
           └─ 信号发送方停止

恢复:
    ibuf.cnt <= lowwater
       └─ RTS=低
           └─ 发送方继续
```

### RX FIFO处理时序

```
硬件接收 (FIFO = 32字节)

数据到达rate: 38400 bps
单字节时间: 10bit / 38400 ≈ 260 μs

    T0: byte0进入FIFO
        FIFO.cnt = 1      (触发级 = 8)
        │
    T1: byte1进入FIFO
        FIFO.cnt = 2
        │
    ...
    T7: byte7进入FIFO
        FIFO.cnt = 8      (触发级达成!)
        │
    T8: RxInt中断触发
        │
    T9: rx_interrupt()执行
        ├─ 读FIFO.cnt = 8
        ├─ 循环读8字节
        │  ├─ 每次: byte = UARTDR
        │  └─ FIFO.cnt--
        ├─ FIFO.cnt = 0
        │
        ┌─ 继续接收(无中断)
    T10:├─ byte8→FIFO
        ├─ ...
        ├─ byte15→FIFO
        │
    T18: FIFO.cnt = 8
        └─ RxInt触发(再次)

接收超时(RtInt):
    若FIFO中有数据但未达触发级
    且未见新数据(32位时间内)
    → RtInt中断触发
    → rx_interrupt()读取所有数据
```

---

## DMA传输时序(可选)

### TX DMA时序

```
应用write()
    │
    ├─ obuf填充
T0: └─ tto(TTO_DATA)
       │
       ├─ 检查DMA_TX_ACTIVE
T1:    ├─ 否: 继续
       │
       ├─ 填充DMA缓冲
       │  ├─ byte0→buf_tx[0]
       │  ├─ byte1→buf_tx[1]
T2:    │  └─ ...byte31→buf_tx[31]
       │
       ├─ 缓冲区满或无更多数据
T3:    ├─ 配置DMA传输
       │  ├─ src = buf_tx物理地址
       │  ├─ dst = UARTDR
       │  └─ length = 32字节
       │
T4:    ├─ dmafuncs.xfer_start()
       └─ DMA_TX_ACTIVE = true
          │
T5:    DMA硬件接管
          ├─ 自动填充UART FIFO
          ├─ 字节逐个移出
          │
T36:   └─ 32字节全部发送完
          │
T37:   DMA完成中断
          ├─ 清DMA_TX_ACTIVE
          ├─ 填充下一批(如有)
          └─ 所有数据发送完毕
             └─ 通知应用

优点: CPU未参与数据传输
```

### RX DMA时序 (双缓冲)

```
硬件接收:
    byte0→RX FIFO
T0: │
T1: byte1→RX FIFO
    │
    ...
T7: byte8→RX FIFO  (FIFO = 8/16)
    │
T8: DMA触发
    ├─ src = UARTDR (硬件FIFO)
    ├─ dst = buf_rx[0]物理地址
    └─ length = 16字节 (大小可配)
    │
T9: DMA自动填充buf_rx[0]
    ├─ ...接收字节...
    │
T24: buf_rx[0]满32字节
     │
T25: 切换到buf_rx[1]
     ├─ DMA.buffer0 ^= 1
     └─ 继续接收到buf_rx[1]
     │
应用同时:
     ├─ 从buf_rx缓冲读取
     ├─ 处理数据
     └─ 无中断等待

cpu少量参与: 仅在缓冲区切换时
```

此DEVC驱动设计完整，完全可应用于Raspberry Pi 5的QNX系统。

