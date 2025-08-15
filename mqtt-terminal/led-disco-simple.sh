#!/bin/bash

# Simple LED Disco - Uses mqtt-terminal efficiently
DEVICE_ID=313938
HOST=192.168.2.62

# Quick command function with very short timeout
cmd() {
    npm run start -- -d $DEVICE_ID -h $HOST -c "$1" --timeout 500 2>/dev/null >/dev/null &
}

# Turn all LEDs off
all_off() {
    cmd "app led off 0"
    cmd "app led off 1" 
    cmd "app led off 2"
    cmd "app led off 3"
    wait
}

# Cleanup on exit
cleanup() {
    echo -e "\n\n🛑 Turning off all LEDs..."
    all_off
    pkill -f "node.*mqtt-terminal" 2>/dev/null || true
    echo "Disco ended! 🎭"
    exit 0
}

trap cleanup SIGINT

echo "╔═══════════════════════════════════════╗"
echo "║    🎵 SIMPLE LED DISCO 🎵             ║"
echo "║     Press Ctrl+C to stop              ║"
echo "╚═══════════════════════════════════════╝"
echo ""

# Start with all off
all_off
sleep 1

while true; do
    # Wave
    echo -n "🌊 "
    for i in 0 1 2 3; do
        cmd "app led on $i"
        sleep 0.15
    done
    sleep 0.2
    for i in 0 1 2 3; do
        cmd "app led off $i"
        sleep 0.15
    done
    
    # Flash all
    echo -n "💫 "
    cmd "app led on 0"
    cmd "app led on 1"
    cmd "app led on 2"
    cmd "app led on 3"
    sleep 0.3
    all_off
    sleep 0.2
    
    # Alternate pairs
    echo -n "🔄 "
    for j in {1..4}; do
        cmd "app led on 0"
        cmd "app led on 2"
        sleep 0.2
        cmd "app led off 0"
        cmd "app led off 2"
        cmd "app led on 1"
        cmd "app led on 3"
        sleep 0.2
        cmd "app led off 1"
        cmd "app led off 3"
    done
    
    # Random
    echo -n "🎲 "
    for i in {1..10}; do
        led=$((RANDOM % 4))
        if [ $((RANDOM % 2)) -eq 0 ]; then
            cmd "app led on $led"
        else
            cmd "app led off $led"
        fi
        sleep 0.1
    done
    
    echo "" # New line
    sleep 0.5
done
