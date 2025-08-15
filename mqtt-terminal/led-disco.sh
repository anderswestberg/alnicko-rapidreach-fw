#!/bin/bash

# LED Disco Show - A fun light pattern generator!
# Uses the 4 LEDs to create musical patterns

DEVICE_ID=313938

# Quick LED control
led() {
    npm run start -- -d $DEVICE_ID -c "app led $2 $1" 2>/dev/null >/dev/null &
}

# Turn all LEDs off
all_off() {
    led 0 off
    led 1 off
    led 2 off
    led 3 off
    wait
}

# Patterns for our disco show
pattern_wave() {
    echo "🌊 Wave pattern..."
    for i in 0 1 2 3; do
        led $i on
        sleep 0.1
    done
    for i in 0 1 2 3; do
        led $i off
        sleep 0.1
    done
}

pattern_bounce() {
    echo "⚡ Bounce pattern..."
    for i in 0 1 2 3 2 1; do
        led $i on
        sleep 0.08
        led $i off
    done
}

pattern_flash() {
    echo "💫 Flash pattern..."
    # All on
    led 0 on
    led 1 on
    led 2 on
    led 3 on
    sleep 0.2
    all_off
    sleep 0.1
}

pattern_alternate() {
    echo "🔄 Alternate pattern..."
    # Even LEDs
    led 0 on
    led 2 on
    sleep 0.15
    led 0 off
    led 2 off
    # Odd LEDs
    led 1 on
    led 3 on
    sleep 0.15
    led 1 off
    led 3 off
}

pattern_random() {
    echo "🎲 Random pattern..."
    for i in {1..8}; do
        local led_num=$((RANDOM % 4))
        local state=$((RANDOM % 2))
        if [ $state -eq 1 ]; then
            led $led_num on
        else
            led $led_num off
        fi
        sleep 0.05
    done
}

pattern_build() {
    echo "📈 Build pattern..."
    # Build up
    led 0 on
    sleep 0.1
    led 1 on
    sleep 0.1
    led 2 on
    sleep 0.1
    led 3 on
    sleep 0.3
    # Flash
    all_off
    sleep 0.05
    led 0 on
    led 1 on
    led 2 on
    led 3 on
    sleep 0.05
    all_off
}

pattern_heartbeat() {
    echo "❤️  Heartbeat pattern..."
    # Double beat
    led 0 on
    led 3 on
    sleep 0.1
    all_off
    sleep 0.05
    led 0 on
    led 3 on
    sleep 0.1
    all_off
    sleep 0.3
}

# Cleanup function
cleanup() {
    echo -e "\n\n🛑 Stopping LED Disco Show..."
    all_off
    echo "Thanks for the party! 🎉"
    exit 0
}

# Intro
intro() {
    clear
    echo "╔═══════════════════════════════════════╗"
    echo "║      🎵 LED DISCO SHOW 🎵             ║"
    echo "║   Press Ctrl+C to stop the party!     ║"
    echo "╚═══════════════════════════════════════╝"
    echo ""
    
    # Cool startup sequence
    echo "🚀 Starting up..."
    pattern_wave
    pattern_flash
    sleep 0.5
    all_off
    echo ""
}

# Main disco loop
main_show() {
    local patterns=(
        pattern_wave
        pattern_bounce
        pattern_alternate
        pattern_flash
        pattern_heartbeat
        pattern_build
        pattern_random
        pattern_wave
        pattern_bounce
        pattern_alternate
    )
    
    # Play through patterns
    for pattern in "${patterns[@]}"; do
        $pattern
        sleep 0.2
    done
    
    # Epic finale
    echo "🎆 FINALE!"
    for j in {1..3}; do
        pattern_flash
    done
    pattern_wave
    pattern_bounce
    pattern_build
    
    echo -e "\n🔄 Restarting show...\n"
    sleep 1
}

# Set trap for clean exit
trap cleanup SIGINT

# Start the show!
intro

# Run forever
while true; do
    main_show
done
