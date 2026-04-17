# DEVB SDMMC驱动架构与设计分析

## 目录
1. [概述](#概述)
2. [架构设计](#架构设计)
3. [核心组件](#核心组件)
4. [数据结构](#数据结构)
5. [初始化流程](#初始化流程)
6. [I/O处理流程](#io处理流程)
7. [分区管理](#分区管理)
8. [高级功能](#高级功能)
9. [错误处理](#错误处理)
10. [性能优化](#性能优化)

---

## 概述

### DEVB SDMMC驱动简介

DEVB (Device Block) 是QNX Neutrino操作系统中的块设备驱动框架，专门处理存储设备如硬盘、SSD、SD卡、MMC卡等。SDMMC子模块实现了SD/MMC卡的完整支持。

**关键特性:**
- **多协议支持**: SD卡、MMC卡、eMMC
- **分区管理**: 支持GPT/MBR分区表
- **高级功能**: TRIM、可靠写入、后台操作
- **电源管理**: 休眠/唤醒支持
- **热插拔**: 动态设备检测

**版本信息:**
- 软件成熟度: SQML Level 1 (实验性)
- 版本号: 100
- 架构: CAM (Common Access Method)

---

## 架构设计

### 分层架构

```
┌─────────────────────────────────────┐
│         应用程序层                   │
│  (文件系统、数据库、用户程序)         │
├─────────────────────────────────────┤
│         CAM层 (Common Access Method) │
│  - 命令队列管理                       │
│  - 设备抽象                          │
│  - 错误处理                          │
├─────────────────────────────────────┤
│         SIM层 (SDMMC模拟器)          │
│  - 协议转换                          │
│  - 命令执行                          │
│  - 状态管理                          │
├─────────────────────────────────────┤
│         SDIO层 (设备接口)            │
│  - 硬件抽象                          │
│  - 总线通信                          │
│  - 中断处理                          │
└─────────────────────────────────────┘
```

### CAM架构集成

DEVB驱动完全集成到QNX的CAM (Common Access Method) 架构中：

```
CAM Manager
    │
    ├── XPT (Transport Layer)
    │   ├── 路径管理
    │   ├── 设备枚举
    │   └── 命令路由
    │
    └── SIM (SDMMC Simulator)
        ├── 命令处理
        ├── 设备控制
        └── 状态管理
```

**CAM命令支持:**
- `XPT_SCSI_IO`: SCSI I/O操作
- `XPT_DEVCTL`: 设备控制命令
- `XPT_PATH_INQ`: 路径查询
- `XPT_RESET_BUS/DEV`: 总线/设备复位

---

## 核心组件

### 1. 控制器结构 (SDMMC_CTRL)

全局控制器管理所有HBA实例：

```c
typedef struct _sdmmc_ctrl {
    TAILQ_HEAD(,_sim_hba) hlist;     // HBA链表
    uint32_t              cflags;     // 控制标志
    int                   verbosity;   // 详细级别
    uint32_t              nhba;       // HBA数量
    path_id_t             pathid_max; // 最大路径ID
    int                   priority;    // 线程优先级
    // ... 其他字段
} SDMMC_CTRL;
```

**控制标志:**
- `SDMMC_CFLAG_SCAN`: 自动检测接口
- `SDMMC_CFLAG_ENUMERATING`: 枚举中
- `SDMMC_CFLG_ARGS_ERR`: 参数错误

### 2. HBA扩展结构 (SIM_SDMMC_EXT)

每个HBA的主扩展数据结构：

```c
typedef struct _sim_sdmmc_ext {
    SIM_HBA         *hba;              // 指向HBA
    uint32_t        eflags;            // 扩展标志
    sdio_device     *device;           // SDIO设备
    sdio_device_instance_t instance;   // 设备实例
    sdio_hc_info_t  hc_inf;            // 主机控制器信息
    sdio_dev_info_t dev_inf;           // 设备信息
    timer_t         pm_timerid;        // 电源管理定时器
    // ... 电源管理字段
    SDMMC_TARGET    targets[8];        // 目标设备数组
} SIM_SDMMC_EXT;
```

**扩展标志位:**
- `SDMMC_EFLAG_PRESENT`: 设备存在
- `SDMMC_EFLAG_TIMER`: 定时器激活
- `SDMMC_EFLAG_BKOPS`: 后台操作
- `SDMMC_EFLAG_CACHE`: 缓存启用
- `SDMMC_EFLAG_RELWR`: 可靠写入

### 3. 分区结构 (SDMMC_PARTITION)

管理存储设备分区：

```c
typedef struct _sdmmc_partition {
    uint32_t    pflags;     // 分区标志
    uint32_t    config;     // 配置
    char        name[20];   // 分区名
    uint32_t    blk_shft;   // 块大小移位
    uint64_t    slba;       // 起始LBA
    uint64_t    elba;       // 结束LBA
    uint64_t    nlba;       // LBA数量
    uint64_t    rc;         // 读取计数
    uint64_t    wc;         // 写入计数
    uint64_t    tc;         // TRIM计数
} SDMMC_PARTITION;
```

---

## 数据结构

### 目标设备结构 (SDMMC_TARGET)

```c
typedef struct _sdmmc_target {
    uint32_t        nluns;          // LUN数量
    uint32_t        blksz;          // 块大小
    SDMMC_PARTITION partitions[8];  // 分区数组
} SDMMC_TARGET;
```

### 命令控制块 (CCB)

CAM命令的核心数据结构：

```c
typedef struct _ccb_header {
    uint8_t     cam_func_code;    // 功能码
    uint8_t     cam_status;       // 状态
    uint8_t     cam_flags;        // 标志
    uint8_t     cam_path_id;      // 路径ID
    uint8_t     cam_target_id;    // 目标ID
    uint8_t     cam_target_lun;   // LUN
    uint16_t    cam_hba;          // HBA索引
} CCB_HEADER;
```

**SCSI I/O CCB:**
```c
typedef struct _ccb_scsiiio {
    CCB_HEADER      cam_ch;           // 头部
    uint8_t         cam_cdb_len;      // CDB长度
    uint8_t         cam_sense_len;    // Sense长度
    uint8_t         cam_cdb_io_flags; // I/O标志
    uint8_t         cam_tag_action;   // 标签动作
    uint32_t        cam_timeout;      // 超时
    uint8_t         cam_cdb_bytes[16]; // CDB字节
    // ... 数据缓冲区字段
} CCB_SCSIIO;
```

---

## 初始化流程

### 1. 模块注册

```c
static const MODULE_ENTRY sim_module = {
    .name   = "sdmmc",
    .args   = &sdmmc_sim_args,      // 参数解析
    .attach = &sdmmc_sim_attach,    // 连接函数
    .detach = &sdmmc_sim_detach     // 断开函数
};
```

### 2. 连接流程

```
sdmmc_sim_attach()
    │
    ├── 参数验证
    │   └── 检查SDMMC_CFLG_ARGS_ERR
    │
    ├── SDIO连接建立
    │   ├── sdio_connect() - 建立连接
    │   └── 设置回调函数
    │       ├── insertion - 设备插入
    │       ├── removal   - 设备移除
    │       └── event     - 事件处理
    │
    ├── 设备枚举
    │   ├── 自动扫描模式
    │   │   ├── 遍历所有总线
    │   │   ├── 分配HBA结构
    │   │   └── 调用sdmmc_attach()
    │   │
    │   └── 目标模式
    │       └── 连接指定设备
    │
    └── 启用枚举
        └── sdio_enum(SDIO_ENUM_ENABLE)
```

### 3. HBA初始化

```
sdmmc_sim_init()
    │
    ├── 分配扩展结构
    │   └── SIM_SDMMC_EXT
    │
    ├── 初始化队列
    │   ├── SIM队列
    │   └── 完成队列
    │
    ├── 创建设备线程
    │   ├── sdmmc_driver_thread()
    │   └── 设置优先级
    │
    ├── 电源管理初始化
    │   ├── 注册powerman
    │   └── 设置状态机
    │
    └── 分区配置
        └── sim_bs_partition_config()
```

---

## I/O处理流程

### 命令处理主循环

```
sdmmc_driver_thread()
    │
    ├── 等待命令
    │   ├── MsgReceivePulse()
    │   └── 检查队列
    │
    ├── 命令出队
    │   └── simq_ccb_dequeue()
    │
    ├── 命令分发
    │   ├── XPT_SCSI_IO     -> sdmmc_read_write()
    │   ├── XPT_RESET_BUS   -> sdmmc_reset_bus()
    │   ├── XPT_RESET_DEV   -> sdmmc_reset_dev()
    │   ├── XPT_ABORT       -> sdmmc_abort()
    │   └── XPT_DEVCTL      -> sdmmc_sim_action_devctl()
    │
    ├── 执行命令
    │   └── 调用相应处理函数
    │
    ├── 完成处理
    │   ├── 设置cam_status
    │   └── simq_ccb_complete()
    │
    └── 循环继续
```

### 读写操作流程

```
sdmmc_read_write()
    │
    ├── 设备就绪检查
    │   └── sdmmc_unit_ready()
    │
    ├── 权限验证
    │   ├── 写保护检查
    │   └── 锁定状态检查
    │
    ├── LBA解析
    │   ├── sdmmc_ccb_lba()
    │   └── 地址转换
    │
    ├── 分区设置
    │   └── sdio_set_partition()
    │
    ├── 后台操作检查
    │   └── sdmmc_bkops()
    │
    ├── 数据传输
    │   ├── 构建SGE列表
    │   ├── 可靠写入处理
    │   └── 调用sdmmc_rw()
    │
    └── 完成返回
```

### 可靠写入实现

```c
// 可靠写入逻辑
if (flgs & SCF_SBC_RLW) {
    // 计算可靠写入大小
    rel_wr_count = ext->dev_inf.rel_wr_sec_c * 512;
    
    // 分割大写入为小块
    for (每个SGE) {
        while (有数据) {
            // 地址对齐检查
            if (!SDMMC_ADDR_ALIGNED(lba, rel_wr_sec_c)) {
                count = 512;  // 使用最小块
            }
            
            // 执行可靠写入
            status = sdmmc_rw(hba, part, flgs, lba, count, 
                            &rw_sge, 1, ccb->cam_req_map, timeout);
        }
    }
}
```

---

## 分区管理

### 分区类型支持

| 分区类型 | 描述 | 配置值 |
|----------|------|--------|
| 用户数据 | 标准数据分区 | MMC_PART_USER |
| 启动分区1 | 引导代码 | MMC_PART_BOOT1 |
| 启动分区2 | 备用引导 | MMC_PART_BOOT2 |
| RPMB | 重放保护 | MMC_PART_RPMB |
| GP1-GP4 | 通用分区 | MMC_PART_GP1-4 |

### 分区配置流程

```
sim_bs_partition_config()
    │
    ├── 获取设备信息
    │   └── sdio_dev_info()
    │
    ├── 解析分区表
    │   ├── GPT分区表
    │   └── MBR分区表
    │
    ├── 创建分区结构
    │   ├── 计算LBA范围
    │   ├── 设置块大小
    │   └── 配置标志
    │
    ├── 注册分区
    │   └── 更新targets数组
    │
    └── 设置默认分区
```

### 分区标志

```c
#define SDMMC_PFLAG_WP        (1 << 0)    // 写保护
#define SDMMC_PFLAG_HIDDEN    (1 << 1)    // 隐藏分区
#define SDMMC_PFLAG_READONLY  (1 << 2)    // 只读
#define SDMMC_PFLAG_SYSTEM    (1 << 3)    // 系统分区
```

---

## 高级功能

### 1. TRIM支持

```c
// TRIM命令处理
case SBC_TRIM1:
case SBC_TRIM2:
    // 解析TRIM范围
    status = sdmmc_erase_devctl(hba, ccb);
    
    // 执行TRIM
    status = sdio_erase(device, start, end);
    
    // 更新统计
    part->tc += blocks;
    break;
```

### 2. 后台操作 (BKOPS)

```c
sdmmc_bkops()
    │
    ├── 检查状态
    │   └── 读取BKOPS状态寄存器
    │
    ├── 评估紧急程度
    │   ├── 非关键操作
    │   ├── 影响性能
    │   ├── 关键操作
    │   └── 正在进行
    │
    ├── 触发后台操作
    │   └── sdio_bkops_start()
    │
    └── 定时检查
        └── 设置下次检查时间
```

**BKOPS状态:**
- `BKOPS_STATUS_OPERATIONS_NONE`: 无操作
- `BKOPS_STATUS_OPERATIONS_NON_CRITICAL`: 非关键
- `BKOPS_STATUS_OPERATIONS_IMPACTED`: 影响性能
- `BKOPS_STATUS_OPERATIONS_CRITICAL`: 关键

### 3. 电源管理

```c
// 电源状态机
#define SDMMC_PM_IDLE     0    // 空闲
#define SDMMC_PM_SLEEP    1    // 休眠
#define SDMMC_PM_ACTIVE   2    // 激活
#define SDMMC_PM_SUSPEND  3    // 挂起

// 状态转换
sdmmc_suspend()  // 进入休眠
sdmmc_resume()   // 从休眠唤醒
```

### 4. 缓存管理

```c
// 缓存控制
if (ext->eflags & SDMMC_EFLAG_CACHE) {
    // 启用缓存
    sdio_cache_ctrl(device, SDIO_CACHE_ON);
    
    // 脏缓存处理
    if (ext->eflags & SDMMC_EFLAG_VCACHE_DIRTY) {
        sdio_cache_flush(device);
    }
}
```

---

## 错误处理

### 错误类型映射

| SDIO错误 | CAM状态 | 描述 |
|----------|---------|------|
| EOK | CAM_REQ_CMP | 成功 |
| ETIMEDOUT | CAM_CMD_TIMEOUT | 超时 |
| EIO | CAM_UNCOR_PARITY | I/O错误 |
| ENXIO | CAM_NO_DEVICE | 无设备 |
| EACCES | CAM_DATA_RUN_ERR | 访问拒绝 |

### 错误恢复策略

```c
sdmmc_error()
    │
    ├── 记录错误
    │   └── cam_slogf()记录
    │
    ├── 重试逻辑
    │   ├── 超时重试
    │   └── 总线重置
    │
    ├── 状态更新
    │   ├── 设置CAM状态
    │   └── 更新统计
    │
    └── 错误上报
        └── 返回给上层
```

### 总线重置

```c
sdmmc_reset()
    │
    ├── 停止当前操作
    │   └── 取消所有CCB
    │
    ├── 硬件重置
    │   ├── sdio_reset_bus()
    │   └── sdio_reset_device()
    │
    ├── 重新初始化
    │   └── 恢复设备状态
    │
    └── 继续处理
```

---

## 性能优化

### 1. 队列管理

```c
// SIM队列优化
simq_ccb_enqueue()    // 入队
simq_ccb_dequeue()    // 出队
simq_ccb_complete()   // 完成

// 队列深度控制
#define SDMMC_START_CCB_MAX  25  // 最大启动CCB数
```

### 2. 散射-聚集支持

```c
// SGE处理
if (ccb->cam_ch.cam_flags & CAM_SCATTER_VALID) {
    sgc = ccb->cam_sglist_cnt;
    sgp = (sdio_sge_t *)ccb->cam_data.cam_sg_ptr;
} else {
    // 单缓冲区模式
    sgc = 1;
    sgp = &sge;
    sgp->sg_count = ccb->cam_dxfer_len;
    sgp->sg_address = ccb->cam_data.cam_data_ptr;
}
```

### 3. DMA支持

```c
// DMA控制
if (ccb->cam_ch.cam_flags & CAM_DATA_PHYS) {
    flgs |= SCF_DATA_PHYS;  // 物理地址模式
}

// 零拷贝优化
if (支持DMA) {
    // 直接使用物理地址
    // 无需数据复制
}
```

### 4. 并发控制

```c
// 互斥保护
pthread_mutex_t  *mutex;     // 队列互斥
pthread_cond_t   *cond;      // 条件变量

// 原子操作
atomic_add()     // 计数器更新
atomic_set()     // 标志设置
```

---

**总结**

DEVB SDMMC驱动是QNX中功能完整的块设备驱动实现，提供了从基本读写到高级存储功能的完整支持。通过CAM架构集成，实现了设备无关的存储接口，支持多种SD/MMC设备类型，并包含了现代存储设备的各种优化特性。

关键设计特点：
- **模块化架构**: CAM/SIM/SDIO清晰分层
- **功能完备**: 支持分区、TRIM、电源管理等
- **性能优化**: 队列管理、DMA、并发控制
- **错误恢复**: 完善的错误处理和重试机制
- **可扩展性**: 支持多种设备类型和配置选项

这为QNX系统提供了稳定高效的存储设备支持。