# PL011 UART硬件规格与寄存器参考

## 目录
1. [硬件概览](#硬件概览)
2. [寄存器详细定义](#寄存器详细定义)
3. [控制逻辑](#控制逻辑)
4. [中断源](#中断源)
5. [波特率计算](#波特率计算)
6. [时序参数](#时序参数)
7. [错误处理](#错误处理)
8. [快速参考](#快速参考)

---

## 硬件概览

### Raspberry Pi 5上的PL011 UART

| 参数 | 值 | 说明 |
|------|-----|------|
| 控制器类型 | ARM PL011 | Primecell UART |
| 数量 | 2个 (主要) | uart0, uart1 |
| uart0地址 | 0x10009000 | 物理地址 |
| uart1地址 | 0x1000A000 | 物理地址 |
| uart0中断 | 36 | IRQ号 |
| uart1中断 | 37 | IRQ号 |
| 寄存器空间 | 0x1000 (4KB) | 完整映射 |
| 输入时钟 | 24 MHz | 固定频率 |
| 分频器 | 16 | 波特率分频 |
| TX FIFO深度 | 32字节 | 可编程触发级 |
| RX FIFO深度 | 32字节 | 可编程触发级 |
| 数据位 | 5-8 | 可配置 |
| 停止位 | 1或2 | 可配置 |
| 奇偶校验 | 无/奇/偶 | 可配置 |
| 流控制 | RTS/CTS | 硬件支持 |
| 波特率范围 | 300-3000000 | 软件配置 |

---

## 寄存器详细定义

### 寄存器快速索引

```
地址   名称              类型    说明
──────────────────────────────────────────────
0x00   UARTDR            R/W     数据寄存器
0x04   UARTRSR/UARTECR   R/W     接收状态/错误清除
0x18   UARTFR            RO      标志寄存器
0x20   UARTILPR          R/W     低功耗计数器
0x24   UARTIBRD          R/W     整数波特率除数
0x28   UARTFBRD          R/W     小数波特率除数
0x2C   UARTLCR_H         R/W     线控制寄存器
0x30   UARTCR            R/W     控制寄存器
0x34   UARTIFLS          R/W     中断FIFO级别
0x38   UARTIMSC          R/W     中断掩码集
0x3C   UARTRIS           RO      原始中断状态
0x40   UARTMIS           RO      掩码中断状态
0x44   UARTICR           WO      中断清除
0x48   UARTDMACR         R/W     DMA控制
```

---

### UARTDR (地址: 0x00) - 数据寄存器

**写操作**: 发送数据到TX FIFO
**读操作**: 从RX FIFO读取数据

```
位    字段名     值范围   描述
───────────────────────────────────────────────
[7:0] DATA       0-255    接收/发送的数据字节
[8]   FE         0/1      帧错误 (只读)
[9]   PE         0/1      奇偶校验错误 (只读)
[10]  BE         0/1      中止错误 (只读)
[11]  OE         0/1      FIFO溢出错误 (只读)
```

**使用示例**:
```c
// 发送字节 'A'
write_port(dev, PL011_DR, 'A');

// 读接收字节
unsigned int data = read_port(dev, PL011_DR);
unsigned char byte = data & 0xFF;
unsigned int errors = (data >> 8) & 0x0F;
if (errors & 0x8) {  // 过溢
    // 处理错误
}
```

---

### UARTRSR/UARTECR (地址: 0x04) - 接收状态/错误清除

**读操作**: 获取接收状态
**写操作**: 清除错误标志

```
读取时:
位    字段          含义
─────────────────────────────────────
[0]   FE_ERR        帧错误
[1]   PE_ERR        奇偶校验错误
[2]   BE_ERR        中止错误
[3]   OE_ERR        FIFO溢出

写入时:
向该寄存器写任何值都会清除所有错误标志
```

**使用示例**:
```c
// 检查接收错误
unsigned int status = read_port(dev, PL011_RSR);
if (status & 0x0F) {  // 有任何错误
    // 记录错误
    write_port(dev, PL011_ECR, 0);  // 清除错误
}
```

---

### UARTFR (地址: 0x18) - 标志寄存器 (只读)

实时反映UART状态和FIFO状态

```
位    字段      值    含义
──────────────────────────────────────────────
[0]   CTS       0/1   清除发送 (外部线状态)
[1]   DSR       0/1   数据就绪
[2]   DCD       0/1   数据载波检测
[3]   BUSY      0/1   UART忙 (发送中)
[4]   RXFE      0/1   RX FIFO空
[5]   TXFF      0/1   TX FIFO满
[6]   RXFF      0/1   RX FIFO满
[7]   TXFE      0/1   TX FIFO空
[8]   RI        0/1   振铃指示
```

**含义解释**:
- **CTS[0]**: 0=流控制OFF,可发送; 1=流控制ON,停止发送
- **RXFE[4]**: 1=FIFO空,无数据可读; 0=FIFO有数据
- **TXFF[5]**: 1=TX FIFO满,不能写入; 0=有空间
- **TXFE[7]**: 1=TX FIFO空,数据已发完; 0=FIFO有数据
- **BUSY[3]**: 1=正在传输; 0=空闲

**常见用法**:
```c
// 检查是否可以发送
unsigned int fr = read_port(dev, PL011_FR);
if (!(fr & TXFF) && (fr & CTS)) {  // FIFO有空间且允许发送
    write_port(dev, PL011_DR, byte);
}

// 检查是否有数据可读
if (!(fr & RXFE)) {  // RX FIFO非空
    unsigned int data = read_port(dev, PL011_DR);
}

// 等待发送完成
while (read_port(dev, PL011_FR) & BUSY) {
    // 等待...
}
```

---

### UARTIBRD (地址: 0x24) - 整数波特率除数

存储波特率分频的整数部分

```
位     字段         说明
──────────────────────────────────────
[15:0] IBRD         整数波特率除数(0-65535)
```

**计算公式**:
```
IBRD = ⌊ IC_CLK / (16 × BaudRate) ⌋

例：计算38400波特率
IBRD = ⌊ 24000000 / (16 × 38400) ⌋
     = ⌊ 39.0625 ⌋
     = 39 (0x27)
```

**使用**:
```c
unsigned int ibrd = clk / (16 * baud);
write_port(dev, PL011_IBRD, ibrd);
```

---

### UARTFBRD (地址: 0x28) - 小数波特率除数

存储波特率分频的小数部分

```
位     字段         说明
──────────────────────────────────────
[5:0]  FBRD         小数波特率除数(0-63)
```

**计算公式**:
```
小数部分 = IC_CLK / (16 × BaudRate) - IBRD
FBRD = round(小数部分 × 64)

例：
小数部分 = 24000000 / (16 × 38400) - 39
        = 39.0625 - 39
        = 0.0625
FBRD = round(0.0625 × 64)
     = round(4)
     = 4 (0x04)
```

**使用**:
```c
unsigned int divisor = clk / (16 * baud);  // 39.0625
unsigned int ibrd = divisor;  // 39
unsigned int fbrd = round((divisor - ibrd) * 64);  // 4
write_port(dev, PL011_FBRD, fbrd);
```

---

### UARTLCR_H (地址: 0x2C) - 线控制寄存器

配置数据格式、奇偶校验、FIFO等

```
位    字段        值       含义
────────────────────────────────────────────────────
[1:0] WLEN        00        5位数据
                  01        6位数据
                  10        7位数据
                  11        8位数据(默认)

[2]   FEN         0/1       FIFO禁用/启用

[3]   STP2        0/1       1个/2个停止位

[4]   EPS         0/1       奇偶检查:偶/奇

[5]   PEN         0/1       奇偶校验禁用/启用

[6]   BRK         0/1       无中止条件/发送中止

[7]   SPS         0/1       固定奇偶校验/粘性
```

**配置组合示例**:
```
标准配置(8N1 + FIFO):
    WLEN = 11 (8位数据)
    FEN = 1 (启用FIFO)
    STP2 = 0 (1个停止位)
    PEN = 0 (无奇偶校验)
    值 = 0x70 = 0b01110000

配置7E1(7位偶校验):
    WLEN = 10 (7位数据)
    FEN = 1 (启用FIFO)
    STP2 = 0 (1个停止位)
    PEN = 1 (启用奇偶)
    EPS = 1 (偶校验)
    值 = 0x6C = 0b01101100
```

**使用**:
```c
void setup_line_control(DEV_PL011 *dev, int bits, int stop, int parity)
{
    unsigned int lcr_h = 0;
    
    // 数据位
    if (bits == 8) lcr_h |= (3 << 0);      // WLEN = 11
    else if (bits == 7) lcr_h |= (2 << 0);
    
    // FIFO启用
    lcr_h |= (1 << 2);  // FEN = 1
    
    // 停止位
    if (stop == 2) lcr_h |= (1 << 3);  // STP2 = 1
    
    // 奇偶校验
    if (parity != 'n') {
        lcr_h |= (1 << 5);             // PEN = 1
        if (parity == 'o') lcr_h |= (1 << 4);  // EPS = 1 (奇)
    }
    
    write_port(dev, PL011_LCR_H, lcr_h);
}
```

---

### UARTCR (地址: 0x30) - 控制寄存器

控制UART的启用和硬件信号

```
位     字段      值      含义
────────────────────────────────────────────────
[0]    UARTEN    0/1     UART禁用/启用

[5]    RTS       0/1     RTS请求发送(低/高)

[8]    RXE       0/1     接收禁用/启用

[9]    TXE       0/1     发送禁用/启用

[14]   RTSEN     0/1     RTS硬件流控禁用/启用

[15]   CTSEN     0/1     CTS硬件流控禁用/启用
```

**配置步骤**:
```
初始化时序:
1. 禁用UART
   write_port(dev, PL011_CR, 0)
   
2. 配置波特率、线控制等
   
3. 启用UART (RXE+TXE+UARTEN+流控())
   unsigned int cr = 0;
   cr |= (1 << 0);   // UARTEN = 1
   cr |= (1 << 8);   // RXE = 1
   cr |= (1 << 9);   // TXE = 1
   cr |= (1 << 14);  // RTSEN = 1
   cr |= (1 << 15);  // CTSEN = 1
   write_port(dev, PL011_CR, cr);
```

---

### UARTIFLS (地址: 0x34) - 中断FIFO级别

设置RX/TX FIFO触发阈值级别

```
位     字段       值    含义
──────────────────────────────────────────────────────
[2:0]  TXIFLSEL   000   1/8满 (4字节)
                  001   1/4满 (8字节)
                  010   1/2满 (16字节,默认)
                  011   3/4满 (24字节)
                  100   7/8满 (28字节)

[5:3]  RXIFLSEL   000   1/8满 (4字节)
                  001   1/4满 (8字节)
                  010   1/2满 (16字节,默认)
                  011   3/4满 (24字节)
                  100   7/8满 (28字节)
```

**触发条件**:
- **TX触发**: TX FIFO≤设定级时触发TxInt
- **RX触发**: RX FIFO≥设定级时触发RxInt (处理开始)

**使用示例**:
```c
// 设置RX触发在FIFO≥16字节, TX触发在FIFO≤8字节
unsigned int ifls = 0;
ifls |= (0x01 << 3);  // RXIFLSEL = 001 (1/4满=8字节)
ifls |= (0x01 << 0);  // TXIFLSEL = 001 (1/4满=8字节)
write_port(dev, PL011_IFLS, ifls);
```

---

### UARTIMSC (地址: 0x38) - 中断掩码集

启用/禁用各个中断源

```
位     字段       0/1      含义
──────────────────────────────────────────────
[0]    RIMIM      禁/启    RI中断掩码
[1]    CTSMIM     禁/启    CTS中断掩码
[2]    DCDMIM     禁/启    DCD中断掩码
[3]    DSRMIM     禁/启    DSR中断掩码
[4]    RXIM       禁/启    RX FIFO中断掩码
[5]    TXIM       禁/启    TX FIFO中断掩码
[6]    RTIM       禁/启    RX超时中断掩码
[7]    FEIM       禁/启    帧错误中断掩码
[8]    PEIM       禁/启    奇偶错误中断掩码
[9]    BEIM       禁/启    中止错误中断掩码
[10]   OEIM       禁/启    溢出错误中断掩码
```

**典型启用组合**:
```
主控(Master)数据传输:
    RX := 1 (接收中断启用)
    TX := 1 (发送中断启用)
    RT := 1 (接收超时启用)
    FE := 1 (帧错误启用)
    PE := 1 (奇偶错误启用)
    BE := 1 (中止错误启用)
    OE := 1 (溢出错误启用)
    值 = 0x7F0 = 0b011111110000

使用代码:
    #define UART_STANDARD_MASK 0x7F0
    write_port(dev, PL011_IMSC, UART_STANDARD_MASK);
```

---

### UARTRIS (地址: 0x3C) - 原始中断状态 (只读)

显示所有中断源的当前状态(不受掩码影响)

```
位     字段       001      含义
──────────────────────────────────────────────
[0]    RIRMIS     0/1      RI原始中断
[1]    CTSRMIS    0/1      CTS原始中断
...
```

**等同于IMSC的位定义**, 但显示掩码前的状态

使用: 调试时读取原始状态

---

### UARTMIS (地址: 0x40) - 掩码中断状态 (只读)

显示当前活跃的中断(已通过掩码过滤)

```
MIS = RIS & IMSC  (逻辑与)

含义: 只显示同时被RIS激活且被IMSC启用的中断
```

**标准使用**:
```c
unsigned int mis = read_port(dev, PL011_MIS);

if (mis & (1 << 4)) {  // RxInt
    rx_interrupt(dev);
}
if (mis & (1 << 5)) {  // TxInt
    tx_interrupt(dev);
}
if (mis & (1 << 6)) {  // RtInt (接收超时)
    rx_timeout(dev);
}
```

---

### UARTICR (地址: 0x44) - 中断清除 (只写)

写1到相应位可清除该中断

```
位     字段       写     含义
──────────────────────────────────────────────
[0]    RIICRX     1      清除RI中断
[1]    CTSICR     1      清除CTS中断
[2]    DCDMICR    1      清除DCD中断
[3]    DSRMICR    1      清除DSR中断
[4]    RXICR      1      清除RX中断
[5]    TXICR      1      清除TX中断
[6]    RTICR      1      清除RX超时中断
[7]    FEICR      1      清除帧错误中断
[8]    PEICR      1      清除奇偶错误中断
[9]    BEICR      1      清除中止错误中断
[10]   OEICR      1      清除溢出错误中断
```

**使用**:
```c
// 清除RX中断 (位[4])
write_port(dev, PL011_ICR, 1 << 4);

// 清除所有错误中断
write_port(dev, PL011_ICR, 0x7C0);  // 位[10:6]

// 注意: RXIM/TXIM需通过IFLS启用/禁用而不是ICR
```

---

### UARTDMACR (地址: 0x48) - DMA控制

启用/禁用DMA模式

```
位     字段       值      含义
──────────────────────────────────────────────
[0]    RXDMAE     0/1     RX DMA禁用/启用
[1]    TXDMAE     0/1     TX DMA禁用/启用
[2]    DMAONERR   0/1     DMA继续/错误时中止
```

**使用**:
```c
// 启用TX DMA
write_port(dev, PL011_DMACR, 1 << 1);  // TXDMAE = 1

// 启用RX/TX DMA
unsigned int dmacr = (1 << 0) | (1 << 1);
write_port(dev, PL011_DMACR, dmacr);
```

---

## 控制逻辑

### UART启用/禁用序列

```
禁用UART (停止操作):
    step1: write_port(dev, PL011_CR, 0)
           └─ 清除所有控制位,包括UARTEN
    
    step2: 等待发送完成
           while (read_port(dev, PL011_FR) & BUSY) {
               // 等待...
           }

启用UART (开始操作):
    step1: 禁用状态 (CR = 0)
    
    step2: 配置波特率
           write_port(dev, PL011_IBRD, ibrd);
           write_port(dev, PL011_FBRD, fbrd);
    
    step3: 配置线控制
           write_port(dev, PL011_LCR_H, lcr_h);
           // 必须在UARTEN前配置LCR_H
    
    step4: 配置FIFO触发级
           write_port(dev, PL011_IFLS, ifls);
    
    step5: 启用UART + 收发 + 流控制
           write_port(dev, PL011_CR, 
               (1 << 0)  |  // UARTEN
               (1 << 8)  |  // RXE
               (1 << 9)  |  // TXE
               (1 << 14) |  // RTSEN
               (1 << 15));  // CTSEN
    
    step6: 启用中断 (可选)
           write_port(dev, PL011_IMSC, mask);
```

---

### 流控制逻辑

```
硬件流控制 (RTS/CTS):

发送方视角:
    读 FR[0] (CTS)
    ├─ CTS=0: 对方允许发送,写FIFO
    └─ CTS=1: 对方停止发送,等待

接收方视角:
    ibuf.cnt >= highwater
    ├─ 设置RTS=1: 信号"停止发送"
    └─ ibuf.cnt降至lowwater时
       └─ 设置RTS=0: 恢复"可发送"

软件流控制 (XON/XOFF):
    接收 XOFF (0x13)
    ├─ 停止发送
    └─ 直到接收 XON (0x11)
```

---

## 中断源

### 10个中断源总表

| 位 | MSB名 | RIS名 | 掩码 | 触发条件 |
|----|-------|--------|------|---------|
| 0 | RIICRX | RIRMIS | RIMIM | RI线变化 |
| 1 | CTSICR | CTSRMIS | CTSMIM | CTS线下降沿 |
| 2 | DCDMICR | DCDRMIS | DCDMIM | DCD线变化 |
| 3 | DSRMICR | DSRRMIS | DSRMIM | DSR线变化 |
| 4 | RXICR | RXRIS | RXIM | RX FIFO≥阈值 |
| 5 | TXICR | TXRIS | TXIM | TX FIFO≤阈值 |
| 6 | RTICR | RTRIS | RTIM | RX超时(32位未读) |
| 7 | FEICR | FERIS | FEIM | 帧错误 |
| 8 | PEICR | PERIS | PEIM | 奇偶错误 |
| 9 | BEICR | BERIS | BEIM | 中止检测 |
| 10 | OEICR | OERIS | OEIM | FIFO溢出 |

---

## 波特率计算

###公式

```
输入时钟: IC_CLK = 24 MHz
分频器: 16 (固定)
目标波特率: BaudRate

计算步骤:
    1. divisor = IC_CLK / (16 × BaudRate)
    2. IBRD = ⌊ divisor ⌋
    3. 小数部分 = divisor - IBRD
    4. FBRD = round(小数部分 × 64)
    5. 验证: 实际波特率 = IC_CLK / (16 × (IBRD + FBRD/64))
```

### 常见波特率表

| 波特率 | divisor | IBRD | FBRD | 精度 |
|--------|---------|------|------|------|
| 9600 | 156.25 | 156 | 16 | 99.95% |
| 19200 | 78.125 | 78 | 8 | 99.95% |
| 38400 | 39.0625 | 39 | 4 | 99.99% |
| 57600 | 26.0417 | 26 | 3 | 99.93% |
| 115200 | 13.0208 | 13 | 2 | 99.93% |
| 230400 | 6.5104 | 6 | 33 | 99.99% |
| 460800 | 3.2552 | 3 | 17 | 99.99% |
| 921600 | 1.6276 | 1 | 40 | 99.96% |
| 1500000 | 1.0 | 1 | 0 | 100% |

### 计算示例 (38400 bps)

```
divisor = 24000000 / (16 × 38400)
        = 24000000 / 614400
        = 39.0625

IBRD = ⌊39.0625⌋ = 39

小数部分 = 39.0625 - 39 = 0.0625

FBRD = round(0.0625 × 64)
     = round(4)
     = 4

验证:
    波特率 = 24000000 / (16 × (39 + 4/64))
           = 24000000 / (16 × 39.0625)
           = 24000000 / 625
           = 38400 bps ✓ (完全精确!)
```

---

## 时序参数

### I2C和UART的混合比较

| 特性 | I2C | UART |
|------|-----|------|
| 位时间(38400) | N/A | 26 μs |
| 字节时间(8+1+1) | N/A | 260 μs |
| START条件 | <4.7 μs建立 | 无 |
| STOP条件 | <4.7 μs时间 | 1-2位周期 |
| 流控制 | 无 | RTS/CTS |
| 最大距离 | 几米 | 几米(RS232) |

### 串口超时计算

```
RX超时触发条件:
    - FIFO中有数据, 但不足触发阈值
    - 且持续时间超过 32位周期

例: 38400 bps, 10位/字节
    位周期 = 1 / 38400 = 26 μs
    32位超时 = 32 × 26 = 832 μs
    
    即: FIFO中有数据超过830微秒未读 → RtInt触发
```

---

## 错误处理

### 错误标志详解

| 错误 | 标志 | 原因 | 恢复 |
|------|------|------|------|
| 帧错误(FE) | DR[8] | 停止位检测为0 | 同步波特率 |
| 奇偶(PE) | DR[9] | 校验位计算错误 | 请求重传 |
| 中止(BE) | DR[10] | 接收到0长脉冲 | 等待恢复 |
| 溢出(OE) | DR[11] | FIFO满,新数据丢失 | 加快接收处理 |

### 错误恢复程序

```c
unsigned int data = read_port(dev, PL011_DR);
unsigned int errors = (data >> 8) & 0x0F;

if (errors) {
    // 1. 记录错误
    if (errors & 0x01) error_count.frame++;
    if (errors & 0x02) error_count.parity++;
    if (errors & 0x04) error_count.break++;
    if (errors & 0x08) error_count.overrun++;
    
    // 2. 清除错误状态
    write_port(dev, PL011_ECR, 0);
    
    // 3. 可选: 向上层报错
    // send_error_event(error_type);
    
    // 4. 继续接收下一字节
    return;  // 丢弃此字节
}

// 正常处理
process_byte(data & 0xFF);
```

---

## 快速参考

### 常用操作速查表

| 操作 | 代码 | 说明 |
|------|------|------|
| 读数据 | `val = read_port(dev, PL011_DR)` | 从RX FIFO |
| 写数据 | `write_port(dev, PL011_DR, val)` | 到TX FIFO |
| 读状态 | `fr = read_port(dev, PL011_FR)` | 检查FIFO状态 |
| 读中断 | `mis = read_port(dev, PL011_MIS)` | 获取活跃中断 |
| 清中断 | `write_port(dev, PL011_ICR, mask)` | 写1清除 |
| 启用UART | `write_port(dev, PL011_CR, 0x301)` | UARTEN+RXE+TXE |
| 禁用UART | `write_port(dev, PL011_CR, 0)` | 清除所有 |

### 关键寄存器列表

```
必须配置的寄存器:
    □ IBRD  - 整数波特率
    □ FBRD  - 小数波特率
    □ LCR_H - 数据格式(8N1)
    □ IFLS  - 中断触发级
    □ IMSC  - 中断掩码
    □ CR    - 启用/流控制

监控的寄存器:
    □ FR    - FIFO状态(写数据时)
    □ MIS   - 中断状态(中断处理时)
    □ DR    - 数据(读写时检查错误位)

调试用的寄存器:
    □ RIS   - 原始中断状态
    □ RSR   - 接收状态
```

---

完整的PL011 UART硬件规格参考,适用于Raspberry Pi 5的QNX系统开发。

