#!/bin/bash
# Monitor for device connection and send keepalive commands

MQTT_HOST="192.168.2.79"
DEVICE_ID="313938"

echo "🔍 Monitoring for device $DEVICE_ID connection to $MQTT_HOST..."
echo "Will send 'help' command when device connects to prevent timeout"

# First, check current connections
kubectl exec -n rapidreach $(kubectl get pods -n rapidreach | grep emqx | awk '{print $1}') -- /opt/emqx/bin/emqx_ctl clients list 2>/dev/null | grep "$DEVICE_ID"

# Monitor in a loop
while true; do
    # Check if device is connected
    if kubectl exec -n rapidreach $(kubectl get pods -n rapidreach | grep emqx | awk '{print $1}') -- /opt/emqx/bin/emqx_ctl clients list 2>/dev/null | grep -q "$DEVICE_ID.*connected=true"; then
        echo "✅ Device connected! Sending keepalive command..."
        
        # Send command via mqtt-terminal
        cd /home/rapidreach/work/alnicko-rapidreach-fw/mqtt-terminal
        npm run start -- -d $DEVICE_ID -h $MQTT_HOST -c "help" 2>&1 | grep -v "Failed to send logs"
        
        echo "📨 Command sent. Waiting 30 seconds before next check..."
        sleep 30
    else
        echo "⏳ Device not connected. Checking again in 5 seconds..."
        sleep 5
    fi
done
