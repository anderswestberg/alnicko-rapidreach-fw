#!/bin/bash

# Kill any existing processes using the serial port
pkill -f "cat /dev/ttyACM0" 2>/dev/null

# Function to send command and wait for response
send_cmd() {
    echo "Sending: $1"
    echo -e "$1" > /dev/ttyACM0
    sleep 1
}

# Start monitoring in background
(cat /dev/ttyACM0 | tee device-output.log) &
CAT_PID=$!

sleep 1

# Send commands
send_cmd "net iface"
send_cmd "net dhcpv4" 
send_cmd "mqtt status"
send_cmd "log enable dbg shell_mqtt"
send_cmd "log enable dbg net_mqtt"
send_cmd "kernel uptime"

# Wait a bit more
sleep 3

# Kill the background cat
kill $CAT_PID 2>/dev/null

echo "Device output saved to device-output.log"
