#!/bin/bash
# Monitor device connection and track uptime

DEVICE_ID="313938"
CHECK_INTERVAL=10
CONNECTED=false
CONNECT_TIME=""

echo "🔍 Monitoring for device $DEVICE_ID connection..."
echo "Press Ctrl+C to stop"
echo ""

while true; do
    # Check if device is connected
    if kubectl exec -n rapidreach $(kubectl get pods -n rapidreach | grep emqx | awk '{print $1}') -- \
       /opt/emqx/bin/emqx_ctl clients list 2>/dev/null | grep -q "${DEVICE_ID}.*connected=true"; then
        
        if [ "$CONNECTED" = false ]; then
            CONNECT_TIME=$(date)
            echo "✅ [$(date '+%H:%M:%S')] Device $DEVICE_ID CONNECTED!"
            echo "   Connection established at: $CONNECT_TIME"
            CONNECTED=true
        else
            # Calculate uptime
            UPTIME_SECONDS=$(($(date +%s) - $(date -d "$CONNECT_TIME" +%s)))
            UPTIME_MINUTES=$((UPTIME_SECONDS / 60))
            UPTIME_REMAINDER=$((UPTIME_SECONDS % 60))
            
            echo -ne "\r⏱️  [$(date '+%H:%M:%S')] Device connected for: ${UPTIME_MINUTES}m ${UPTIME_REMAINDER}s "
            
            # Alert if connection survives past 90 seconds
            if [ $UPTIME_SECONDS -eq 91 ]; then
                echo -e "\n🎉 SUCCESS! Device stayed connected past 90 seconds! The keepalive is working!"
            fi
        fi
    else
        if [ "$CONNECTED" = true ]; then
            echo -e "\n❌ [$(date '+%H:%M:%S')] Device $DEVICE_ID DISCONNECTED"
            CONNECTED=false
        else
            echo -ne "\r⏳ [$(date '+%H:%M:%S')] Waiting for device connection... "
        fi
    fi
    
    sleep $CHECK_INTERVAL
done
