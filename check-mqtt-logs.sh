#!/bin/bash

SERIAL_PORT="/dev/ttyACM0"
LOG_FILE="/tmp/device-serial-$(date +%Y%m%d-%H%M%S).log"

echo "=== RapidReach Device Serial Monitor ==="
echo "Serial Port: $SERIAL_PORT"
echo "Log File: $LOG_FILE"
echo ""
echo "Configuring serial port..."

# Configure serial port
stty -F $SERIAL_PORT 115200 cs8 -cstopb -parenb raw -echo

echo "Capturing current output for 3 seconds..."
timeout 3 cat $SERIAL_PORT | tee -a $LOG_FILE

echo ""
echo "Sending kernel reboot command via shell..."
echo ""

# Send reboot command
echo "kernel reboot cold" > $SERIAL_PORT
sleep 0.5

echo "Capturing reboot logs (30 seconds)..."
echo "Press Ctrl+C to stop..."
echo ""

# Capture boot logs
timeout 30 cat $SERIAL_PORT | tee -a $LOG_FILE | grep --line-buffered -E "(Booting|mqtt|MQTT|connect|broker|init_sm|ERROR|WARNING|Failed)" --color=always

echo ""
echo "Log saved to: $LOG_FILE"
echo ""
echo "To see full log: cat $LOG_FILE"
echo "To see MQTT lines: grep -i mqtt $LOG_FILE"

