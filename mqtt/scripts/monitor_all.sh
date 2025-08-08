#!/bin/bash
# Monitor all RapidReach MQTT traffic

echo "Monitoring ALL RapidReach MQTT traffic..."
echo "Press Ctrl+C to stop"
echo ""

mosquitto_sub -h mosquitto -t 'rapidreach/#' -v | while read line; do
    timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$timestamp] $line"
done
