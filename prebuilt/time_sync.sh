#!/bin/sh

SLEEP_INTERVAL=30 # 初始检查时间间隔为30秒
while true; do
    CUR_TIME=$(date)

    if [ $CUR_TIME > '1980-01-01 00:00:00' ]; then
        echo "System time is valid: $CUR_TIME" >> /tmp/time_sync.log
        SLEEP_INTERVAL=3600 # 时间有效，将检查间隔调整为1小时
        echo "Next time check in 1 hour." >> /tmp/time_sync.log
    else
        echo "System time is invalid: $CUR_TIME. Synchronizing time with NTP server..."
        ntpdate -u ntp.aliyun.com time1.cloud.tencent.com > /tmp/ntp_sync.log 2>&1
        if [ $? -eq 0 ]; then
            echo "Time synchronization successful."   >> /tmp/ntp_sync.log 
        else
            echo "Time synchronization failed. Check /tmp/ntp_sync.log for details."  >> /tmp/ntp_sync.log
        fi
    fi
    
    sleep $SLEEP_INTERVAL
done
