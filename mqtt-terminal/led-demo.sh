#!/bin/bash

# LED Demo using the new CLI
# This script shows how easy it is to control LEDs with the CLI

DEVICE_ID="313938"
CLI_PATH="./dist/cli.js"

echo "🎭 LED Demo using mqtt-terminal CLI"
echo "=================================="

# Check if CLI exists
if [ ! -f "$CLI_PATH" ]; then
    echo "❌ CLI not found. Run 'npm run build' first."
    exit 1
fi

echo "✅ CLI found at $CLI_PATH"
echo ""

# Function to send command
send_cmd() {
    local cmd="$1"
    local desc="$2"
    echo "💡 $desc: $cmd"
    node "$CLI_PATH" -d "$DEVICE_ID" -c "$cmd" --timeout 3000
    echo ""
}

# Turn all LEDs off first
echo "🔄 Turning all LEDs off..."
for i in {0..3}; do
    send_cmd "app led off $i" "Turn off LED $i"
done

# Demo sequence
echo "🎵 Starting LED demo sequence..."
echo ""

# Wave pattern
echo "🌊 Wave pattern..."
for i in {0..3}; do
    send_cmd "app led on $i" "Turn on LED $i"
    sleep 0.5
    send_cmd "app led off $i" "Turn off LED $i"
done

# All on
echo "✨ All LEDs on..."
for i in {0..3}; do
    send_cmd "app led on $i" "Turn on LED $i"
done
sleep 1

# All off
echo "🌑 All LEDs off..."
for i in {0..3}; do
    send_cmd "app led off $i" "Turn off LED $i"
done

echo ""
echo "🎉 Demo complete! The CLI makes LED control super easy!"
echo ""
echo "💡 Try these commands:"
echo "   node $CLI_PATH -d $DEVICE_ID -c 'app led on 0'"
echo "   node $CLI_PATH -d $DEVICE_ID -c 'app led off 0'"
echo "   node $CLI_PATH -d $DEVICE_ID -c 'device id'"
echo ""
echo "🚀 Or start interactive mode:"
echo "   node $CLI_PATH -d $DEVICE_ID"
