# OTA Client 流程梳理

> 基于 `src/ota/ota_client.cpp` + `images/rpi5.build` 整理

---

## 1. 启动链

```
RPi5 上电
  └─ startup-bcm2712-rpi5（硬件初始化）
      └─ procnto-smp（QNX 微内核）
          └─ startup-script（rpi5.build 内嵌脚本）
              ├─ 驱动依次启动（WDT / PCI / UART / SPI / SDMMC / USB）
              ├─ .storage-server.sh（devf-ram → /var/run /var/db）
              ├─ net_start.sh          ← OTA 在此启动
              ├─ customize_startup.sh
              └─ ksh（交互 shell）
```

### net_start.sh 关键顺序

```
1. gpio-rp1 set 32 op pd dh        # 拉低以太网复位引脚
2. io-sock（加载网卡驱动）
3. dhcpcd -bqq                      # 后台获取 IP + DNS
4. .ssh-server.sh
5. ota_client -c /etc/ota_config -d # -d: fork+setsid 守护进程
6. hwstatusd / qconn ...
```

> **顺序关键**：dhcpcd 必须先于 ota_client，否则 DNS 未就绪。

---

## 2. main() 初始化

```
main()
  ├─ 默认配置（内存）
  │     server_url      = http://192.168.50.148:8080
  │     check_interval  = 300 s
  │     boot_path       = /var/boot
  │     version_file    = /var/boot/ota_version   ← 固定跟随 boot_path
  │     config_file     = /var/boot/config.txt
  │     log_file        = /tmp/ota_client.log
  │     ota_config_path = /etc/ota_config
  │
  ├─ 检查 /var/boot 是否已挂载（/proc/mounts 或 mount 输出）
  │     未挂载 → mount -t dos /dev/sd0t12 /var/boot（最多 5 次）
  │
  ├─ 参数：-c 配置文件 / -d 守护进程 / -h 帮助
  ├─ read_config()
  ├─ 若 -d：fork + setsid
  └─ ota_loop()
```

---

## 3. 版本提交事务（核心）

**版本号在「新槽成功引导之后」才写入 `ota_version`。**

```
try_apply_update:
  download+SHA256+size → write_pending(槽,版本,切槽时 uptime/墙钟) → set_active_ifs → shutdown -v

settle_pending（进程启动时一次）:
  active==expected 且确认真的重启过 → write_version → clear pending
  active==expected 但无重启证据     → 保留 pending（进程重启 ≠ 系统重启，不 commit）
  else                             → 删未用镜像 → clear pending（版本不动，可重试）
  commit 失败                      → 保留 pending，本轮不发起新升级
  config.txt 读不出                → 推迟 settle（不猜槽位）
```

`ota_pending` 五行：`IFS 名` + `版本号` + `切槽时 uptime 秒` + `切槽时墙钟秒` + 见证位（0/1）。
缺后三行的旧文件按「已重启」处理，保持原行为。

「确认真的重启过」的判据按可靠性排序（见 `system_rebooted_since`）：

| 判据 | 依据 | 说明 |
|------|------|------|
| 见证文件 | `/tmp/ota_switch_witness` 是否还在 | `/tmp` 是 `[type=link] /dev/shmem`（内核内存 fs），重启必清空。文件在 = 同一个 boot，文件没了 = 重启过。不受校时影响。创建失败时见证位记 0，退回下面两条 |
| 单调钟回绕 | 当前 uptime < 切槽时 uptime | QNX 文档：`CLOCK_MONOTONIC` 的 tv_sec 是 seconds since system boot 且不可调整 |
| boot 时刻推进 | 墙钟 − uptime 即内核 `qtime->boot_time` | 切槽到下次内核启动之间必有 `request_reboot()` 的 10s 等待 + 关机 + 固件重启（实测约 12-16s），仅进程重启时约 0，阈值 5s。补上一条的盲区：切槽若发生在开机后很早，重启后到 settle 的 uptime 可能仍 ≥ 记录值 |

---

## 4. OTA 主循环

```
ota_loop()
  settle_pending_on_boot()          # 仅进程启动时一次
  while(true):
    if pending_blocks_update():     # pending 未确认：阻塞新升级
      if need_reboot: 再次请求重启    # config.txt 读不出时不请求，避免重启循环
      sleep(interval); continue
    check_and_update_config()
    if newer(server, local):
      try_apply_update()            # 成功则 set_active_ifs + shutdown -v
    sleep(interval)
```

---

## 5. A/B 槽

| 轮次 | 当前 kernel= | 目标槽 |
|------|-------------|--------|
| 第 1 次 | `ifs-rpi5.bin`（A） | `ifs-rpi5_B.bin`（B） |
| 第 2 次 | `ifs-rpi5_B.bin`（B） | `ifs-rpi5.bin`（A） |

`get_active_ifs` / `set_active_ifs` 只识别**非注释**的 `kernel=` 行。

---

## 6. 版本号格式

| 格式 | 示例 | 说明 |
|------|------|------|
| `YYYY.WW.N` | `2026.18.1` | 年.ISO周.序号 |
| 整数 `N` | `3` | 兼容旧格式，视为 year=0,week=0,seq=N |

---

## 7. 关键路径

| 路径 | 说明 |
|------|------|
| `/var/boot/` | FAT 分区（`/dev/sd0t12`） |
| `/var/boot/ifs-rpi5.bin` | 槽 A |
| `/var/boot/ifs-rpi5_B.bin` | 槽 B |
| `/var/boot/config.txt` | `kernel=` 选槽 |
| `/var/boot/ota_version` | 已提交版本（boot 成功后写入） |
| `/var/boot/ota_pending` | 进行中事务（槽 + 待提交版本 + 切槽时刻 + 见证位） |
| `/tmp/ota_switch_witness` | 切槽时创建的重启见证（`/tmp` → `/dev/shmem`，重启即消失） |
| `/etc/ota_config` | OTA 配置（IFS 内只读，可热更新到 /tmp） |
| `/tmp/ota_client.log` | 日志 |

---

## 8. 错误处理

| 阶段 | 行为 |
|------|------|
| /var/boot 挂载失败 | 进程退出 |
| HTTP 非 2xx / curl 失败 | 本轮跳过，重试 |
| SHA256 / 大小校验失败 | 删镜像，重试 |
| 写 pending 失败 | 删镜像，重试 |
| set_active_ifs 失败 | 清 pending、删镜像，版本不变，重试 |
| boot 成功但写版本失败 | settle 返回 false，每轮重试 commit |
| 仅 ota_client 进程重启（系统未重启） | 不提交版本，保留 pending，继续请求重启 |
| config.txt 读不出（无 `kernel=` 行） | 不猜槽位：settle 推迟、升级阻塞、不请求重启（避免重启循环） |
| 远端 ota_config 无 OTA_SERVER= | 拒绝热加载 |
| 新 IFS 无法启动 | 应用层无法回滚（需 bootloader/watchdog） |

---

## 9. 安全与刷盘

- 下载 IFS、写 `config.txt` / `ota_version` / `ota_pending` 后尽量 `fsync`
- 重启：`system("shutdown -v")`（保持原写法；默认 type 为 reboot）
- SHA256 比对前将远端哈希规范为小写

---

## 10. 待完善

- [ ] HTTPS / 证书校验
- [ ] 断点续传
- [ ] 启动计数 / bootloader 级失败回滚
- [ ] 数字签名（RSA）
