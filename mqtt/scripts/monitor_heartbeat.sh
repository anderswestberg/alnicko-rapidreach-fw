#!/bin/bash
# Monitor RapidReach heartbeat messages

echo "Monitoring RapidReach heartbeat messages..."
echo "Press Ctrl+C to stop"
echo ""

mosquitto_sub -h mosquitto -t 'rapidreach/heartbeat' -v | while read line; do
    timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$timestamp] $line"
done
