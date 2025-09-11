#!/bin/bash

echo "=== Monitoring Device Debug Logs ==="
echo ""
echo "Looking for:"
echo "- DEBUG: DEVICE_REG_COMPLETE"
echo "- DEBUG: MQTT_INIT_START reached"
echo "- DEBUG: Sending EVENT_RETRY"
echo "- DEBUG: Calling mqtt_init()"
echo ""
echo "Starting monitor..."
echo ""

# Monitor device server logs for any activity
kubectl logs -n rapidreach deployment/device-server -f | while read line; do
    echo "$line"
    # Highlight debug messages
    if [[ "$line" == *"DEBUG:"* ]]; then
        echo ">>> FOUND DEBUG: $line"
    fi
done
