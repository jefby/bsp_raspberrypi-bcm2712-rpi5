# RPi5 PCI/PCIe 分析

> 基于 `src/hardware/startup/lib`、`src/hardware/support/bcm2712/msix-rp1`、
> `src/hardware/startup/boards/bcm/bcm2712` 的代码审计，以及板上实测
> （QNX rpi5-qnx 8.0.0，aarch64le）整理。
>
> 说明：搜索时大量 `psci`（PSCI，电源状态接口）是误匹配，已排除。
> 本文只讨论真正的 PCI / PCIe 组件。

---

## 1. 硬件背景：两个独立的 PCIe 域

Pi 5（BCM2712 + RP1）有两部分与 PCI 相关，本 BSP 分别做了不同处理：

| 域 | 位置 | 承载内容 | 本仓库的处理 |
|---|---|---|---|
| **RP1 内部 PCIe** | SoC ↔ RP1 芯片间内部链路（bus 1） | onboard GbE 等 SoC 外设；RP1 全部外设中断经 MSI-X 路由 | `msix-rp1` 工具配置其 MSI-X 控制器 |
| **扩展口 (Extension Port)** | M.2 / HAT PCIe x1 插槽（用户可见） | 外接 PCIe 卡 | `init_pcie_ext_msi_controller()` + 8 个边沿 IRQ |

```
BCM2712 SoC
  │
  ├──(内部 PCIe, bus 1)──► RP1 域 ──► onboard GbE (cgem0, 0x1DE4:0x0001)
  │                            ▲ 所有外设中断经 MSI-X → GICv2   [msix-rp1]
  │
  └──(PCIe x1 扩展口 / M.2)──► 外接 PCIe 卡
                               ▲ 8 个 IRQ → GICv2 向量 287~295 (边沿触发)
                                [init_pcie_ext_msi_controller + init_intrinfo]
```

> 注意：Pi 5 的 GbE 不是 SoC 内部集成，而是通过内部 PCIe 链路上挂在 RP1 侧的一个独立网卡芯片。

---

## 2. 中断映射关系

GICv2 **不支持 MSI**（Message Signaled Interrupts），因此处理 PCIe 设备必须外挂 MSI-X 控制器/适配器。Pi 5 用 MIP (Multi-Port INT controller) 做 MSI-X。两个域各有一套：

### RP1 内部域（`msix-rp1/main.c`）
- MSI-X 基址 `RP1_PCIE_MSIX_ADDR = 0x1f00108000`，大小 `0x1000`，64 个中断槽。
- 把 RP1 的 **64 个外设中断**逐条映射到 GICv2：
  - GICD 基址 `GICD_PADDR = 0x107fff9000`，写 ICfR（`+0xc00`）配置。
  - IRQ 索引定义见 `RP1_PCIE_MSIX_IRQ_*`（如 ETH=6、USB2=31、USB3=36、I2C0..5、SPI0..8、UART0..5、MIPI0/1 等）。
- MIP INT 控制器基址 `BCM2712_MIP_INT_CONTROLLER_ADDR = 0x1000130000`，配置 MASKL/H_HOST/VPU。

### 扩展口（`init_intrinfo.c` + `rpi5/init_hwinfo.c`）
- 8 个 IRQ 映射到 GICv2 向量 **287~295**（`PCIE_EXT_IRQ_0=287`, `PCIE_EXT_IRQ_NUM=8`），设为**边沿触发**。
- MSI-X 控制器基址 `PCIE_EXT_MIP_INTC_ADDR = 0x1000131000`：host 端全 unmask、VPU 端全 mask，并写 CFG。

---

## 3. 组件分类

### 3.1 本板定制代码（活跃）

| 文件 | 作用 |
|---|---|
| `src/hardware/support/bcm2712/msix-rp1/main.c` | 独立用户态工具，配置 RP1 PCIe MSI-X。把 64 个外设中断映射到 GICv2 + 初始化 MIP INT 控制器掩码。CLI：`-c` 清理、`-i irq1,irq2,...` 指定中断列表。访问设备用 `PCI_BDF(1,0,0)`（bus 1 = RP1 域）。 |
| `src/hardware/startup/boards/bcm/bcm2712/rpi5/init_hwinfo.c::init_pcie_ext_msi_controller()` | 启动时配置**扩展口** MSI-X（MIP）控制器掩码/CFG。在 `main.c:233` 被调用。 |
| `src/hardware/startup/boards/bcm/bcm2712/init_intrinfo.c` | 把扩展口 8 个 IRQ（GICv2 向量 287~295）设为边沿触发；并初始化 GICv2（`gic_v2_init(GICD_PADDR, GICC_PADDR)`）。 |

### 3.2 QNX BSP 存量代码（树里有，非本板新写）

| 文件 | 作用 / 状态 |
|---|---|
| `startup/lib/pci_read_cfg{8,16}.c`、`pci_write_cfg{8,16}.c` | 低层 PCI 配置寄存器读写薄封装。依赖架构相关的 `pci_read_cfg32`/`write_cfg32`——**本仓库无源码实现**，由 QNX 预编 `libstartup.a`（aarch64/a.le）提供。 |
| `startup/lib/hw_ser8250_pci.c` (`init_8250_pci`) | PCI BAR 上的 8250 串口初始化（解析 bus/dev/func/BAR 配置行）。仅声明在 `startup.h`，**无活跃调用者 → 此构建下未使用**。 |
| `startup/lib/hwibus_add_pci.c` | 向 hwinfo 树添加 PCI 总线（供驱动枚举）。只被自身 `.dep` 和头文件引用，**无驱动调用 → 未使用**。 |
| `startup/lib/aarch64/callout_interrupt_t18x_pcie.S` / `_ic6.S` | 非 MSI 的 PCIe 中断汇编处理（T18x/RPi5）。未在构建配置中被引用，应属预编 startup 库的一部分。 |

