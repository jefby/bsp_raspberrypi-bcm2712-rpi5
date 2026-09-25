# Changelog

All notable changes to the RPi5 QNX 8 BSP are documented in this file.

## [v0.0.4] — 2026-09-25

### Fixed (OTA Client)
- `fsync_path`: changed `O_RDONLY` → `O_RDWR` to ensure FAT driver flushes data correctly
- `set_active_ifs`: replaced non-atomic write with **tmp + fsync + rename** pattern; on failure restores from `.bak`
- `try_apply_update`: added `check_boot_space()` — verifies `/var/boot` has enough free space before downloading IFS (+1MB FAT metadata margin)
- Removed `ofstream::file()` call (not supported by QNX libc++); uses `close()` + `fsync_path()` instead

### Fixed (Build / OTA Deploy)
- `auto_local_new.sh`: now generates version as `YYYY.WW.N` (Tesla-style ISO week), uploads IFS + SHA256 sidecar, updates `version.txt` as the final trigger

### Added
- `hwstatusd`: updated prebuilt binary with storage info support and history chart HTTP endpoint
- `build.sh`: full clean build pipeline (QNX SDP → OTA client → IFS → `_B` copy)

---

## [v0.0.3] — 2026-09-17

### Added (OTA Client)
- **SHA256 verification** for downloaded IFS (`openssl/evp.h`, 64KB chunked digest)
- **Pending transaction** mechanism: `ota_pending` file tracks in-flight slot switch; version committed only after cold-start confirmation
- **A/B slot management**: auto-detects active slot from `config.txt`, targets the other for updates
- **Config OTA**: hot-reloads `/etc/ota_config` from server (`/ota_config` endpoint) with safety check (must contain `OTA_SERVER=`)
- **Version format upgrade**: Tesla-style `YYYY.WW.N` with backward-compatible integer fallback
- **Rollback detection**: if new IFS fails to boot, old version is preserved; unused target slot is cleaned up

### Added (Network / WiFi)
- QWDI DHD 2.11 WiFi driver packaged in IFS (optional, not auto-started)
- `start_wifi.sh` helper: start/stop/status/scan with second `io-sock` prefix (`/wifi`)
- `devs-qwdi_dhd_sdio-2_11-rpi5.so` + firmware blobs bundled

### Added (Hardware / Drivers)
- `hwstatusd`: HTTP daemon exposing hardware status on port 8080
- DWC I2C transaction handling fixes
- `uptime` command in prebuilt utilities

### Fixed (OTA Client)
- Version file moved from read-only `/etc/ota_version` → writable `/var/boot/ota_version`
- Read-only `/etc/` handling for config OTA on QNX IFS PFS
- `/var/boot` mount retry logic (5 attempts with backoff)

### Fixed (Build / System)
- `axge` driver recovery and debug support
- DNS resolution fix: ensured `dhcpcd` runs before SSH/OTA in `net_start.sh`
- Shutdown command fix
- Build info generation (`build_info.txt` with git hash + timestamp)

### Added (Docs)
- `docs/devinfo_output_analysis.md`
- OTA flow documentation (`docs/ota_client_flow.md`)
- Driver analysis docs: DEVB, DEVC, I2C, SPI (flowcharts + hardware reference)

---

## [v0.0.2] — 2026-09-10

### Added (OTA Client)
- Initial OTA client implementation (`src/ota/ota_client.cpp`)
- A/B partition strategy: `ifs-rpi5.bin` (A) + `ifs-rpi5_B.bin` (B)
- Background daemon via `procmgr_daemon()` in QNX
- Config file parsing (`/etc/ota_config`: server URL, interval, paths)
- HTTP GET for version check and IFS download (libcurl)

### Added (Build System)
- `build.sh`: full build pipeline with QNX SDP environment sourcing
- `src/ota/Makefile`: qcc C++11 compilation with libcurl + openssl linking
- `images/rpi5.build` integration: OTA client, libcurl, ota_config, ota_version in IFS
- `net_start.sh`: added `ota_client -d` startup after DHCP

### Fixed (Network)
- DNS resolution bug (multiple iterations): ensured `dhcpcd` hook writes `/etc/resolv.conf` before other services start
- Network driver load order fix (`io-sock` before `dhcpcd`)

### Fixed (System)
- Shutdown command issues
- Version file path fix (moved to writable location)
- OTA client crash fixes and edge case handling

### Added (Utilities)
- `fan_control.sh`: temperature-based fan speed control via `/dev/fan`
- `time_sync.sh`: NTP time synchronization loop (ntp.aliyun.com / cloud.tencent.com)
- `start_wifi.sh`: WiFi helper script

### Fixed (Build / Image)
- Missing library dependencies resolved
- IFS build for both A and B slots (`ifs-rpi5.bin` + `ifs-rpi5_B.bin`)

---

## [v0.0.1] — 2026-09-03

### Initial Release

### Added (Core BSP)
- QNX Neutrino 8 IFS for Raspberry Pi 5 (BCM2712 + RP1 SoC)
- `startup-bcm2712-rpi5`: microkernel startup (WDT, GIC, MMU, SMP)
- Full driver stack: PCI server, UART, SPI, SDMMC, USB OTG xHCI, I2C DWC
- GPIO utilities: `gpio-aon-bcm`, `gpio-bcm`, `gpio-rp1`
- Mbox utility for temperature/firmware access
- Fan controller (`fan-rpi5`) with resmgr

### Added (System Services)
- SSH server (key generation on first boot)
- Network stack: `io-sock` + `dhcpcd` + `sshd`
- Storage: devf-ram for `/var/run`, `/var/db`
- Watchdog: `wdtkick` with RPi5-specific registers

### Added (Hardware Support)
- BCM2712 PCIe HW module (`pci_hw-bcm2712-rpi5.so`)
- RP1 MSIX interrupt configuration utility
- I2C driver with DWCC controller
- SPI driver with DWCC controller

### Fixed
- Local build system integration with QNX SDP
- `.gitignore` for build artifacts and prebuilt binaries

---

## Versioning Convention

Tags follow `vMAJOR.MINOR.PATCH`:
- **MAJOR**: breaking changes (new hardware, new IFS layout)
- **MINOR**: new features (new drivers, new services)
- **PATCH**: bug fixes only

OTA server versions use Tesla-style `YYYY.WW.N` format:
```
2026.39.1  → year=2026, ISO-week=39, sequence=1
```
