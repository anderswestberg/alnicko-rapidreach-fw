#!/bin/bash
# Monitor all MQTT traffic on EMQX broker
# Usage: ./monitor-mqtt-traffic.sh [topic]

BROKER="192.168.2.79"
PORT="1883"
USERNAME="admin"
PASSWORD="Strix1867"
TOPIC="${1:-#}"  # Default to all topics (#)

echo "================================================"
echo "   MQTT Traffic Monitor - RapidReach"
echo "================================================"
echo "Broker: $BROKER:$PORT"
echo "Topic:  $TOPIC"
echo "Press Ctrl+C to stop"
echo "================================================"
echo ""

# Check if mosquitto_sub is installed
if ! command -v mosquitto_sub &> /dev/null; then
    echo "Error: mosquitto_sub not found"
    echo "Install with: sudo apt-get install mosquitto-clients"
    exit 1
fi

# Subscribe and format output with timestamps
mosquitto_sub -h $BROKER -p $PORT -u $USERNAME -P $PASSWORD -t "$TOPIC" -v | while read line; do
    timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$timestamp] $line"
done

