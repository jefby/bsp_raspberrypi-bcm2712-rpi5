# DEVB SDMMC硬件规格与接口参考

## 目录
1. [SD/MMC协议概述](#sdmmc协议概述)
2. [SDIO接口架构](#sdio接口架构)
3. [设备类型与特性](#设备类型与特性)
4. [命令集参考](#命令集参考)
5. [寄存器接口](#寄存器接口)
6. [时序参数](#时序参数)
7. [电源管理](#电源管理)
8. [错误恢复](#错误恢复)
9. [性能规格](#性能规格)
10. [调试接口](#调试接口)

---

## SD/MMC协议概述

### 协议层次结构

```
应用层 (文件系统)
    │
    ├── SCSI命令层 (CAM)
    │   ├── READ/WRITE
    │   ├── INQUIRY
    │   └── MODE SENSE
    │
    └── SD/MMC协议层
        ├── 命令 (CMD0-CMD63)
        ├── 响应 (R1-R7)
        └── 数据传输
            ├── 单块传输
            ├── 多块传输
            └── 流传输
```

### 总线拓扑

```
主机控制器 (SDIO HC)
    │
    ├── SD总线
    │   ├── 1位数据线 (DAT0)
    │   ├── 4位数据线 (DAT0-3)
    │   └── 8位数据线 (MMC, DAT0-7)
    │
    ├── 命令线 (CMD)
    │   └── 双向命令/响应
    │
    └── 时钟线 (CLK)
        └── 主机到设备
```

### 设备识别

| 设备类型 | CID格式 | 容量范围 | 特性 |
|----------|---------|----------|------|
| SD卡 | 16字节 | 32MB-2TB | 版权保护, 速度等级 |
| MMC | 16字节 | 16MB-2TB | 增强存储, 高速模式 |
| eMMC | 16字节 | 4GB-1TB | 嵌入式, 多分区 |

---

## SDIO接口架构

### SDIO连接参数

```c
typedef struct sdio_connect_parm {
    uint32_t        vsdio;          // SDIO版本
    int             argc;           // 命令行参数
    char            **argv;         // 参数数组
    sdio_funcs_t    funcs;          // 回调函数
    sdio_device_ident_t ident;      // 设备标识
} sdio_connect_parm_t;
```

### 回调函数接口

```c
typedef struct sdio_funcs {
    uint32_t nfuncs;                    // 函数数量
    void (*insertion)(                  // 设备插入
        sdio_device_instance_t *instance
    );
    void (*removal)(                    // 设备移除
        sdio_device_instance_t *instance
    );
    void (*event)(                      // 通用事件
        sdio_device_instance_t *instance,
        sdio_event_t event
    );
} sdio_funcs_t;
```

### 设备实例结构

```c
typedef struct sdio_device_instance {
    uint32_t    vid;        // 厂商ID
    uint32_t    did;        // 设备ID
    uint32_t    dtype;      // 设备类型
    uint32_t    ccd;        // 设备类
    uint32_t    path;       // 总线路径
    uint32_t    func;       // 函数号
    void        *hdl;       // 私有句柄
} sdio_device_instance_t;
```

---

## 设备类型与特性

### SD卡特性矩阵

| 特性 | SD | SDHC | SDXC | SDUC |
|------|----|------|------|------|
| 容量 | ≤2GB | ≤32GB | ≤2TB | ≤128TB |
| 寻址 | 字节 | 块(512B) | 块(512B) | 块(512B) |
| 电压 | 3.3V | 3.3V | 3.3V | 3.3V/1.8V |
| 速度 | ≤25MB/s | ≤25MB/s | ≤104MB/s | ≤985MB/s |
| UHS | 无 | I | I/II/III | I/II/III |

### MMC特性

| 特性 | MMC | eMMC | MMC+ |
|------|-----|------|------|
| 容量 | ≤2GB | ≤1TB | ≤2GB |
| 电压 | 3.3V/1.8V | 3.3V/1.8V | 3.3V/1.8V |
| 速度 | ≤20MB/s | ≤400MB/s | ≤52MB/s |
| 特性 | 基本 | 多分区/BKOPS | 高速模式 |

### 设备信息结构

```c
typedef struct sdio_dev_info {
    uint32_t    caps;               // 设备能力
    uint32_t    max_dtr;            // 最大数据传输率
    uint32_t    ocr;                // 操作条件寄存器
    uint32_t    rca;                // 相对卡地址
    uint64_t    capacity;           // 容量(字节)
    uint32_t    block_size;         // 块大小
    uint32_t    block_count;        // 块数量
    uint32_t    rel_wr_sec_c;       // 可靠写入扇区数
    uint32_t    erase_size;         // 擦除大小
    char        cid[16];            // 卡ID
    char        csd[16];            // 卡特定数据
    uint32_t    flags;              // 状态标志
} sdio_dev_info_t;
```

**能力标志 (caps)**:
```c
#define DEV_CAP_HS          (1 << 0)    // 高速模式
#define DEV_CAP_HS200       (1 << 1)    // HS200模式
#define DEV_CAP_DDR         (1 << 2)    // 双数据率
#define DEV_CAP_8BIT        (1 << 3)    // 8位总线
#define DEV_CAP_MMC         (1 << 4)    // MMC设备
#define DEV_CAP_SD          (1 << 5)    // SD设备
#define DEV_CAP_WR_REL      (1 << 6)    // 可靠写入
#define DEV_CAP_BKOPS       (1 << 7)    // 后台操作
#define DEV_CAP_CACHE       (1 << 8)    // 缓存支持
#define DEV_CAP_TRIM        (1 << 9)    // TRIM支持
#define DEV_CAP_DISCARD     (1 << 10)   // 丢弃支持
```

---

## 命令集参考

### 基本命令 (Class 0)

| 命令 | 名称 | 参数 | 响应 | 描述 |
|------|------|------|------|------|
| CMD0 | GO_IDLE_STATE | - | - | 进入空闲状态 |
| CMD1 | SEND_OP_COND | OCR | R3 | 发送操作条件 |
| CMD2 | ALL_SEND_CID | - | R2 | 获取所有卡ID |
| CMD3 | SET_RELATIVE_ADDR | RCA | R1 | 设置相对地址 |
| CMD4 | SET_DSR | DSR | - | 设置驱动级 |
| CMD5 | IO_SEND_OP_COND | OCR | R4 | I/O操作条件 |
| CMD6 | SWITCH | ARG | R1 | 模式切换 |
| CMD7 | SELECT_CARD | RCA | R1 | 选择卡 |
| CMD8 | SEND_IF_COND | VOLT | R7 | 发送接口条件 |
| CMD9 | SEND_CSD | RCA | R2 | 发送CSD |
| CMD10 | SEND_CID | RCA | R2 | 发送CID |
| CMD12 | STOP_TRANSMISSION | - | R1 | 停止传输 |
| CMD13 | SEND_STATUS | RCA | R1 | 发送状态 |
| CMD15 | GO_INACTIVE_STATE | RCA | - | 进入非活跃状态 |

### 块读写命令 (Class 2)

| 命令 | 名称 | 参数 | 响应 | 描述 |
|------|------|------|------|------|
| CMD16 | SET_BLOCKLEN | LEN | R1 | 设置块长度 |
| CMD17 | READ_SINGLE_BLOCK | ADDR | R1 | 读单块 |
| CMD18 | READ_MULTIPLE_BLOCK | ADDR | R1 | 读多块 |
| CMD24 | WRITE_BLOCK | ADDR | R1 | 写单块 |
| CMD25 | WRITE_MULTIPLE_BLOCK | ADDR | R1 | 写多块 |
| CMD27 | PROGRAM_CSD | - | R1 | 编程CSD |

### 擦除命令 (Class 3)

| 命令 | 名称 | 参数 | 响应 | 描述 |
|------|------|------|------|------|
| CMD32 | ERASE_WR_BLK_START | ADDR | R1 | 擦除开始地址 |
| CMD33 | ERASE_WR_BLK_END | ADDR | R1 | 擦除结束地址 |
| CMD38 | ERASE | - | R1 | 执行擦除 |

### 锁卡命令 (Class 4)

| 命令 | 名称 | 参数 | 响应 | 描述 |
|------|------|------|------|------|
| CMD42 | LOCK_UNLOCK | - | R1 | 锁/解锁卡 |

### 应用命令 (Class 5)

| 命令 | ACMD | 名称 | 参数 | 响应 | 描述 |
|------|------|------|------|------|------|
| CMD55 | - | APP_CMD | RCA | R1 | 应用命令前缀 |
| ACMD6 | CMD55+6 | SET_BUS_WIDTH | WIDTH | R1 | 设置总线宽度 |
| ACMD13 | CMD55+13 | SD_STATUS | - | R1 | SD状态 |
| ACMD22 | CMD55+22 | SEND_NUM_WR_BLOCKS | - | R1 | 发送写入块数 |
| ACMD23 | CMD55+23 | SET_WR_BLK_ERASE_COUNT | COUNT | R1 | 设置擦除计数 |
| ACMD41 | CMD55+41 | SD_SEND_OP_COND | OCR | R3 | SD操作条件 |
| ACMD42 | CMD55+42 | SET_CLR_CARD_DETECT | MODE | R1 | 卡检测 |
| ACMD51 | CMD55+51 | SEND_SCR | - | R1 | 发送SCR |

---

## 寄存器接口

### CSD寄存器 (卡特定数据)

**SD卡CSD格式 (128位)**:

```
位域    字段名          描述
──────────────────────────────────────────────
[127:126] CSD_STRUCTURE  CSD结构版本
[125:120] TAAC          数据读取访问时间1
[119:112] NSAC          数据读取访问时间2
[111:104] TRAN_SPEED    最大数据传输率
[103:99]  CCC           命令类支持
[98:96]   READ_BL_LEN   最大读取数据块长度
[95]      READ_BL_PARTIAL 部分块读取允许
[94:80]   C_SIZE         设备容量
[79:62]   VDD_R_CURR_MIN 最小读取电流(VDD)
[61:59]   VDD_R_CURR_MAX 最大读取电流(VDD)
[58:56]   VDD_W_CURR_MIN 最小写入电流(VDD)
[55:53]   VDD_W_CURR_MAX 最大写入电流(VDD)
[52:47]   C_SIZE_MULT    设备容量乘数
[46]      ERASE_BLK_EN   擦除单块启用
[45:39]   SECTOR_SIZE    擦除扇区大小
[38:32]   WP_GRP_SIZE    写保护组大小
[31]      WP_GRP_ENABLE  写保护组启用
[30:29]   R2W_FACTOR     读写速度比
[28:26]   WRITE_BL_LEN   写入块长度
[25]      WRITE_BL_PARTIAL 部分块写入允许
[24:22]   FILE_FORMAT_GRP 文件格式组
[21]      COPY            复制标志
[20]      PERM_WRITE_PROTECT 永久写保护
[19]      TMP_WRITE_PROTECT 临时写保护
[18:16]   FILE_FORMAT     文件格式
[15:8]    CRC             CRC校验
[7:1]     RESERVED        保留
[0]       NOT_USED        未使用
```

### CID寄存器 (卡ID)

**SD卡CID格式**:

```
位域      字段名        描述
────────────────────────────────────
[127:120] MID         制造商ID
[119:104] OID         OEM/应用ID
[103:64]  PNM         产品名称
[63:56]   PRV         产品版本
[55:24]   PSN         产品序列号
[23:20]   MDT         制造日期
[19:16]   CRC         CRC7校验
[15:1]    RESERVED    保留
[0]       NOT_USED    未使用
```

### SCR寄存器 (SD配置寄存器)

```
位域    字段名              描述
────────────────────────────────────────────
[63:60] SCR_STRUCTURE       SCR结构版本
[59:56] SD_SPEC              SD规范版本
[55:52] DATA_STAT_AFTER_ERASE 数据状态(擦除后)
[51:48] SD_SECURITY          SD安全版本
[47:44] SD_BUS_WIDTHS        支持的总线宽度
[43:32] RESERVED             保留
[31:0]  MANUFACTURER         制造商特定
```

### EXT_CSD寄存器 (扩展CSD, MMC/eMMC)

**关键字段**:

```
偏移    字段名              描述
────────────────────────────────────────────
0x00    RESERVED             保留
0x01    RESERVED             保留
0x02    EXT_CSD_REV          EXT_CSD版本
0x03    RESERVED             保留
0x04    PARTITIONING_SUP     分区支持
0x05    ERASE_GROUP_DEF      擦除组定义
0x06    RESERVED             保留
0x07    BOOT_BUS_CONDITIONS  引导总线条件
0x08    BOOT_CONFIG_PROT     引导配置保护
0x09    RESERVED             保留
0x0A    ERASE_MEM_CONT       擦除内存内容
0x0B    RESERVED             保留
0x0C    BUS_WIDTH            总线宽度
0x0D    HS_TIMING            高速时序
0x0E    RESERVED             保留
0x0F    POWER_CLASS          功率等级
0x10    RESERVED             保留
0x11    CMD_SET_REV          命令集版本
0x12    RESERVED             保留
0x13    CMD_SET              命令集
0x14    EXT_CSD_REV          EXT_CSD版本
0x15    RESERVED             保留
0x16    POWER_OFF_NOTIF      断电通知
0x17    HS_TIMING            高速时序
0x18    RESERVED             保留
0x19    BOOT_BUS_WIDTH       引导总线宽度
0x1A    BOOT_BUS_MODE        引导总线模式
0x1B    OUT_OF_INTERRUPT_TIME 中断时间
0x1C    PARTITION_CONFIG      分区配置
0x1D    RESERVED             保留
0x1E    ERASE_GROUP_SIZE     擦除组大小
0x1F    ERASE_GROUP_MULT     擦除组乘数
```

---

## 时序参数

### 时钟频率规格

| 模式 | 频率范围 | 描述 |
|------|----------|------|
| 识别模式 | 0-400KHz | 卡识别和初始化 |
| 默认速度 | 0-25MHz | 标准速度模式 |
| 高速模式 | 0-50MHz | SD高速模式 |
| HS200 | 0-200MHz | UHS-I模式 |
| HS400 | 0-200MHz | UHS-II模式 (DDR) |

### 总线时序参数

**命令线时序**:

```
参数              最小值    典型值    最大值    单位
────────────────────────────────────────────────────
CMD上升时间       -         -         10       ns
CMD下降时间       -         -         10       ns
CMD保持时间       5         -         -        ns
CMD建立时间       5         -         -        ns
```

**数据线时序 (默认速度)**:

```
参数              最小值    典型值    最大值    单位
────────────────────────────────────────────────────
DAT上升时间       -         -         10       ns
DAT下降时间       -         -         10       ns
DAT保持时间       2         -         -        ns
DAT建立时间       2         -         -        ns
时钟-数据延迟     -         -         14       ns
```

### 电源时序

**上电时序**:

```
事件              时间      描述
────────────────────────────────────
VDD稳定          1ms       电源稳定
上电延时         1ms       等待卡准备
CMD0发送         1ms后     进入空闲状态
初始化完成       1-2s      完全就绪
```

---

## 电源管理

### 电源状态

| 状态 | 描述 | 电流 | 退出条件 |
|------|------|------|----------|
| off | 断电 | 0mA | 上电 |
| standby | 待机 | <0.1mA | CMD唤醒 |
| transfer | 传输 | 10-100mA | 传输完成 |
| programming | 编程 | 50-200mA | 编程完成 |

### 电压范围

| 设备类型 | 电压范围 | 推荐电压 |
|----------|----------|----------|
| SD卡 | 2.7-3.6V | 3.3V |
| MMC | 2.7-3.6V | 3.3V |
| eMMC | 2.7-3.6V | 3.3V/1.8V |

### 电源管理命令

```c
// 进入休眠
sdio_suspend(device);

// 从休眠唤醒
sdio_resume(device);

// 电源状态查询
sdio_power_status(device, &status);
```

---

## 错误恢复

### 错误类型

| 错误码 | 名称 | 描述 | 恢复动作 |
|--------|------|------|----------|
| 0x01 | CRC_ERROR | CRC校验错误 | 重试命令 |
| 0x02 | ILLEGAL_COMMAND | 非法命令 | 检查命令格式 |
| 0x04 | COM_CRC_ERROR | 命令CRC错误 | 重试命令 |
| 0x08 | FUNCTION_NUMBER | 函数号错误 | 检查参数 |
| 0x10 | PARAMETER_ERROR | 参数错误 | 修正参数 |
| 0x20 | ADDRESS_ERROR | 地址错误 | 检查地址范围 |
| 0x40 | ERASE_ERROR | 擦除错误 | 重试擦除 |

### 总线重置序列

```
检测到总线错误
    │
    ├── 发送CMD12 (停止传输)
    │
    ├── 等待1ms
    │
    ├── 发送CMD0 (进入空闲)
    │
    ├── 重新初始化序列
    │   ├── CMD1 (MMC) 或 ACMD41 (SD)
    │   ├── CMD2 (获取CID)
    │   ├── CMD3 (设置RCA)
    │   └── CMD7 (选择卡)
    │
    └── 恢复正常操作
```

---

## 性能规格

### 吞吐量规格

| 模式 | 总线宽度 | 时钟 | 理论最大吞吐量 |
|------|----------|------|----------------|
| 默认 | 1位 | 25MHz | 25MB/s |
| 高速 | 4位 | 50MHz | 200MB/s |
| UHS-I | 4位 | 100MHz | 400MB/s |
| UHS-II | 8位 | 200MHz | 1.6GB/s |
| HS200 | 4位 | 200MHz | 800MB/s |
| HS400 | 8位 | 200MHz | 1.6GB/s |

### IOPS性能

| 操作类型 | 典型IOPS | 备注 |
|----------|----------|------|
| 随机读取(4KB) | 1000-5000 | 取决于寻道时间 |
| 顺序读取 | 10000-50000 | 连续块传输 |
| 随机写入 | 500-2000 | 写前擦除开销 |
| 顺序写入 | 5000-20000 | 连续写入 |

### 延迟规格

| 操作 | 最小延迟 | 典型延迟 | 最大延迟 |
|------|----------|----------|----------|
| 命令响应 | 1μs | 10μs | 100μs |
| 单块读取 | 100μs | 1ms | 10ms |
| 多块读取 | 1ms | 10ms | 100ms |
| 单块写入 | 1ms | 10ms | 100ms |
| 多块写入 | 10ms | 100ms | 1s |

---

## 调试接口

### 调试命令

```c
// 启用调试输出
sdmmc_ctrl.verbosity = 1;

// 转储设备信息
sdio_dev_info_dump(device);

// 读取状态寄存器
sdio_status_read(device, &status);

// 执行诊断命令
sdio_diagnostic(device, DIAG_READ_CSD);
sdio_diagnostic(device, DIAG_READ_CID);
sdio_diagnostic(device, DIAG_READ_SCR);
```

### 性能监控

```c
// 启用性能计数器
sdio_perf_enable(device, PERF_READ_IOPS | PERF_WRITE_IOPS);

// 读取性能统计
sdio_perf_read(device, &stats);

// 重置计数器
sdio_perf_reset(device);
```

### 错误日志

```c
// 记录错误事件
cam_slogf(_SLOGC_SIM_MMC, _SLOG_ERROR, 1, 1,
    "%s: command %d failed, status 0x%x",
    __FUNCTION__, cmd, status);

// 启用详细日志
sdmmc_ctrl.verbosity = 2;
```

---

**总结**

DEVB SDMMC硬件接口提供了完整的SD/MMC设备支持，从基本的读写操作到高级的电源管理和错误恢复。驱动通过SDIO抽象层与硬件交互，支持多种设备类型和传输模式。

关键硬件特性：
- **多协议支持**: SD、MMC、eMMC设备
- **高速传输**: 支持UHS-I/II和HS200/400模式
- **电源管理**: 完整的电源状态机
- **错误恢复**: 多级错误检测和恢复
- **性能优化**: DMA支持和队列管理

这些硬件规格确保了驱动在各种存储设备上的稳定性和高效性。