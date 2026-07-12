#!/bin/sh
#
# start_wifi.sh — RPi5 QNX8 第二套 io-sock + QWDI WiFi 启动脚本
#
# 【默认不随系统启动】本脚本仅打包进镜像，需 root 手动执行：
#     start_wifi.sh
# 不要在 rpi5.build 的 [script] 里默认调用（除非你明确要开机连 WiFi）。
#
# 依赖（IFS 中应已打包）：
#   /lib/dll/devs-qwdi_dhd_sdio-2_11-rpi5.so
#   /etc/io-sock.conf
#   /etc/qwdi_wifi.conf
#   /etc/firmware/{firmware.bin,nvram.txt,firmware.clm_blob}
#   wpa_supplicant / dhcpcd / ifconfig
#
# 用法：
#   start_wifi.sh              # 启动栈+驱动+关联+DHCP
#   start_wifi.sh stop         # 停止 wpa / 第二套 io-sock（尽量不影响默认栈）
#   start_wifi.sh status       # 查看状态
#   start_wifi.sh scan         # 扫描 AP（需已 start）
#
# 环境变量（可选覆盖）：
#   WIFI_PREFIX=/wifi
#   WIFI_DRIVER=qwdi_dhd_sdio-2_11-rpi5
#   WIFI_IO_SOCK_CONF=/etc/io-sock.conf
#   WIFI_WPA_CONF=/etc/wpa_supplicant.wifi.conf
#   WIFI_IFACE=bcm0
#   WIFI_WAIT_SEC=30
#

export PATH=/proc/boot:/sbin:/bin:/usr/bin:/usr/sbin:${PATH}
export LD_LIBRARY_PATH=/proc/boot:/lib:/usr/lib:/lib/dll:/lib/dll/pci:${LD_LIBRARY_PATH}

WIFI_PREFIX=${WIFI_PREFIX:-/wifi}
WIFI_DRIVER=${WIFI_DRIVER:-qwdi_dhd_sdio-2_11-rpi5}
WIFI_IO_SOCK_CONF=${WIFI_IO_SOCK_CONF:-/etc/io-sock.conf}
WIFI_WPA_CONF=${WIFI_WPA_CONF:-/etc/wpa_supplicant.wifi.conf}
WIFI_IFACE=${WIFI_IFACE:-bcm0}
WIFI_WAIT_SEC=${WIFI_WAIT_SEC:-30}
WIFI_SOCK_NODE=${WIFI_PREFIX}/dev/socket

log() {
	echo "start_wifi: $*"
}

die() {
	echo "start_wifi: ERROR: $*" >&2
	exit 1
}

need_root() {
	# QNX: id -u 若可用则检查；否则尝试写 socket 目录
	if command -v id >/dev/null 2>&1; then
		[ "$(id -u)" = "0" ] || die "请用 root 运行"
	fi
}

export_sock() {
	export SOCK=${WIFI_PREFIX}
}

stack_up() {
	[ -e "${WIFI_SOCK_NODE}" ]
}

wait_for_node() {
	_node=$1
	_sec=${2:-${WIFI_WAIT_SEC}}
	_i=0
	while [ "${_i}" -lt "${_sec}" ]; do
		if [ -e "${_node}" ]; then
			return 0
		fi
		sleep 1
		_i=$((_i + 1))
	done
	return 1
}

start_iosock() {
	if stack_up; then
		log "io-sock 已存在 (${WIFI_SOCK_NODE})，跳过启动"
		return 0
	fi

	[ -f "${WIFI_IO_SOCK_CONF}" ] || die "缺少 ${WIFI_IO_SOCK_CONF}"
	[ -f "/lib/dll/devs-${WIFI_DRIVER}.so" ] || \
		[ -f "/lib/dll/${WIFI_DRIVER}.so" ] || \
		log "警告: 未在 /lib/dll 找到 devs-${WIFI_DRIVER}.so（依赖 LD_LIBRARY_PATH）"

	log "启动 io-sock prefix=${WIFI_PREFIX} driver=${WIFI_DRIVER}"
	# 后台启动第二套栈
	io-sock -o "prefix=${WIFI_PREFIX}" -o "config=${WIFI_IO_SOCK_CONF}" \
		-d "${WIFI_DRIVER}" &
	_iosock_pid=$!
	log "io-sock pid=${_iosock_pid}"

	if ! wait_for_node "${WIFI_SOCK_NODE}" "${WIFI_WAIT_SEC}"; then
		die "等待 ${WIFI_SOCK_NODE} 超时 (${WIFI_WAIT_SEC}s)，请查 slog2info"
	fi
	log "栈就绪: ${WIFI_SOCK_NODE}"

	# 等接口节点 / 驱动登记
	_i=0
	while [ "${_i}" -lt "${WIFI_WAIT_SEC}" ]; do
		if SOCK=${WIFI_PREFIX} ifconfig "${WIFI_IFACE}" >/dev/null 2>&1; then
			log "接口 ${WIFI_IFACE} 已出现"
			return 0
		fi
		sleep 1
		_i=$((_i + 1))
	done
	log "警告: ${WIFI_WAIT_SEC}s 内未见 ${WIFI_IFACE}，继续尝试 wpa（请 ifconfig -a 核对接口名）"
}

