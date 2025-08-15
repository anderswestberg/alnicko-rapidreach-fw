#!/bin/bash

# LED Disco Show - Optimized version with persistent MQTT connection
# Uses a single Node.js script to maintain connection

DEVICE_ID=313938

# Create a temporary Node.js script for persistent connection
cat > /tmp/led-controller.js << 'EOF'
const mqtt = require('mqtt');
const readline = require('readline');

const deviceId = process.argv[2] || '313938';
const brokerUrl = 'mqtt://192.168.2.62:1883';

const client = mqtt.connect(brokerUrl, {
    username: 'admin',
    password: 'public'
});

const commandTopic = `${deviceId}_rx`;
const responseTopic = `${deviceId}_tx`;

let commandQueue = [];
let currentCommand = null;

client.on('connect', () => {
    console.log('Connected to MQTT broker');
    client.subscribe(responseTopic);
    processQueue();
});

client.on('message', (topic, message) => {
    if (currentCommand) {
        currentCommand = null;
        processQueue();
    }
});

function sendCommand(cmd) {
    commandQueue.push(cmd);
    if (!currentCommand) {
        processQueue();
    }
}

function processQueue() {
    if (commandQueue.length > 0 && !currentCommand) {
        currentCommand = commandQueue.shift();
        client.publish(commandTopic, currentCommand);
    }
}

const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
});

rl.on('line', (input) => {
    sendCommand(input);
});

process.on('SIGINT', () => {
    console.log('\nDisconnecting...');
    client.end();
    process.exit(0);
});
EOF

# Start the Node.js LED controller
echo "🎉 Starting LED Disco Controller..."
node /tmp/led-controller.js $DEVICE_ID &
CONTROLLER_PID=$!

# Give it time to connect
sleep 2

# Function to send LED commands
led_cmd() {
    echo "$1" > /proc/$CONTROLLER_PID/fd/0 2>/dev/null || true
}

# Turn all LEDs off
all_off() {
    led_cmd "app led off 0"
    led_cmd "app led off 1"
    led_cmd "app led off 2"
    led_cmd "app led off 3"
}

# Cleanup function
cleanup() {
    echo -e "\n\n🛑 Stopping LED Disco Show..."
    all_off
    sleep 0.5
    kill $CONTROLLER_PID 2>/dev/null
    rm -f /tmp/led-controller.js
    echo "Thanks for the party! 🎉"
    exit 0
}

trap cleanup SIGINT

# Intro
clear
echo "╔═══════════════════════════════════════╗"
echo "║      🎵 LED DISCO SHOW 🎵             ║"
echo "║   Press Ctrl+C to stop the party!     ║"
echo "╚═══════════════════════════════════════╝"
echo ""

# Initialize - all off
all_off
sleep 1

# Main disco loop
while true; do
    echo "🌊 Wave pattern..."
    for i in 0 1 2 3; do
        led_cmd "app led on $i"
        sleep 0.1
    done
    for i in 0 1 2 3; do
        led_cmd "app led off $i"
        sleep 0.1
    done
    
    echo "⚡ Bounce pattern..."
    for i in 0 1 2 3 2 1; do
        led_cmd "app led on $i"
        sleep 0.08
        led_cmd "app led off $i"
    done
    
    echo "💫 Flash pattern..."
    led_cmd "app led on 0"
    led_cmd "app led on 1"
    led_cmd "app led on 2"
    led_cmd "app led on 3"
    sleep 0.2
    all_off
    sleep 0.1
    
    echo "🔄 Alternate pattern..."
    led_cmd "app led on 0"
    led_cmd "app led on 2"
    sleep 0.15
    led_cmd "app led off 0"
    led_cmd "app led off 2"
    led_cmd "app led on 1"
    led_cmd "app led on 3"
    sleep 0.15
    led_cmd "app led off 1"
    led_cmd "app led off 3"
    
    echo "🎲 Random pattern..."
    for i in {1..8}; do
        led_num=$((RANDOM % 4))
        state=$((RANDOM % 2))
        if [ $state -eq 1 ]; then
            led_cmd "app led on $led_num"
        else
            led_cmd "app led off $led_num"
        fi
        sleep 0.05
    done
    
    echo "📈 Build pattern..."
    led_cmd "app led on 0"
    sleep 0.1
    led_cmd "app led on 1"
    sleep 0.1
    led_cmd "app led on 2"
    sleep 0.1
    led_cmd "app led on 3"
    sleep 0.3
    all_off
    sleep 0.05
    led_cmd "app led on 0"
    led_cmd "app led on 1"
    led_cmd "app led on 2"
    led_cmd "app led on 3"
    sleep 0.05
    all_off
    
    echo "❤️  Heartbeat pattern..."
    led_cmd "app led on 0"
    led_cmd "app led on 3"
    sleep 0.1
    all_off
    sleep 0.05
    led_cmd "app led on 0"
    led_cmd "app led on 3"
    sleep 0.1
    all_off
    sleep 0.3
    
    echo -e "\n🔄 Next cycle...\n"
    sleep 0.5
done