### 3.3 禁用 / 死代码（未编译）

| 文件 | 原因 |
|---|---|
| `src/hardware/devb/sdmmc/sdiodi/pci.c` + `base.c` 中所有 `#ifdef SDIO_PCI_SUPPORT` 块 | **SDIO-over-PCI**（PCIe 上的 SDIO 主机控制器，含 MSI-X/MSI 配置）。宏 `SDIO_PCI_SUPPORT` 在构建配置、头文件里**均未定义** → 整条路径未编译。 |
| `src/hardware/devb/include/pci_devices.h` | 上千行 PCI 厂商/设备 ID 表（各网卡/SATA/IDE）。仅被上述禁用的 SDIO-PCI 路径及存量 devb 驱动使用，本仓库无活跃引用。（该表里 Realtek=0x10EC、Broadcom=0x14E4，均不含 onboard GbE 的 0x1DE4。） |

### 3.4 GPIO 上的 PCIe 相关引脚定义

- `support/bcm2712/gpio-bcm/gpio.c`：`PCIE_SDA`、`PCIE_SCL`（PCIe I²C 侧带总线）。
- `support/bcm2712/gpio-rp1/gpio.c`：`PCIE_CLKREQ_N`、`RP1_PCIE_CLKREQ_N`、`PCIE_RP1_WAKE`（时钟请求/唤醒信号）。

---

## 4. 实测验证（板上）

> 登录流程：`ssh qnxuser@192.168.50.54` → `su root`（凭据不记录明文）。
> 坑记录：QNX sshd 需交互认证，加 `BatchMode=yes` 会导致登录失败；去掉后正常。
> `su` 可非交互：`printf 'root\n' | su -c '...' root`。

### onboard GbE = `cgem0`

```
ifconfig cgem0:
  flags=UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST
  ether 88:a2:9e:bb:85:6b
  inet 192.168.50.54 netmask 0xffffff00 broadcast 192.168.50.255
  media: Ethernet autoselect (1000baseT <full-duplex>)
  status: active
```

- **接口名**：`cgem0`（唯一带 MAC/IP 的实体网口）。
- **IP**：`192.168.50.54/24`（即 SSH 入口地址）。
- **链路**：**active**，**1000baseT full-duplex** → 千兆双工。
- **流量**：`netstat -i` 显示 Ipkts 594781 / Opkts 476695，真实在跑数据。

### 端到端连通性

```
ping -c 2 192.168.50.43   # 从板子 ping 回执行命令的宿主
2 packets transmitted, 2 received, 0% loss
round-trip min/avg/max = 1.957 / 1.981 / 2.005 ms
```

### 设备 ↔ 接口映射

启动日志 `PCIe scan 00001de4:00000001`（RP1 域，bus 1）枚举出的网卡：

- vendor ID `0x1DE4` / device ID `0x0001`
- class/subclass/reg `02/00/00` = Ethernet Network Controller
- **即 onboard GbE，对应到接口 `cgem0`**

> 该 ID 不在本地 `pci_devices.h`、也不在默认 `pci-tool` 数据库里 → 显示 `<unknown>`。
> 驱动实际已加载并工作（不在本仓库源码树内，而在 QNX SDP / base net stack）。

---

## 5. 关键地址与寄存器速查

| 名称 | 地址 | 说明 |
|---|---|---|
| GICD (GICv2) | `0x107fff9000` | 中断配置寄存器 ICfR @ `+0xc00` |
| GICC | `0x107fffa000` | CPU 端 GIC |
| RP1 PCIe MSI-X | `0x1f00108000` (size 0x1000) | RP1 域 MSI-X，64 槽 |
| RP1 MIP INT ctrl | `0x1000130000` (size 0xc0) | RP1 域 MSI-X 控制器 |
| 扩展口 MIP INT ctrl | `0x1000131000` (size 0xc0) | 扩展口 MSI-X 控制器 |

扩展口 IRQ：GICv2 向量 `287 ~ 295`（边沿触发）。
RP1 PCIe 访问 BDF：`PCI_BDF(1, 0, 0)`。

---

## 6. 备注 / 坑

- **底层配置访问依赖预编库**：`pci_read_cfg32`/`write_cfg32` 无源码实现，靠 QNX 预编 `libstartup.a`。改 PCI 配置逻辑时注意这条依赖链。
- **SDIO-over-PCI 是死代码**：若未来要启用 SDIO over PCIe，需定义 `SDIO_PCI_SUPPORT` 并确认驱动可用；当前 `pci_devices.h` 也不含相关 ID。
- **此 QNX 构建缺工具**：无 `ip`（iproute2）、`dmesg`、`sudo`；网络查看用 `ifconfig`/`netstat`/`route`，root 下能拿到的额外深度有限。
- **SSH host key 会因重刷变化**：`known_hosts` 冲突时 OpenSSH 会锁定密码登录防 MITM，需先 `ssh-keygen -R <ip>` 再连。
