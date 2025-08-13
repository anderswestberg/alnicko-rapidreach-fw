#!/bin/bash

# Test MQTT CLI Bridge

echo "Testing MQTT CLI Bridge on RapidReach device..."

# Configure serial port
stty -F /dev/ttyACM0 115200 raw -echo

# Function to send command and wait for response
send_cmd() {
    echo -e "$1\r" > /dev/ttyACM0
    sleep 1
}

# Read responses in background
(while true; do cat /dev/ttyACM0; done) &
CAT_PID=$!

# Wait a bit for serial to stabilize
sleep 2

echo "1. Checking MQTT status..."
send_cmd "rapidreach mqtt status"
sleep 2

echo -e "\n2. Initializing MQTT..."
send_cmd "rapidreach mqtt init"
sleep 2

echo -e "\n3. Connecting to MQTT broker..."
send_cmd "rapidreach mqtt connect"
sleep 3

echo -e "\n4. Checking MQTT status again..."
send_cmd "rapidreach mqtt status"
sleep 2

echo -e "\n5. Enabling MQTT CLI bridge..."
send_cmd "rapidreach mqtt cli enable"
sleep 2

echo -e "\n6. Checking CLI bridge status..."
send_cmd "rapidreach mqtt cli status"
sleep 2

echo -e "\n7. Starting heartbeat..."
send_cmd "rapidreach mqtt heartbeat start"
sleep 2

# Kill the background cat process
kill $CAT_PID 2>/dev/null

echo -e "\nDevice setup complete. You can now test MQTT commands."
