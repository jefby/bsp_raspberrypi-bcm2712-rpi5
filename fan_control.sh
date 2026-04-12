#!/bin/sh
# /sbin/fan_control.sh

export PATH=$PATH:/tmp
# 配置参数
TEMP_LOW=45000      # 45°C以下关闭风扇
TEMP_MID=55000      # 55°C低速
TEMP_HIGH=65000     # 65°C中速
TEMP_MAX=75000      # 75°C全速

SPEED_OFF=0
SPEED_LOW=30
SPEED_MID=60
SPEED_HIGH=100

POLL_INTERVAL=5     # 5秒检查一次

# GPIO配置（树莓派5官方风扇接口）
FAN_GPIO=12

# BCM2712 PWM寄存器地址
GPIO_BASE=0x1F000D0000
PWM_BASE=0x1F000D0000
PWM_OFFSET=0x30000

# 使用devmem访问寄存器的函数
write_reg() {
    local addr=$1
    local val=$2
    devmem $addr  $val 2>/dev/null
}

read_reg() {
    local addr=$1
    devmem $addr  2>/dev/null
}

# 计算地址（十六进制）
calc_addr() {
    printf "0x%X" $(($1 + $2))
}

# 设置风扇速度
set_fan_speed() {
    local speed=$1
    local pwm_value
    
    # 限制范围
    [ $speed -lt 0 ] && speed=0
    [ $speed -gt 100 ] && speed=100
    
    # 计算PWM值 (0-255)
    pwm_value=$((speed * 255 / 100))
    
    # 写入 /dev/fan
    echo $pwm_value > /dev/fan 2>/dev/null || echo "Error: Failed to set fan speed"
    
    echo "Fan speed set to ${speed}% (pwm value: $pwm_value)"
}

# 根据温度计算风扇速度
calc_fan_speed() {
    local temp=$1
    
    if [ $temp -lt $TEMP_LOW ]; then
        echo $SPEED_OFF
    elif [ $temp -lt $TEMP_MID ]; then
        echo $SPEED_LOW
    elif [ $temp -lt $TEMP_HIGH ]; then
        echo $SPEED_MID
    else
        echo $SPEED_HIGH
    fi
}

# 读取温度
read_temperature() {
    local temp
    temp=$(mbox-bcm temperature 2>/dev/null | awk '{print $2}')
    echo "$temp"
}

# 清理退出
cleanup() {
    echo "Shutting down fan controller..."
    set_fan_speed 0
    exit 0
}

# 捕获退出信号
trap cleanup INT TERM

# 主程序
echo "=== BCM2712 Fan Controller ==="
# 重定向输出到日志文件
exec >> /tmp/fan.log 2>&1

echo "Thresholds: OFF<45°C, LOW<55°C, MID<65°C, HIGH>=65°C"

# 等待 /dev/fan 设备就绪
echo "Waiting for /dev/fan device..."
while ! [ -e /dev/fan ]; do
    sleep 1
done
echo "/dev/fan device is ready"

current_speed=-1

# 主循环
while true; do
    # 读取温度
    temp=$(read_temperature)
    
    if [ -z "$temp" ]; then
        echo "Error: Failed to read temperature"
        sleep $POLL_INTERVAL
        continue
    fi
    
    # 计算目标速度
    target_speed=$(calc_fan_speed $temp)
    
    # 只在速度改变时调整
    if [ "$target_speed" != "$current_speed" ]; then
        set_fan_speed $target_speed
        current_speed=$target_speed
        
        # 转换温度显示
        temp_c=$((temp / 1000))
        temp_frac=$(( (temp % 1000) / 100 ))
        echo "$(date '+%Y-%m-%d %H:%M:%S') - Temp: ${temp_c}.${temp_frac}°C -> Fan: ${target_speed}%"
    fi
    
    sleep $POLL_INTERVAL
done