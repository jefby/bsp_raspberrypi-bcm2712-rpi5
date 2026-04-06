# I2C驱动硬件规格和寄存器参考

## 目录
1. [硬件概览](#硬件概览)
2. [寄存器映射](#寄存器映射)
3. [关键寄存器详解](#关键寄存器详解)
4. [中断源定义](#中断源定义)
5. [I2C协议命令](#i2c协议命令)
6. [时序参数](#时序参数)
7. [错误源代码](#错误源代码)
8. [FIFO管理](#fifo管理)

---

## 硬件概览

### Raspberry Pi 5上的DWC I2C控制器

| 参数 | 值 | 说明 |
|------|-----|------|
| 芯片 | BCM2712 | Broadcom SoC |
| 控制器 | DWC I2C | Synopsys DesignWare I2C |
| 物理地址 | 设备树中定义 | 通常通过Device Tree |
| 内存大小 | 0x1000 (4KB) | 寄存器空间 |
| IRQ | 0xa8 (168) | 中断号(树莓派5) |
| 输入时钟 | 200MHz | 系统时钟 |
| 主控模式 | 支持 | master only |
| 地址宽度 | 7位/10位 | 可配置 |
| 速率支持 | 100KHz, 400KHz | STD, FAST modes |
| FIFO深度 | 256字节 | TX/RX独立 |
| DMA支持 | 否 | PIO+中断驱动 |

### 内存地址空间

```
0x00    DW_IC_CON              - 控制寄存器
0x04    DW_IC_TAR              - 目标地址寄存器
0x08    DW_IC_SAR              - 自身地址寄存器
0x0c    DW_IC_HS_MADDR         - 高速主控地址
0x10    DW_IC_DATA_CMD         - 数据/命令FIFO
0x14    DW_IC_SS_SCL_HCNT      - 标准模式SCL高计数
0x18    DW_IC_SS_SCL_LCNT      - 标准模式SCL低计数
0x1c    DW_IC_FS_SCL_HCNT      - 快速模式SCL高计数
0x20    DW_IC_FS_SCL_LCNT      - 快速模式SCL低计数
0x24    DW_IC_HS_SCL_HCNT      - 高速模式SCL高计数(未用)
0x28    DW_IC_HS_SCL_LCNT      - 高速模式SCL低计数(未用)
0x2c    DW_IC_INTR_STAT        - 中断状态(只读)
0x30    DW_IC_INTR_MASK        - 中断掩码(读写)
0x34    DW_IC_RAW_INTR_STAT    - 原始中断状态
0x38    DW_IC_RX_TL            - RX FIFO阈值
0x3c    DW_IC_TX_TL            - TX FIFO阈值
0x40    DW_IC_CLR_INTR         - 清中断(读清)
0x44    DW_IC_CLR_RX_UNDER     - 清RX下溢(读清)
0x48    DW_IC_CLR_RX_OVER      - 清RX上溢(读清)
0x4c    DW_IC_CLR_TX_OVER      - 清TX上溢(读清)
0x50    DW_IC_CLR_RD_REQ       - 清读请求(读清)
0x54    DW_IC_CLR_TX_ABRT      - 清TX中止(读清)
0x58    DW_IC_CLR_RX_DONE      - 清RX完成(读清)
0x5c    DW_IC_CLR_ACTIVITY     - 清活动(读清)
0x60    DW_IC_CLR_STOP_DET     - 清STOP检测(读清)
0x64    DW_IC_CLR_START_DET    - 清START检测(读清)
0x68    DW_IC_CLR_GEN_CALL     - 清通用调用(读清)
0x6c    DW_IC_ENABLE           - 适配器启用
0x70    DW_IC_STATUS           - 状态寄存器(只读)
0x74    DW_IC_TXFLR            - TX FIFO等级
0x78    DW_IC_RXFLR            - RX FIFO等级
0x7c    DW_IC_SDA_HOLD         - SDA保持时间
0x80    DW_IC_TX_ABRT_SOURCE   - TX中止源代码
0x84    DW_IC_SLV_DATA_NACK_ONLY - 从模式NAK配置
0x88    DW_IC_DMA_CR           - DMA控制(未用)
0x8c    DW_IC_DMA_TDLR         - DMA TX深度水位
0x90    DW_IC_DMA_RDLR         - DMA RX深度水位
0x94    DW_IC_SDA_SETUP        - SDA建立时间
0x98    DW_IC_ACK_GENERAL_CALL - 通用调用ACK
0x9c    DW_IC_ENABLE_STATUS    - 启用状态(只读)
0xa0    DW_IC_FS_SPKLEN        - 快速模式尖脉冲长度
0xa4    DW_IC_HS_SPKLEN        - 高速模式尖脉冲长度
0xf4    DW_IC_COMP_PARAM_1     - 组件参数1
0xf8    DW_IC_COMP_VERSION     - 版本号
0xfc    DW_IC_COMP_TYPE        - 组件类型
0x204   DW_IC_SOFT_RESET       - 软复位
0x800   DW_IC_CLOCK_PARAMS     - 时钟参数
```

---

## 关键寄存器详解

### DW_IC_CON (0x00) - 控制寄存器

**位配置**：

```
[0]     MASTER              - 1=主模式(必须), 0=从模式
[2:1]   SPEED               - 速率选择
        00=保留
        01=标准(100KHz)
        10=快速(400KHz)
        11=高速(3.4MHz, 不支持)
[3]     IC_10BITADDR_SLAVE  - 1=10位从地址
[4]     IC_10BITADDR_MASTER - 1=10位主地址
[5]     IC_RESTART_EN       - 1=支持RESTART条件
[6]     IC_SLAVE_DISABLE    - 1=禁用从功能(必须在主模式)
[7]     STOP_DET_IFADDRESSED- 从模式STOP检测
[8:10]  TX_EMPTY_CTRL       - TX FIFO空控制
[11]    RX_FIFO_FULL_HLD_CTRL - RX FIFO满保持
[12]    SMBUS_ARP_EN        - SMBus ARP启用(未用)
[13]    SMBUS_PERSISTENT_SLV_ADDR_EN - SMBus持久地址
```

**使用示例**：
```c
// 主模式, 快速模式, 7位地址, RESTART支持, 禁用从
master_cfg = DW_IC_CON_MASTER           // [0] = 1
           | DW_IC_CON_SPEED_FAST       // [2:1] = 10
           | DW_IC_CON_RESTART_EN       // [5] = 1
           | DW_IC_CON_SLAVE_DISABLE;   // [6] = 1
```

---

### DW_IC_TAR (0x04) - 目标地址寄存器

**位配置**：

```
[9:0]   IC_TAR              - I2C目标地址 (7位或10位)
[10]    GC_OR_START         - 1=通用调用, 0=正常地址
[11]    SPECIAL             - 1=特殊地址(START BYTE), 0=正常
[12]    IC_TAR_10BITADDR    - 1=使用10位地址编码
[15:13] (保留)
```

**使用示例**：
```c
// 设置7位目标地址 0x50
i2c_reg_write32(dev, DW_IC_TAR, 0x50);

// 设置10位目标地址 0x250
addr_10bit = 0x250 | DW_IC_TAR_10BITADDR_MASTER;
i2c_reg_write32(dev, DW_IC_TAR, addr_10bit);
```

---

### DW_IC_DATA_CMD (0x10) - 数据/命令寄存器

**写入(Master发送命令)**：

```
[7:0]   DAT                 - 数据字节
[8]     CMD                 - 命令类型
        0 = WRITE(发送)
        1 = READ(接收)
[9]     STOP                - 1=在此字节后生成STOP条件
[10]    RESTART             - 1=在此字节后生成RESTART条件
        (用于发送->接收转换)
```

**读取(从FIFO读数据)**：

```
[7:0]   DAT                 - 接收到的数据字节
[8:10]  (保留)
```

**使用示例**：
```c
// 发送单字节数据(无特殊条件)
uint32_t cmd = (byte & 0xFF) | DW_IC_DATA_CMD_WRITE;
i2c_reg_write32(dev, DW_IC_DATA_CMD, cmd);

// 发送单字节数据并生成STOP
uint32_t cmd = (byte & 0xFF) | DW_IC_DATA_CMD_WRITE | DW_IC_DATA_CMD_STOP;
i2c_reg_write32(dev, DW_IC_DATA_CMD, cmd);

// 接收单字节(纯读)
uint32_t cmd = DW_IC_DATA_CMD_READ;
i2c_reg_write32(dev, DW_IC_DATA_CMD, cmd);

// 接收单字节并生成STOP
uint32_t cmd = DW_IC_DATA_CMD_READ | DW_IC_DATA_CMD_STOP;
i2c_reg_write32(dev, DW_IC_DATA_CMD, cmd);

// 发送->接收转换(RESTART)
uint32_t cmd = DW_IC_DATA_CMD_READ | DW_IC_DATA_CMD_RESTART;
i2c_reg_write32(dev, DW_IC_DATA_CMD, cmd);
```

---

### DW_IC_SS_SCL_HCNT/LCNT (0x14/0x18) - 标准模式时序

```
标准模式(100KHz):
DW_IC_SS_SCL_HCNT [15:0] - SCL高电平持续计数
DW_IC_SS_SCL_LCNT [15:0] - SCL低电平持续计数

示例值 (200MHz输入时钟):
    tHIGH = 4.0us, tLOW = 4.7us, tF = 0.3us
    HCNT = (200 * (4 + 0.3) + 500) / 1000 - 3 = 797
    LCNT = (200 * (4.7 + 0.3) + 500) / 1000 - 1 = 999

写入:
    i2c_reg_write32(dev, DW_IC_SS_SCL_HCNT, 797);
    i2c_reg_write32(dev, DW_IC_SS_SCL_LCNT, 999);
```

---

### DW_IC_FS_SCL_HCNT/LCNT (0x1c/0x20) - 快速模式时序

```
快速模式(400KHz):
DW_IC_FS_SCL_HCNT [15:0] - SCL高电平计数
DW_IC_FS_SCL_LCNT [15:0] - SCL低电平计数

示例值 (200MHz输入时钟):
    tHIGH = 0.6us, tLOW = 1.3us, tF = 0.3us
    HCNT = (200 * (0.6 + 0.3) + 500) / 1000 - 3 = 177
    LCNT = (200 * (1.3 + 0.3) + 500) / 1000 - 1 = 319

写入:
    i2c_reg_write32(dev, DW_IC_FS_SCL_HCNT, 177);
    i2c_reg_write32(dev, DW_IC_FS_SCL_LCNT, 319);
```

---

### DW_IC_INTR_MASK (0x30) - 中断掩码

**位定义**：

```
[0]     M_RX_UNDER          - RX FIFO下溢掩码
[1]     M_RX_OVER           - RX FIFO上溢掩码
[2]     M_RX_FULL           - RX FIFO满掩码
[3]     M_TX_OVER           - TX FIFO上溢掩码
[4]     M_TX_EMPTY          - TX FIFO空掩码
[5]     M_RD_REQ            - 读请求掩码(从模式)
[6]     M_TX_ABRT           - TX中止掩码
[7]     M_RX_DONE           - RX完成掩码(从模式)
[8]     M_ACTIVITY          - 活动掩码
[9]     M_STOP_DET          - STOP检测掩码
[10]    M_START_DET         - START检测掩码
[11]    M_GEN_CALL          - 通用调用掩码(从模式)
```

**默认掩码**（主模式）：
```c
#define DW_IC_INTR_DEFAULT_MASK \
    (DW_IC_INTR_RX_FULL   |     // [2] - 接收FIFO满
     DW_IC_INTR_TX_EMPTY  |     // [4] - 发送FIFO空
     DW_IC_INTR_TX_ABRT   |     // [6] - 传输中止(错误)
     DW_IC_INTR_STOP_DET)       // [9] - STOP检测(完成)

使用:
    i2c_reg_write32(dev, DW_IC_INTR_MASK, DW_IC_INTR_DEFAULT_MASK);
```

---

### DW_IC_INTR_STAT (0x2c) - 中断状态(只读)

**位定义**：
```
[0]     R_RX_UNDER          - RX FIFO下溢已发生
[1]     R_RX_OVER           - RX FIFO上溢已发生
[2]     R_RX_FULL           - RX FIFO达到阈值
[3]     R_TX_OVER           - TX FIFO上溢已发生
[4]     R_TX_EMPTY          - TX FIFO低于阈值
[5]     R_RD_REQ            - 从模式读请求
[6]     R_TX_ABRT           - TX中止(错误)
[7]     R_RX_DONE           - RX完成(从模式)
[8]     R_ACTIVITY          - I2C活动
[9]     R_STOP_DET          - STOP条件检测
[10]    R_START_DET         - START条件检测
[11]    R_GEN_CALL          - 通用调用地址收到
```

**读取中断**：
```c
uint32_t status = i2c_reg_read32(dev, DW_IC_INTR_STAT);

if (status & DW_IC_INTR_TX_EMPTY) {
    // FIFO有空间,可以写数据
}

if (status & DW_IC_INTR_RX_FULL) {
    // FIFO有数据,可以读取
}

if (status & DW_IC_INTR_STOP_DET) {
    // 事务已完成
}

if (status & DW_IC_INTR_TX_ABRT) {
    // 发生错误
}
```

---

### DW_IC_ENABLE (0x6c) - 适配器启用

**位定义**：
```
[0]     ENABLE              - 1=启用, 0=禁用
[1:31]  (保留)
```

**使用**：
```c
// 启用适配器
i2c_reg_write32(dev, DW_IC_ENABLE, 1);

// 禁用适配器(必须在更改配置前)
i2c_reg_write32(dev, DW_IC_ENABLE, 0);
```

---

### DW_IC_ENABLE_STATUS (0x9c) - 启用状态(只读)

**位定义**：
```
[0]     IC_EN               - 1=使能过程进行中, 0=适配器禁用
```

**使用(轮询)**：
```c
// 等待适配器禁用
int32_t timeout = 1000;
while ((i2c_reg_read32(dev, DW_IC_ENABLE_STATUS) & 1) != 0) {
    if (--timeout <= 0) {
        // 超时
        return -ETIMEDOUT;
    }
    usleep(25);  // 25us延迟
}
```

---

### DW_IC_STATUS (0x70) - 状态寄存器(只读)

**位定义**：
```
[0]     ACTIVITY            - 1=总线忙, 0=总线空闲
[1]     TFNF                - TX FIFO未满
[2]     TFE                 - TX FIFO为空
[3]     RFNE                - RX FIFO非空
[4]     RFF                 - RX FIFO满
[5]     MST_ACTIVITY        - 主控忙
[6]     SLV_ACTIVITY        - 从控忙(未用)
```

**使用**：
```c
// 检查总线是否忙
uint32_t status = i2c_reg_read32(dev, DW_IC_STATUS);
if (status & DW_IC_STATUS_ACTIVITY) {
    // 总线正忙,有I2C活动
}

// 检查TX FIFO
if (status & DW_IC_STATUS_TFNF) {
    // TX FIFO未满,可以写入
}

// 检查RX FIFO
if (status & DW_IC_STATUS_RFNE) {
    // RX FIFO非空,可以读取
}
```

---

### DW_IC_TXFLR/RXFLR (0x74/0x78) - FIFO等级(只读)

**位定义**：
```
[8:0]   TXFLR/RXFLR - FIFO中的条目数(0-256)
```

**使用**：
```c
// 获取TX FIFO中的字节数
uint32_t tx_count = i2c_reg_read32(dev, DW_IC_TXFLR);

// 获取RX FIFO中的字节数
uint32_t rx_count = i2c_reg_read32(dev, DW_IC_RXFLR);

if (rx_count > 0) {
    // 有数据可读
}
```

---

### DW_IC_RX_TL/TX_TL (0x38/0x3c) - FIFO阈值

**位定义**：
```
[7:0]   RX_TL / TX_TL - 阈值水位(0-255)
```

**含义**：
- **RX_TL**：当RX FIFO >= RX_TL时触发RX_FULL中断
- **TX_TL**：当TX FIFO <= TX_TL时触发TX_EMPTY中断

**使用**：
```c
// 获取FIFO深度(256字节)
uint32_t fifo_depth = dev->fifo_depth;

// 设置阈值为深度的一半
i2c_reg_write32(dev, DW_IC_RX_TL, fifo_depth / 2);
i2c_reg_write32(dev, DW_IC_TX_TL, fifo_depth / 2);
```

---

### DW_IC_TX_ABRT_SOURCE (0x80) - 中止源代码(只读)

**错误源位定义**：

```
[0]   7B_ADDR_NOACK        - Master发送7位地址无应答
[1]   10ADDR1_NOACK        - Master发送10位地址第1字节无应答
[2]   10ADDR2_NOACK        - Master发送10位地址第2字节无应答
[3]   TXDATA_NOACK         - Master发送数据无应答
[4]   GCALL_NOACK          - Master发送通用调用无应答
[5]   GCALL_READ           - Master从通用调用地址读(非法)
[7]   SBYTE_ACKDET         - START字节检测到应答(非法)
[9]   SBYTE_NORSTRT        - START字节无RESTART(非法)
[10]  10B_RD_NORSTRT       - 10位地址读无RESTART(非法)
[11]  MASTER_DIS           - 用户禁用Master(错误)
[12]  ARBT_LOST            - I2C仲裁丢失
[16]  USER_ARBT            - 用户禁用导致仲裁失败
```

**使用**：
```c
uint32_t abrt = i2c_reg_read32(dev, DW_IC_TX_ABRT_SOURCE);

if (abrt & DW_IC_TX_ARBT_LOST) {
    // 仲裁丢失(多主设备冲突)
    status |= I2C_STATUS_ARBL;
}
else if (abrt & DW_IC_TX_ABRT_ADDR_NOACK) {
    // 地址无应答(从设备不存在或断电)
    status |= I2C_STATUS_ADDR_NACK;
}
else if (abrt & DW_IC_TX_ABRT_TXDATA_NOACK) {
    // 数据无应答(从设备内部错误)
    status |= I2C_STATUS_DATA_NACK;
}
else {
    // 其他错误
    status |= I2C_STATUS_ABORT;
}
```

---

### 清中断寄存器 (0x40-0x68)

这些寄存器通过**读操作的副作用**清除中断：

```
DW_IC_CLR_INTR      (0x40)  - 清所有中断
DW_IC_CLR_RX_UNDER  (0x44)  - 清RX下溢
DW_IC_CLR_RX_OVER   (0x48)  - 清RX上溢
DW_IC_CLR_TX_OVER   (0x4c)  - 清TX上溢
DW_IC_CLR_RD_REQ    (0x50)  - 清读请求(从模式)
DW_IC_CLR_TX_ABRT   (0x54)  - 清TX中止
DW_IC_CLR_RX_DONE   (0x58)  - 清RX完成(从模式)
DW_IC_CLR_ACTIVITY  (0x5c)  - 清活动标志
DW_IC_CLR_STOP_DET  (0x60)  - 清STOP检测
DW_IC_CLR_START_DET (0x64)  - 清START检测
DW_IC_CLR_GEN_CALL  (0x68)  - 清通用调用
```

**使用**：
```c
// 清除所有中断(初始化时)
(void)i2c_reg_read32(dev, DW_IC_CLR_INTR);

// 清除TX中止中断(错误处理时)
(void)i2c_reg_read32(dev, DW_IC_CLR_TX_ABRT);

// 清除STOP检测中断
(void)i2c_reg_read32(dev, DW_IC_CLR_STOP_DET);
```

---

### DW_IC_COMP_PARAM_1 (0xf4) - 组件参数

**位定义**：
```
[7:0]   RX_BUFFER_DEPTH     - RX FIFO深度字段
[15:8]  TX_BUFFER_DEPTH     - TX FIFO深度字段
[24:16] ADD_ENCODED_PARAMS  - 额外参数
```

**使用(提取FIFO深度)**：
```c
uint32_t param = i2c_reg_read32(dev, DW_IC_COMP_PARAM_1);

uint32_t tx_depth = ((param >> 16) & 0xFF) + 1;
uint32_t rx_depth = ((param >> 8) & 0xFF) + 1;

dev->fifo_depth = min(tx_depth, rx_depth);  // 通常两者相等(256)
```

---

### DW_IC_COMP_TYPE (0xfc) - 组件类型(只读)

**值**：0x44570140 (DWC I2C)

**验证**：
```c
uint32_t comp_type = i2c_reg_read32(dev, DW_IC_COMP_TYPE);
if (comp_type != DW_IC_COMP_TYPE_VALUE) {
    // 错误: 不是DWC I2C控制器
    return -ENODEV;
}
```

---

## 中断源定义

### 默认启用的中断

```c
#define DW_IC_INTR_RX_UNDER      0x001    // 位[0]
#define DW_IC_INTR_RX_OVER       0x002    // 位[1]
#define DW_IC_INTR_RX_FULL       0x004    // 位[2]
#define DW_IC_INTR_TX_OVER       0x008    // 位[3]
#define DW_IC_INTR_TX_EMPTY      0x010    // 位[4]
#define DW_IC_INTR_RD_REQ        0x020    // 位[5] - 从模式
#define DW_IC_INTR_TX_ABRT       0x040    // 位[6]
#define DW_IC_INTR_RX_DONE       0x080    // 位[7] - 从模式
#define DW_IC_INTR_ACTIVITY      0x100    // 位[8]
#define DW_IC_INTR_STOP_DET      0x200    // 位[9]
#define DW_IC_INTR_START_DET     0x400    // 位[10]
#define DW_IC_INTR_GEN_CALL      0x800    // 位[11] - 从模式

// 主模式默认掩码
#define DW_IC_INTR_DEFAULT_MASK \
    (DW_IC_INTR_RX_FULL     |    // 接收FIFO满
     DW_IC_INTR_TX_EMPTY    |    // 发送FIFO空
     DW_IC_INTR_TX_ABRT     |    // 传输中止(错误)
     DW_IC_INTR_STOP_DET)        // STOP条件(完成)
```

### 中断处理优先级(使用中的驱动顺序)

1. **TX_ABRT** (最高) - 错误中断,立即停止传输
2. **STOP_DET** - 事务完成信号
3. **RX_FULL** - 接收数据可用
4. **TX_EMPTY** (最低) - 发送FIFO有空间

---

## I2C协议命令

### 写数据命令

```c
// 单字节写(无特殊条件)
uint32_t cmd = (byte & 0xFF) | DW_IC_DATA_CMD_WRITE;

// 最后字节写 + STOP条件
uint32_t cmd = (byte & 0xFF) | DW_IC_DATA_CMD_WRITE | DW_IC_DATA_CMD_STOP;

// 中间字节写(示例256字节事务中的第128字节)
uint32_t cmd = (byte & 0xFF) | DW_IC_DATA_CMD_WRITE;
```

**I2C波形示例**：
```
     START
      │
      ▼
   [ADDR]  ← 7位地址 (或10位)
      │
      ▼
    [ACK]  ← 从设备应答
      │
     ┌─────────────────────────┐
     │  数据传输状态           │
     │                         │
  ┌─ WRITE(byte[0]) ──────┐  │
  │  ├─ WRITE(byte[1])    │  │
  │  ├─ WRITE(byte[2])    │  │
  │  └─ ... 循环       │  │
  │      WRITE(byte[n]) + STOP
  └─────────────────────────┘
      │
      ▼
     STOP
```

---

### 读数据命令

```c
// 单字节读(无特殊条件)
uint32_t cmd = DW_IC_DATA_CMD_READ;

// 最后字节读 + STOP条件
uint32_t cmd = DW_IC_DATA_CMD_READ | DW_IC_DATA_CMD_STOP;

// 从数据读 + STOP条件
uint32_t cmd = DW_IC_DATA_CMD_READ | DW_IC_DATA_CMD_STOP;
```

---

### RESTART命令(发送->接收转换)

```c
// 接收第1字节 + RESTART(发送完后)
uint32_t cmd = DW_IC_DATA_CMD_READ | DW_IC_DATA_CMD_RESTART;

// 中间接收字节
uint32_t cmd = DW_IC_DATA_CMD_READ;

// 最后接收字节 + STOP条件
uint32_t cmd = DW_IC_DATA_CMD_READ | DW_IC_DATA_CMD_STOP;
```

**I2C波形示例**：
```
    START
      │
      ├─ [ADDR+W]  ← 写地址
      ├─ [ACK]
      │
      ├─ [DATA0]   ← 发送数据
      ├─ [ACK]
      ├─ [DATA1]
      ├─ [ACK]
      │
      ├─ RESTART   ← 不释放总线,重新START
      │
      ├─ [ADDR+R]  ← 读地址
      ├─ [ACK]
      │
      ├─ [DATA0]   ← 接收数据
      ├─ [NAK]     ← 最后字节不应答
      │
     STOP
```

---

## 时序参数

### I2C规范时序值

| 参数 | 标准模式(100KHz) | 快速模式(400KHz) | 单位 | 说明 |
|------|-----------------|-----------------|------|------|
| fSCL | 100 | 400 | KHz | SCL频率 |
| tHIGH | 4.0 | 0.6 | us | SCL高电平时间 |
| tLOW | 4.7 | 1.3 | us | SCL低电平时间 |
| tF | 0.3 | 0.3 | us | 下降时间 |
| tR | 1.0 | 0.3 | us | 上升时间 |
| tSU;STA | 4.7 | 0.6 | us | START建立时间 |
| tHD;STA | 4.0 | 0.6 | us | START保持时间 |
| tSU;DAT | 250 | 100 | ns | 数据建立时间 |
| tHD;DAT | 0 | 0 | ns | 数据保持时间 |
| tSU;STO | 4.0 | 0.6 | us | STOP建立时间 |

### HCNT/LCNT计算公式

**公式**：
```
IC_CLK = 200MHz / 1000 = 200000 KHz (树莫派5)

for 标准模式(100KHz):
    tHIGH_ns = 4000 ns
    tLOW_ns = 4700 ns
    tF_ns = 300 ns
    
    // DW推荐的"安全"模式
    HCNT_safe = (200 * (4 + 0.3) + 500) / 1000 - 3
              = (200 * 4.3 + 500) / 1000 - 3
              = 860 / 1000 - 3
              = 0.86 - 3
              = 797 (计算误)
              
    正确:
    HCNT_safe = floor((IC_CLK * (tHIGH_ns + tF_ns) + 500000) / 1000000) - 3
              = floor((200000 * (4000 + 300) + 500000) / 1000000) - 3
              = floor((200000 * 4300 + 500000) / 1000000) - 3
              = floor((860000000 + 500000) / 1000000) - 3
              = floor(860.5) - 3
              = 860 - 3
              = 857

    LCNT = floor((200000 * (4700 + 300) + 500000) / 1000000) - 1
         = floor((200000 * 5000 + 500000) / 1000000) - 1
         = floor((1000000000 + 500000) / 1000000) - 1
         = floor(1000.5) - 1
         = 1000 - 1
         = 999
```

---

## 错误源代码

### TX_ABRT_SOURCE位定义

```c
// 寄存器定义
#define DW_IC_TX_ABRT_7B_ADDR_NOACK         0x00000001u  // 位[0]
#define DW_IC_TX_ABRT_10ADDR1_NOACK         0x00000002u  // 位[1]
#define DW_IC_TX_ABRT_10ADDR2_NOACK         0x00000004u  // 位[2]
#define DW_IC_TX_ABRT_TXDATA_NOACK          0x00000008u  // 位[3]
#define DW_IC_TX_ABRT_GCALL_NOACK           0x00000010u  // 位[4]
#define DW_IC_TX_ABRT_GCALL_READ            0x00000020u  // 位[5]
#define DW_IC_TX_ABRT_SBYTE_ACKDET          0x00000080u  // 位[7]
#define DW_IC_TX_ABRT_SBYTE_NORSTRT         0x00000200u  // 位[9]
#define DW_IC_TX_ABRT_10B_RD_NORSTRT        0x00000400u  // 位[10]
#define DW_IC_TX_ABRT_MASTER_DIS            0x00000800u  // 位[11]
#define DW_IC_TX_ARBT_LOST                  0x00001000u  // 位[12]
#define DW_IC_TX_ARBT_USER_ARBT             0x00010000u  // 位[16]

// 常用的ADDR_NOACK范围
#define DW_IC_TX_ABRT_ADDR_NOACK \
    (DW_IC_TX_ABRT_7B_ADDR_NOACK | \
     DW_IC_TX_ABRT_10ADDR1_NOACK | \
     DW_IC_TX_ABRT_10ADDR2_NOACK)
```

---

## FIFO管理

### FIFO配置

```c
// 获取FIFO深度(从DW_IC_COMP_PARAM_1)
uint32_t param = i2c_reg_read32(dev, DW_IC_COMP_PARAM_1);
uint32_t tx_depth = ((param >> 16) & 0xFF) + 1;
uint32_t rx_depth = ((param >> 8) & 0xFF) + 1;

dev->fifo_depth = min(tx_depth, rx_depth);  // 通常256

// 设置FIFO阈值
uint32_t threshold = dev->fifo_depth / 2;  // 50%
i2c_reg_write32(dev, DW_IC_TX_TL, threshold);
i2c_reg_write32(dev, DW_IC_RX_TL, threshold);
```

### FIFO水位检查

```c
// 检查TX FIFO是否有空间
void check_tx_fifo(dwc_dev_t *dev)
{
    uint32_t txflr = i2c_reg_read32(dev, DW_IC_TXFLR);
    
    if (txflr >= dev->fifo_depth) {
        // FIFO满,不能再写
        return;
    }
    
    // FIFO有空间,可以写入
    uint32_t space = dev->fifo_depth - txflr;
    for (int i = 0; i < space && have_data; i++) {
        uint32_t cmd = (*data++);
        i2c_reg_write32(dev, DW_IC_DATA_CMD, cmd);
    }
}

// 检查RX FIFO是否有数据
void check_rx_fifo(dwc_dev_t *dev)
{
    uint32_t rxflr = i2c_reg_read32(dev, DW_IC_RXFLR);
    
    if (rxflr == 0) {
        // 没有数据可读
        return;
    }
    
    // FIFO有数据,可以读取
    for (int i = 0; i < rxflr; i++) {
        uint32_t data = i2c_reg_read32(dev, DW_IC_DATA_CMD);
        *buf++ = (uint8_t)(data & 0xFF);
    }
}
```

---

## 寄存器初始化检查清单

在使用驱动前,所有这些寄存器应被正确初始化：

```
硬件验证:
□ 读DW_IC_COMP_TYPE = 0x44570140
□ 读DW_IC_COMP_PARAM_1,提取FIFO深度

时钟配置:
□ 设置DW_IC_SS_SCL_HCNT (标准模式高计数)
□ 设置DW_IC_SS_SCL_LCNT (标准模式低计数)
□ 设置DW_IC_FS_SCL_HCNT (快速模式高计数)
□ 设置DW_IC_FS_SCL_LCNT (快速模式低计数)

中断配置:
□ 写DW_IC_INTR_MASK = 0 (暂时禁用)
□ 读DW_IC_CLR_INTR (清除任何待决中断)

FIFO配置:
□ 设置DW_IC_TX_TL = fifo_depth/2
□ 设置DW_IC_RX_TL = fifo_depth/2

主控配置:
□ 写DW_IC_CON = (MASTER | SPEED | RESTART_EN | SLAVE_DISABLE)
□ 写DW_IC_INTR_MASK = DW_IC_INTR_DEFAULT_MASK (启用)

运行前检查:
□ 禁用适配器 (DW_IC_ENABLE = 0)
□ 等待DW_IC_ENABLE_STATUS = 0 (确认禁用)
```

---

## 快速参考表

### 常用操作

| 操作 | 代码 |
|------|------|
| 启用适配器 | `i2c_reg_write32(dev, DW_IC_ENABLE, 1)` |
| 禁用适配器 | `i2c_reg_write32(dev, DW_IC_ENABLE, 0)` |
| 清所有中断 | `(void)i2c_reg_read32(dev, DW_IC_CLR_INTR)` |
| 设置目标地址 | `i2c_reg_write32(dev, DW_IC_TAR, addr)` |
| 写数据字节 | `i2c_reg_write32(dev, DW_IC_DATA_CMD, byte)` |
| 写读命令 | `i2c_reg_write32(dev, DW_IC_DATA_CMD, DW_IC_DATA_CMD_READ)` |
| 读数据字节 | `byte = i2c_reg_read32(dev, DW_IC_DATA_CMD) & 0xFF` |
| 检查总线忙 | `(i2c_reg_read32(dev, DW_IC_STATUS) & DW_IC_STATUS_ACTIVITY)` |
| 获取中断状态 | `status = i2c_reg_read32(dev, DW_IC_INTR_STAT)` |
| 获取RX FIFO计数 | `count = i2c_reg_read32(dev, DW_IC_RXFLR)` |
| 获取TX FIFO计数 | `count = i2c_reg_read32(dev, DW_IC_TXFLR)` |

