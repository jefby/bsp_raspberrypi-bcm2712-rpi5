# DEVB SDMMC驱动流程图与时序分析

## 目录
1. [初始化时序](#初始化时序)
2. [命令处理流程](#命令处理流程)
3. [读写操作时序](#读写操作时序)
4. [分区管理流程](#分区管理流程)
5. [错误处理流程](#错误处理流程)
6. [电源管理状态机](#电源管理状态机)
7. [后台操作流程](#后台操作流程)
8. [中断处理时序](#中断处理时序)
9. [热插拔处理](#热插拔处理)
10. [性能监控](#性能监控)

---

## 初始化时序

### 模块加载时序

```
时间轴: 0ms → 100ms → 500ms → 1s → 2s
        │      │       │      │     │
        ▼      ▼       ▼      ▼     ▼

   模块注册          SDIO连接      设备枚举      分区配置     就绪状态
   sim_module        sdio_connect  sdmmc_attach  partition    服务开始
   .name="sdmmc"     (funcs设置)   (HBA分配)    _config       接受命令
   .args=parse       .insertion    .instance    (GPT/MBR)     (CAM_REQ_CMP)
   .attach=attach    .removal      .device      解析
   .detach=detach    .event        .hc_inf      注册

参数: verbosity=1, nhba=0, pathid_max=0
状态: SDMMC_CFLAG_ENUMERATING → 清除
```

### HBA初始化详细时序

```
sdmmc_sim_init(hba, path)
    │
    ├── 0-5ms: 扩展结构分配
    │   ext = calloc(sizeof(SIM_SDMMC_EXT))
    │   ext->hba = hba
    │   ext->eflags = 0
    │
    ├── 5-10ms: 队列初始化
    │   simq_init(hba->simq)
    │   TAILQ_INIT(&hba->ccb_queue)
    │
    ├── 10-20ms: 线程创建
    │   pthread_create(&tid, NULL, sdmmc_driver_thread, hba)
    │   pthread_setschedprio(tid, SDMMC_SCHED_PRIORITY)
    │   hba->tid = tid
    │
    ├── 20-50ms: 电源管理设置
    │   if (SDMMC_EFLAG_POWMAN)
    │       powman_attach(&ext->powman_hdl)
    │       pm_state = SDMMC_PM_IDLE
    │
    ├── 50-100ms: 设备信息获取
    │   sdio_dev_info(ext->device, &ext->dev_inf)
    │   读取: caps, max_dtr, rel_wr_sec_c
    │
    └── 100-200ms: 分区配置
        sim_bs_partition_config(hba)
        解析分区表，创建分区结构
```

---

## 命令处理流程

### CAM命令分发流程图

```
sdmmc_sim_action(hba, ccbp)
│
├── 验证ccb参数
│   ├── 检查ccb->cam_flags (CAM_CDB_PHYS等)
│   ├── 验证target_id < ext->ntargs
│   └── 验证target_lun < targets[].nluns
│
├── 根据cam_func_code分发
│
├── XPT_SCSI_IO ──────────┐
│   │                     │
│   └── 设置CAM_REQ_INPROG │
│       入队到simq         │
│       发送脉冲给线程     │
│                         │
├── XPT_RESET_BUS ──────┐ │
│   │                   │ │
│   └── 总线重置处理     │ │
│                         │ │
├── XPT_RESET_DEV ─────┐ │ │
│   │                  │ │ │
│   └── 设备重置处理    │ │ │
│                        │ │ │
├── XPT_ABORT ─────────┐ │ │ │
│   │                 │ │ │ │
│   └── 中止命令处理   │ │ │ │
│                       │ │ │ │
├── XPT_DEVCTL ───────┐ │ │ │ │
│   │                │ │ │ │ │
│   └── 设备控制处理  │ │ │ │ │
│                      │ │ │ │ │
└── 其他命令 ──────────┼─┼─┼─┼─┘
    │                  │ │ │ │
    └── CAM_REQ_INVALID │ │ │ │
                         │ │ │ │
线程处理分支 ─────────────┼─┼─┼─┘
    │                     │ │ │
    └── sdmmc_driver_thread │ │
        │                   │ │
        └── 命令执行 ────────┼─┘
            │                │
            └── 完成处理 ─────┘
                │
                └── simq_ccb_complete()
```

### 驱动线程主循环

```
sdmmc_driver_thread(hdl)
│
├── 初始化
│   hba = (SIM_HBA *)hdl
│   ext = (SIM_SDMMC_EXT *)hba->ext
│
├── 主循环 while(1)
│   │
│   ├── 等待事件
│   │   ├── MsgReceivePulse(coid, &pulse)
│   │   └── 检查队列状态
│   │
│   ├── 脉冲处理
│   │   ├── SIM_ENQUEUE: 有新命令
│   │   └── 其他脉冲: 特殊事件
│   │
│   ├── 命令出队
│   │   ccb = simq_ccb_dequeue(hba->simq)
│   │   if (ccb == NULL) continue
│   │
│   ├── 命令执行
│   │   ├── 解析命令类型
│   │   ├── 调用相应处理函数
│   │   └── 更新状态
│   │
│   ├── 完成处理
│   │   ├── 设置ccb->cam_status
│   │   ├── simq_ccb_complete(hba->simq, ccb)
│   │   └── 释放资源
│   │
│   └── 检查退出条件
│       └── 线程退出处理
│
└── 清理退出
```

---

## 读写操作时序

### 标准读写时序图

```
应用程序调用                    CAM层处理                    SIM层处理                    SDIO层处理
    │                              │                              │                              │
    │  read/write(fd, buf, len)    │                              │                              │
    │ ──────────────────────────►  │                              │                              │
    │                              │ cam_io()                     │                              │
    │                              │ - 构建CCB_SCSIIO             │                              │
    │                              │ - 设置CDB (READ10/WRITE10)   │                              │
    │                              │ - 设置数据缓冲区             │                              │
    │                              │ - cam_ccb_wait()             │                              │
    │                              │                              │                              │
    │                              │                              │ sdmmc_sim_action()           │
    │                              │                              │ - 验证参数                   │
    │                              │                              │ - 设置CAM_REQ_INPROG         │
    │                              │                              │ - simq_ccb_enqueue()         │
    │                              │                              │ - MsgSendPulse()             │
    │                              │                              │                              │
    │                              │                              │                              │ sdmmc_driver_thread()
    │                              │                              │                              │ - 接收脉冲
    │                              │                              │                              │ - simq_ccb_dequeue()
    │                              │                              │                              │ - 解析SCSI命令
    │                              │                              │                              │ - 调用sdmmc_read_write()
    │                              │                              │                              │                              │
    │                              │                              │                              │ sdmmc_read_write()
    │                              │                              │                              │ - sdmmc_unit_ready()
    │                              │                              │                              │ - 权限检查
    │                              │                              │                              │ - LBA解析
    │                              │                              │                              │ - 分区设置
    │                              │                              │                              │ - 后台操作检查
    │                              │                              │                              │ - 构建SGE
    │                              │                              │                              │ - 调用sdmmc_rw()
    │                              │                              │                              │                              │
    │                              │                              │                              │                              │ sdio_read/write()
    │                              │                              │                              │                              │ - 协议处理
    │                              │                              │                              │                              │ - 数据传输
    │                              │                              │                              │                              │ - 完成通知
    │                              │                              │                              │                              │ ◄────────────────────
    │                              │                              │                              │ sdmmc_rw()完成
    │                              │                              │                              │ - 检查状态
    │                              │                              │                              │ - 返回CAM_REQ_CMP
    │                              │                              │                              │ ◄────────────────────
    │                              │                              │ sdmmc_driver_thread()继续
    │                              │                              │ - simq_ccb_complete()
    │                              │                              │ - 唤醒等待线程
    │                              │                              │ ◄────────────────────────────
    │                              │ cam_ccb_wait()返回           │                              │
    │                              │ - 检查ccb->cam_status        │                              │
    │                              │ - 返回结果给应用             │                              │
    │                              │ ◄────────────────────────────                              │
    │  read/write()返回结果        │                              │                              │
    │ ◄────────────────────────────                              │                              │
```

### 可靠写入时序

```
大块写入请求
    │
    ├── 检测到SCF_SBC_RLW标志
    │
    ├── 计算可靠写入参数
    │   rel_wr_count = dev_inf.rel_wr_sec_c * 512
    │
    ├── 分割写入操作
    │   for (每个SGE段)
    │   │
    │   ├── 地址对齐检查
    │   │   if (!SDMMC_ADDR_ALIGNED(lba, rel_wr_sec_c))
    │   │       count = 512  // 使用最小块
    │   │
    │   ├── 构建小块SGE
    │   │   rw_sge.sg_count = count
    │   │   rw_sge.sg_address = sg_addr
    │   │
    │   ├── 执行可靠写入
    │   │   sdmmc_rw(hba, part, flgs, lba, count,
    │   │            &rw_sge, 1, req_map, timeout)
    │   │
    │   ├── 更新指针
    │   │   sg_count -= count
    │   │   sg_addr += count
    │   │   lba += (count / 512)
    │   │
    │   └── 循环直到完成
    │
    └── 返回成功
```

---

## 分区管理流程

### 分区发现流程图

```
sim_bs_partition_config(hba)
│
├── 获取设备信息
│   sdio_dev_info(device, &dev_inf)
│   获取: 容量, 块大小, 分区信息
│
├── 检查分区表类型
│   ├── 尝试GPT
│   │   ├── 读取LBA 1 (GPT头)
│   │   ├── 验证签名 "EFI PART"
│   │   └── 读取分区条目
│   │
│   └── 回退到MBR
│       ├── 读取LBA 0
│       ├── 检查引导签名 0x55AA
│       └── 解析分区条目
│
├── 创建设区结构
│   for (每个分区)
│   │
│   ├── 计算LBA范围
│   │   slba = 分区起始扇区
│   │   elba = slba + 分区大小 - 1
│   │   nlba = 分区大小
│   │
│   ├── 设置块大小
│   │   blk_shft = 计算移位值
│   │   (512字节 = 0, 1024字节 = 1, etc.)
│   │
│   ├── 配置分区标志
│   │   ├── 写保护检查
│   │   ├── 只读属性
│   │   └── 系统分区标记
│   │
│   └── 命名分区
│       name = "mmcsd0t1", "mmcsd0t2", etc.
│
├── 更新目标数组
│   targets[target_id].partitions[lun] = partition
│   targets[target_id].nluns++
│
└── 设置默认分区
    通常lun 0为用户数据分区
```

### 分区切换时序

```
分区切换请求 (DEVCTL)
    │
    ├── 接收DCMD_CAM_DEV_PARTITION
    │
    ├── 验证分区存在
    │   检查partition < SDMMC_PARTITION_MAX
    │
    ├── 设置活动分区
    │   sdio_set_partition(device, config)
    │   config = MMC_PART_USER/GP1/etc.
    │
    ├── 更新当前分区指针
    │   ext->current_partition = partition
    │
    └── 返回成功
```

---

## 错误处理流程

### 错误检测和恢复流程

```
I/O操作中检测到错误
    │
    ├── SDIO层返回错误
    │   status = sdio_read/write() 返回值
    │
    ├── 错误分类
    │   ├── ETIMEDOUT → 超时错误
    │   ├── EIO → I/O错误
    │   ├── ENXIO → 设备不存在
    │   └── EACCES → 访问拒绝
    │
    ├── 重试逻辑
    │   if (可重试错误 && retry_count < SDMMC_RW_RETRIES)
    │   │
    │   ├── 延迟重试
    │   │   delay(100ms)
    │   │
    │   ├── 重新执行操作
    │   │   sdmmc_rw() 再次调用
    │   │
    │   └── 更新重试计数
    │       retry_count++
    │
    ├── 总线重置
    │   if (重试失败)
    │       sdmmc_reset(hba)
    │       ├── sdio_reset_bus()
    │       └── 重新初始化设备
    │
    ├── 错误映射
    │   sdmmc_error(hba, ccb, status)
    │   ├── 设置ccb->cam_status
    │   ├── 记录错误日志
    │   └── 更新错误统计
    │
    └── 返回错误状态
        上层处理错误
```

### 总线重置详细流程

```
sdmmc_reset(hba)
│
├── 停止当前操作
│   ├── 取消所有待处理CCB
│   └── 设置设备忙状态
│
├── 硬件重置
│   ├── sdio_reset_bus()
│   │   └── 发送CMD0 (GO_IDLE_STATE)
│   │
│   ├── sdio_reset_device()
│   │   └── 重新初始化设备
│   │       ├── CMD2 (ALL_SEND_CID)
│   │       ├── CMD3 (SET_RELATIVE_ADDR)
│   │       └── CMD7 (SELECT_CARD)
│   │
│   └── 恢复时钟和总线宽度
│
├── 重新配置分区
│   sim_bs_partition_config(hba)
│
├── 清除错误状态
│   ext->eflags &= ~SDMMC_EFLAG_DEV_BUSY
│
└── 恢复正常操作
```

---

## 电源管理状态机

### 电源状态转换图

```
SDMMC_PM_IDLE                    SDMMC_PM_ACTIVE
     │                                 │
     │ 收到I/O请求                     │ I/O完成且空闲超时
     │                                 │
     ▼                                 ▼
┌─────────────┐  休眠命令   ┌─────────────┐  唤醒事件
│             │ ─────────►  │             │ ◄─────────
│   ACTIVE    │             │   SLEEP     │
│   (工作)    │ ◄─────────  │   (休眠)    │
│             │   唤醒      │             │
└─────────────┘             └─────────────┘
     │                                 │
     │ 系统挂起                       │ 系统恢复
     ▼                                 ▼
SDMMC_PM_SUSPEND                   SDMMC_PM_SUSPEND
     │                                 │
     │ 保持挂起状态                   │ 保持挂起状态
     ▼                                 ▼
```

### 状态转换逻辑

```c
// 状态转换函数
sdmmc_suspend(hba)
    │
    ├── 保存当前状态
    │   old_state = ext->pm_state
    │
    ├── 停止I/O操作
    │   取消所有待处理命令
    │
    ├── 设置休眠状态
    │   ext->pm_state = SDMMC_PM_SLEEP
    │   sdio_suspend(device)
    │
    └── 通知powerman
        pm_state_transition(powman_hdl, pm_state_suspend)

sdmmc_resume(hba)
    │
    ├── 恢复设备
    │   sdio_resume(device)
    │
    ├── 重新初始化
    │   sdmmc_reset(hba)  // 轻量级重置
    │
    ├── 恢复状态
    │   ext->pm_state = SDMMC_PM_ACTIVE
    │
    └── 通知powerman
        pm_state_transition(powman_hdl, pm_state_on)
```

### 空闲检测定时器

```
PM定时器处理
    │
    ├── 定时器到期
    │   pm_timestamp + pm_idle_time_ns
    │
    ├── 检查活动状态
    │   if (有待处理命令)
    │       重置定时器
    │       return
    │
    ├── 进入休眠
    │   sdmmc_suspend(hba)
    │
    └── 设置唤醒条件
        等待下次I/O请求
```

---

## 后台操作流程

### BKOPS状态机

```
空闲状态                    非关键操作
     │                           │
     │ 检测到需要维护             │ 继续正常I/O
     │                           │
     ▼                           ▼
┌─────────────┐  性能下降  ┌─────────────┐  完成
│             │ ────────►  │             │ ───────►
│  NONE       │            │  NON_CRIT   │
│  (无操作)   │ ◄────────  │  (非关键)   │
└─────────────┘  维护完成  └─────────────┘
     │                           │
     │ 紧急维护需求              │ 性能严重下降
     ▼                           ▼
┌─────────────┐           ┌─────────────┐
│             │           │             │
│  IMPACTED   │           │  CRITICAL   │
│  (影响性能) │           │  (关键)     │
└─────────────┘           └─────────────┘
     │                           │
     │ 正在执行维护              │ 正在执行维护
     ▼                           ▼
┌─────────────┐           ┌─────────────┐
│             │           │             │
│  IN_PROG    │           │  IN_PROG    │
│  (进行中)   │           │  (进行中)   │
└─────────────┘           └─────────────┘
```

### BKOPS执行流程

```
sdmmc_bkops(hba, tick)
│
├── 读取BKOPS状态
│   status = sdio_bkops_status(device)
│
├── 评估紧急程度
│   switch (status & 0x0F)
│   │
│   ├── BKOPS_STATUS_OPERATIONS_NONE
│   │   └── 无需操作
│   │
│   ├── BKOPS_STATUS_OPERATIONS_NON_CRITICAL
│   │   └── 记录状态，继续
│   │
│   ├── BKOPS_STATUS_OPERATIONS_IMPACTED
│   │   └── 增加tick计数
│   │       bkops_ticks++
│   │
│   └── BKOPS_STATUS_OPERATIONS_CRITICAL
│       └── 立即启动维护
│
├── 检查维护条件
│   if (tick || bkops_ticks >= BKOPS_IMPACTED_TICKS)
│   │
│   ├── 启动后台维护
│   │   sdio_bkops_start(device)
│   │   设置IN_PROG状态
│   │
│   └── 重置计数器
│       bkops_ticks = 0
│
└── 设置下次检查
    定时器: SDMMC_TIME_BKOPS (5秒)
```

---

## 中断处理时序

### SDIO事件处理时序

```
硬件中断发生
    │
    ├── SDIO中断控制器
    │   识别中断源 (卡插入/移除/事件)
    │
    ├── 调用注册回调
    │   funcs.event(instance, event)
    │
    ├── 事件分发
    │   ├── SDIO_EVENT_INSERTION
    │   │   └── sdmmc_sdio_insertion()
    │   │
    │   ├── SDIO_EVENT_REMOVAL
    │   │   └── sdmmc_sdio_removal()
    │   │
    │   └── SDIO_EVENT_GENERAL
    │       └── sdmmc_sdio_event()
    │
    ├── 设备插入处理
    │   ├── 验证设备类型
    │   ├── 分配HBA结构
    │   ├── 初始化设备
    │   └── 注册到CAM
    │
    ├── 设备移除处理
    │   ├── 停止所有操作
    │   ├── 清理资源
    │   ├── 注销HBA
    │   └── 通知上层
    │
    └── 通用事件处理
        ├── 卡状态变化
        ├── 电源事件
        └── 错误事件
```

### 命令完成中断

```
SDIO操作完成
    │
    ├── 硬件完成数据传输
    │
    ├── 触发完成中断
    │   SDIO中断线激活
    │
    ├── 中断服务例程
    │   sdio_isr() - SDIO驱动处理
    │
    ├── 唤醒等待线程
    │   信号量/事件通知
    │
    ├── SIM层处理
    │   检查操作状态
    │   更新统计信息
    │
    └── 继续下一个操作
```

---

## 热插拔处理

### 设备插入流程

```
sdmmc_sdio_insertion(instance)
│
├── 验证设备
│   检查VID/DID匹配兴趣列表
│
├── 查找空闲HBA
│   遍历hlist查找可用插槽
│
├── 创建设备实例
│   if (hba == NULL)
│       hba = sdmmc_alloc_hba()
│
├── 连接设备
│   sdmmc_attach(hba, connection, instance)
│   ├── sdio_device_attach()
│   ├── 获取设备信息
│   └── 初始化硬件
│
├── 配置设备
│   设置时钟、总线宽度
│   启用功能
│
├── 分区枚举
│   sim_bs_partition_config()
│
├── 注册到CAM
│   xpt_bus_register(hba)
│
└── 通知系统
    创建设备节点 (/dev/mmcsd0)
```

### 设备移除流程

```
sdmmc_sdio_removal(instance)
│
├── 查找对应HBA
│   根据instance查找hba
│
├── 停止操作
│   取消所有待处理CCB
│   设置移除标志
│
├── 清理资源
│   sdmmc_detach(hba, CAM_TRUE)
│   ├── 关闭设备连接
│   ├── 释放分区信息
│   └── 停止线程
│
├── 注销CAM
│   xpt_bus_deregister(hba)
│
├── 释放HBA
│   if (自动分配)
│       sdmmc_free_hba(hba)
│
└── 通知系统
    移除设备节点
```

---

## 性能监控

### 统计信息收集

```
I/O完成时更新统计
    │
    ├── 读取计数器
    │   part->rc++  // 读取计数
    │   part->wc++  // 写入计数
    │
    ├── 字节统计
    │   根据操作类型累加
    │   读取: part->rc += blocks
    │   写入: part->wc += blocks
    │
    ├── TRIM统计
    │   part->tc += blocks
    │   part->dc += blocks  // 丢弃
    │
    ├── 错误统计
    │   超时、校验错误等
    │
    └── 性能指标
        IOPS = 操作数 / 时间
        吞吐量 = 字节数 / 时间
```

### 队列深度监控

```
命令队列状态
    │
    ├── 活动命令数
    │   simq->active_count
    │
    ├── 队列长度
    │   TAILQ_LENGTH(&ccb_queue)
    │
    ├── 等待时间
    │   计算入队到出队的时间
    │
    └── 拥塞控制
        if (队列过长)
            延迟新命令
```

### 缓存性能

```
缓存统计
    │
    ├── 缓存命中率
    │   命中操作 / 总操作
    │
    ├── 脏页比率
    │   脏块 / 总块
    │
    ├── 刷新频率
    │   刷新操作 / 时间
    │
    └── 优化建议
        基于统计调整缓存策略
```

---

**总结**

DEVB SDMMC驱动的流程设计体现了现代存储驱动的复杂性和优化需求。从初始化到命令处理，再到错误恢复和电源管理，每一个环节都经过精心设计以确保性能、可靠性和兼容性。

关键流程特点：
- **异步处理**: 命令队列和线程分离
- **状态机驱动**: 电源管理和后台操作
- **错误恢复**: 多级重试和重置机制
- **热插拔支持**: 动态设备管理
- **性能监控**: 全面的统计和优化

这些流程确保了驱动在各种使用场景下的稳定性和高效性。