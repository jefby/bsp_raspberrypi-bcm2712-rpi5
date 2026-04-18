#!/bin/sh

SLEEP_INTERVAL=120 # 120秒检查一次
while true; do
    CUR_TIME=$(date)
    echo "Current time: $CUR_TIME. Synchronizing time with NTP server..."
    ntpdate -u ntp.aliyun.com time1.cloud.tencent.com > /tmp/ntp_sync.log 2>&1
    if [ $? -eq 0 ]; then
        echo "Time synchronization successful."
    else
        echo "Time synchronization failed. Check /tmp/ntp_sync.log for details."
    fi
    
    sleep $SLEEP_INTERVAL
done
