#!/bin/bash

# LED Music Show - Jingle Bells!
# LEDs: 0=Red, 1=Green, 2=Blue, 3=Yellow (assumed)

DEVICE_ID=313938

# Helper function to control LEDs
led() {
    local led_num=$1
    local state=$2
    npm run start -- -d $DEVICE_ID -c "app led $state $led_num" 2>/dev/null >/dev/null
}

# All LEDs off
all_off() {
    for i in 0 1 2 3; do
        led $i off
    done
}

# Flash all LEDs
flash_all() {
    local duration=$1
    for i in 0 1 2 3; do
        led $i on
    done
    sleep $duration
    all_off
}

# Pattern: cycle through LEDs
cycle_leds() {
    local duration=$1
    for i in 0 1 2 3; do
        led $i on
        sleep $duration
        led $i off
    done
}

# Pattern: alternating pairs
alternate_pairs() {
    local duration=$1
    led 0 on
    led 2 on
    sleep $duration
    led 0 off
    led 2 off
    led 1 on
    led 3 on
    sleep $duration
    led 1 off
    led 3 off
}

# Musical note patterns (simplified Jingle Bells rhythm)
# Using different LED patterns for different notes
play_note() {
    local note=$1
    local duration=$2
    
    case $note in
        "E")  # High note - all LEDs
            flash_all $duration
            ;;
        "D")  # Mid-high - LEDs 1,2,3
            led 1 on; led 2 on; led 3 on
            sleep $duration
            led 1 off; led 2 off; led 3 off
            ;;
        "C")  # Mid - LEDs 0,1
            led 0 on; led 1 on
            sleep $duration
            led 0 off; led 1 off
            ;;
        "G")  # Low - single LED cycling
            cycle_leds $(echo "$duration/4" | bc -l)
            ;;
        "REST")  # Pause
            sleep $duration
            ;;
        "CHORD")  # Chord effect
            alternate_pairs $(echo "$duration/2" | bc -l)
            ;;
    esac
}

# Intro animation
intro() {
    echo "🎵 LED Music Show - Jingle Bells! 🎵"
    echo "Press Ctrl+C to stop"
    echo ""
    
    # Startup sequence
    for i in 0 1 2 3; do
        led $i on
        sleep 0.1
    done
    sleep 0.5
    all_off
    sleep 0.5
}

# Main music loop - Jingle Bells
play_jingle_bells() {
    # Jingle bells, jingle bells
    play_note "E" 0.2
    play_note "E" 0.2
    play_note "E" 0.4
    play_note "REST" 0.1
    
    play_note "E" 0.2
    play_note "E" 0.2
    play_note "E" 0.4
    play_note "REST" 0.1
    
    # Jingle all the way
    play_note "E" 0.2
    play_note "G" 0.2
    play_note "C" 0.2
    play_note "D" 0.2
    play_note "E" 0.6
    play_note "REST" 0.2
    
    # Oh what fun it is to ride
    play_note "CHORD" 0.3
    play_note "CHORD" 0.3
    play_note "CHORD" 0.3
    play_note "CHORD" 0.3
    
    # In a one-horse open sleigh
    play_note "D" 0.2
    play_note "D" 0.2
    play_note "D" 0.2
    play_note "D" 0.2
    
    play_note "D" 0.2
    play_note "E" 0.2
    play_note "D" 0.2
    play_note "REST" 0.1
    
    play_note "G" 0.2
    play_note "C" 0.4
    play_note "REST" 0.2
    
    # Grand finale
    flash_all 0.1
    flash_all 0.1
    flash_all 0.1
    cycle_leds 0.05
    cycle_leds 0.05
    flash_all 0.5
    
    sleep 1
}

# Cleanup on exit
cleanup() {
    echo -e "\n\n🛑 Stopping LED Music Show..."
    all_off
    echo "Thanks for watching! 🎭"
    exit 0
}

# Set up trap for Ctrl+C
trap cleanup SIGINT

# Main program
intro

# Run forever
while true; do
    play_jingle_bells
    
    # Intermission pattern
    echo "🎵 Playing Jingle Bells... 🎵"
    alternate_pairs 0.2
    alternate_pairs 0.2
    alternate_pairs 0.2
    sleep 0.5
done