ensure_wpa_conf() {
	if [ -f "${WIFI_WPA_CONF}" ]; then
		return 0
	fi
	# Prefer example shipped in IFS (no real secrets in git / script).
	if [ -f /etc/wpa_supplicant.wifi.conf.example ]; then
		log "未找到 ${WIFI_WPA_CONF}，从 example 复制（请改 SSID/psk 后再连网）"
		cp /etc/wpa_supplicant.wifi.conf.example "${WIFI_WPA_CONF}"
		return 0
	fi
	log "未找到 ${WIFI_WPA_CONF}，写入占位模板（请改 SSID/psk）"
	mkdir -p "$(dirname "${WIFI_WPA_CONF}")" 2>/dev/null
	cat > "${WIFI_WPA_CONF}" << 'WPAEOF'
ctrl_interface=/var/run/wpa_supplicant
update_config=1
ap_scan=1

# WPA2-Personal. Fill with: wpa_passphrase "SSID" "password"
network={
	ssid="YourSSID"
	psk=0000000000000000000000000000000000000000000000000000000000000000
	key_mgmt=WPA-PSK
	proto=RSN
	pairwise=CCMP
	group=CCMP
}
WPAEOF
}

start_wpa() {
	export_sock
	mkdir -p /var/run/wpa_supplicant

	# 已有 wpa 则先停掉（本脚本场景只跑一个）
	if pidin ar 2>/dev/null | grep -q '[w]pa_supplicant'; then
		log "停止已有 wpa_supplicant"
		slay wpa_supplicant 2>/dev/null
		sleep 1
	fi

	ensure_wpa_conf
	[ -f "${WIFI_WPA_CONF}" ] || die "缺少 wpa 配置 ${WIFI_WPA_CONF}"

	log "启动 wpa_supplicant -D qwdi -i ${WIFI_IFACE} -c ${WIFI_WPA_CONF}"
	wpa_supplicant -D qwdi -i "${WIFI_IFACE}" -c "${WIFI_WPA_CONF}" -B || \
		die "wpa_supplicant 启动失败"

	# 等待关联
	_i=0
	while [ "${_i}" -lt "${WIFI_WAIT_SEC}" ]; do
		_st=$(wpa_cli -i "${WIFI_IFACE}" status 2>/dev/null | grep '^wpa_state=' | cut -d= -f2)
		if [ "${_st}" = "COMPLETED" ]; then
			log "WPA 关联成功 (COMPLETED)"
			wpa_cli -i "${WIFI_IFACE}" status 2>/dev/null | grep -E '^(ssid|bssid|key_mgmt|freq|wpa_state)='
			return 0
		fi
		sleep 1
		_i=$((_i + 1))
	done
	log "警告: ${WIFI_WAIT_SEC}s 内未 COMPLETED（当前: ${_st:-unknown}），仍尝试 DHCP"
	wpa_cli -i "${WIFI_IFACE}" status 2>/dev/null || true
}

start_dhcp() {
	export_sock
	log "启动 dhcpcd on ${WIFI_IFACE}"
	# -b 后台；部分版本支持接口参数
	if dhcpcd -bqq "${WIFI_IFACE}" 2>/dev/null; then
		:
	else
		dhcpcd -bqq || log "警告: dhcpcd 返回非零"
	fi
	sleep 2
	ifconfig "${WIFI_IFACE}" 2>/dev/null || true
	netstat -rn 2>/dev/null | head -20 || true
}

do_start() {
	need_root
	start_iosock
	export_sock
	start_wpa
	start_dhcp
	# 可选 DNS（若镜像 resolv 异常可取消注释）
	# cat > /etc/resolv.conf << EOF
	# nameserver 223.5.5.5
	# nameserver 8.8.8.8
	# nameserver 192.168.50.1
	# EOF
	log "完成。后续命令请先: export SOCK=${WIFI_PREFIX}"
	log "测试: SOCK=${WIFI_PREFIX} ping -c 3 8.8.8.8"
}

do_stop() {
	need_root
	export_sock
	log "停止 wpa_supplicant"
	slay wpa_supplicant 2>/dev/null
	# 第二套 io-sock：按进程命令行匹配 prefix（避免误杀默认栈）
	if command -v pidin >/dev/null 2>&1; then
		pidin ar 2>/dev/null | grep 'io-sock' | grep "prefix=${WIFI_PREFIX}" | while read -r line; do
			_pid=$(echo "${line}" | awk '{print $1}')
			[ -n "${_pid}" ] || continue
			log "停止 io-sock pid=${_pid}"
			kill "${_pid}" 2>/dev/null || slay "${_pid}" 2>/dev/null
		done
	fi
	log "stop 完成"
}

do_status() {
	export_sock
	echo "=== SOCK=${SOCK} ==="
	echo "=== socket node ==="
	ls -la "${WIFI_SOCK_NODE}" 2>&1 || true
	echo "=== ifconfig ${WIFI_IFACE} ==="
	ifconfig "${WIFI_IFACE}" 2>&1 || true
	echo "=== wpa status ==="
	wpa_cli -i "${WIFI_IFACE}" status 2>&1 || true
	echo "=== routes ==="
	netstat -rn 2>&1 | head -25 || true
}

do_scan() {
	export_sock
	stack_up || die "栈未启动，请先 start_wifi.sh"
	pidin ar 2>/dev/null | grep -q '[w]pa_supplicant' || die "wpa_supplicant 未运行"
	wpa_cli -i "${WIFI_IFACE}" scan
	sleep 3
	wpa_cli -i "${WIFI_IFACE}" scan_results
}

cmd=${1:-start}
case "${cmd}" in
	start|"") do_start ;;
	stop)     do_stop ;;
	status)   do_status ;;
	scan)     do_scan ;;
	*)
		echo "用法: $0 {start|stop|status|scan}" >&2
		exit 2
		;;
esac
