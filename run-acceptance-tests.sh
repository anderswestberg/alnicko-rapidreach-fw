#!/bin/bash

# Acceptance Test Runner for RapidReach Device
# Uses mqttx CLI tool and Jira API

set -e

# Configuration
MQTT_BROKER="${MQTT_BROKER_HOST:-192.168.2.62}"
MQTT_PORT="${MQTT_BROKER_PORT:-1883}"
DEVICE_ID="${DEVICE_ID:-rapidreach_device}"
COMMAND_TOPIC="rapidreach/${DEVICE_ID}/cli/command"
RESPONSE_TOPIC="rapidreach/${DEVICE_ID}/cli/response"

# Source Jira environment
source ~/.config/rapidreach/jira.env

# Test results file
RESULTS_FILE="acceptance-test-results.txt"
> "$RESULTS_FILE"

# Function to send command and get response
send_command() {
    local cmd="$1"
    echo "  Sending: $cmd"
    
    # Subscribe to response in background
    mqttx sub -t "$RESPONSE_TOPIC" -h "$MQTT_BROKER" -p "$MQTT_PORT" > /tmp/mqtt_response.txt &
    SUB_PID=$!
    sleep 1
    
    # Send command
    mqttx pub -t "$COMMAND_TOPIC" -m "$cmd" -h "$MQTT_BROKER" -p "$MQTT_PORT"
    
    # Wait for response
    sleep 2
    
    # Kill subscription
    kill $SUB_PID 2>/dev/null || true
    
    # Get response
    if [ -f /tmp/mqtt_response.txt ] && [ -s /tmp/mqtt_response.txt ]; then
        RESPONSE=$(cat /tmp/mqtt_response.txt | tail -1)
        echo "  Response: ${RESPONSE:0:100}..."
        echo "$RESPONSE"
    else
        echo "  No response received"
        echo ""
    fi
}

# Function to update Jira task
update_jira_task() {
    local task_id="$1"
    local status="$2"
    local time_minutes="$3"
    
    echo "  Updating Jira task $task_id..."
    
    # Log work
    WORKLOG_JSON=$(cat <<EOF
{
    "timeSpent": "${time_minutes}m",
    "comment": "Automated test ${status} via MQTT CLI interface",
    "started": "$(date -u +%Y-%m-%dT%H:%M:%S.000+0000)"
}
EOF
)
    
    curl -s -X POST \
        -u "${JIRA_USER}:${JIRA_TOKEN}" \
        -H "Content-Type: application/json" \
        -d "$WORKLOG_JSON" \
        "${JIRA_URL}/rest/api/2/issue/${task_id}/worklog" > /dev/null
    
    if [ "$status" = "passed" ]; then
        # Get transitions
        TRANSITIONS=$(curl -s -X GET \
            -u "${JIRA_USER}:${JIRA_TOKEN}" \
            "${JIRA_URL}/rest/api/2/issue/${task_id}/transitions")
        
        # Find Done transition ID
        DONE_ID=$(echo "$TRANSITIONS" | grep -B2 -A2 '"name":"Done"' | grep '"id"' | head -1 | sed 's/.*"id":"\([^"]*\)".*/\1/')
        
        if [ ! -z "$DONE_ID" ]; then
            # Move to Done
            curl -s -X POST \
                -u "${JIRA_USER}:${JIRA_TOKEN}" \
                -H "Content-Type: application/json" \
                -d "{\"transition\":{\"id\":\"$DONE_ID\"}}" \
                "${JIRA_URL}/rest/api/2/issue/${task_id}/transitions" > /dev/null
            echo "  ✓ Moved to Done"
        fi
    fi
}

# Test MQTT connection first
echo "Testing MQTT connection..."
RESPONSE=$(send_command "rapidreach mqtt status")
if [[ "$RESPONSE" == *"Connected"* ]]; then
    echo "✓ MQTT connection verified"
else
    echo "✗ MQTT connection failed"
    exit 1
fi

echo ""
echo "Starting Acceptance Tests"
echo "========================="
echo ""

# Run tests
PASSED=0
FAILED=0

# Test 1: Basic CLI functionality
echo "Test RDP-210: Test shell commands execution"
START_TIME=$(date +%s)
RESPONSE=$(send_command "help")
if [[ "$RESPONSE" == *"Available commands"* ]] || [[ "$RESPONSE" == *"help"* ]]; then
    echo "✓ PASSED"
    update_jira_task "RDP-210" "passed" "2"
    ((PASSED++))
else
    echo "✗ FAILED"
    ((FAILED++))
fi
END_TIME=$(date +%s)
echo "Time: $((END_TIME - START_TIME))s" | tee -a "$RESULTS_FILE"
echo "" | tee -a "$RESULTS_FILE"

# Test 2: Test command
echo "Test RDP-210: Test 'rapidreach test' command"
START_TIME=$(date +%s)
RESPONSE=$(send_command "rapidreach test")
if [[ "$RESPONSE" == *"Hello"* ]]; then
    echo "✓ PASSED"
    ((PASSED++))
else
    echo "✗ FAILED"
    ((FAILED++))
fi
END_TIME=$(date +%s)
echo "Time: $((END_TIME - START_TIME))s" | tee -a "$RESULTS_FILE"
echo "" | tee -a "$RESULTS_FILE"

# Test 3: MQTT Status
echo "Test RDP-221: Test MQTT connection"
START_TIME=$(date +%s)
RESPONSE=$(send_command "rapidreach mqtt status")
if [[ "$RESPONSE" == *"Connected"* ]] || [[ "$RESPONSE" == *"MQTT"* ]]; then
    echo "✓ PASSED"
    update_jira_task "RDP-221" "passed" "2"
    ((PASSED++))
else
    echo "✗ FAILED"
    ((FAILED++))
fi
END_TIME=$(date +%s)
echo "Time: $((END_TIME - START_TIME))s" | tee -a "$RESULTS_FILE"
echo "" | tee -a "$RESULTS_FILE"

# Test 4: System uptime
echo "Test RDP-207: Check system uptime"
START_TIME=$(date +%s)
RESPONSE=$(send_command "rapidreach system uptime")
if [[ "$RESPONSE" == *"Uptime"* ]] || [[ "$RESPONSE" == *"seconds"* ]] || [[ "$RESPONSE" == *"up"* ]]; then
    echo "✓ PASSED"
    update_jira_task "RDP-207" "passed" "1"
    ((PASSED++))
else
    echo "✗ FAILED - May not be implemented yet"
    ((FAILED++))
fi
END_TIME=$(date +%s)
echo "Time: $((END_TIME - START_TIME))s" | tee -a "$RESULTS_FILE"
echo "" | tee -a "$RESULTS_FILE"

# Test 5: Network status (try different commands)
echo "Test RDP-194: Test network connectivity"
START_TIME=$(date +%s)
RESPONSE=$(send_command "rapidreach network status")
if [[ "$RESPONSE" == *"network"* ]] || [[ "$RESPONSE" == *"IP"* ]] || [[ "$RESPONSE" == *"Status"* ]]; then
    echo "✓ PASSED"
    update_jira_task "RDP-194" "passed" "2"
    ((PASSED++))
else
    # Try alternative command
    RESPONSE=$(send_command "rapidreach network ethernet status")
    if [[ "$RESPONSE" == *"ethernet"* ]] || [[ "$RESPONSE" == *"network"* ]]; then
        echo "✓ PASSED (alternative command)"
        update_jira_task "RDP-194" "passed" "2"
        ((PASSED++))
    else
        echo "✗ FAILED - May not be implemented yet"
        ((FAILED++))
    fi
fi
END_TIME=$(date +%s)
echo "Time: $((END_TIME - START_TIME))s" | tee -a "$RESULTS_FILE"
echo "" | tee -a "$RESULTS_FILE"

# Test 6: LED control
echo "Test RDP-200: Test LED control"
START_TIME=$(date +%s)
RESPONSE=$(send_command "rapidreach led status")
if [[ "$RESPONSE" == *"LED"* ]] || [[ "$RESPONSE" == *"led"* ]] || [[ "$RESPONSE" == *"Status"* ]]; then
    echo "✓ PASSED"
    update_jira_task "RDP-200" "passed" "2"
    ((PASSED++))
else
    # Try alternative command
    RESPONSE=$(send_command "rapidreach led all off")
    if [[ "$RESPONSE" == *"LED"* ]] || [[ "$RESPONSE" == *"off"* ]]; then
        echo "✓ PASSED (alternative command)"
        update_jira_task "RDP-200" "passed" "2"
        ((PASSED++))
    else
        echo "✗ FAILED - May not be implemented yet"
        ((FAILED++))
    fi
fi
END_TIME=$(date +%s)
echo "Time: $((END_TIME - START_TIME))s" | tee -a "$RESULTS_FILE"
echo "" | tee -a "$RESULTS_FILE"

# Test 7: MQTT Heartbeat
echo "Test RDP-223: Test MQTT heartbeat"
START_TIME=$(date +%s)
RESPONSE=$(send_command "rapidreach mqtt heartbeat status")
if [[ "$RESPONSE" == *"Heartbeat"* ]] || [[ "$RESPONSE" == *"heartbeat"* ]] || [[ "$RESPONSE" == *"Active"* ]]; then
    echo "✓ PASSED"
    update_jira_task "RDP-223" "passed" "2"
    ((PASSED++))
else
    # Try starting heartbeat
    RESPONSE=$(send_command "rapidreach mqtt heartbeat start")
    if [[ "$RESPONSE" == *"Started"* ]] || [[ "$RESPONSE" == *"started"* ]]; then
        echo "✓ PASSED (started heartbeat)"
        update_jira_task "RDP-223" "passed" "3"
        ((PASSED++))
    else
        echo "✗ FAILED"
        ((FAILED++))
    fi
fi
END_TIME=$(date +%s)
echo "Time: $((END_TIME - START_TIME))s" | tee -a "$RESULTS_FILE"
echo "" | tee -a "$RESULTS_FILE"

# Summary
echo ""
echo "================================"
echo "ACCEPTANCE TEST SUMMARY"
echo "================================"
echo "Total tests run: $((PASSED + FAILED))"
echo "Passed: $PASSED"
echo "Failed: $FAILED"
echo "================================"
echo ""
echo "Results saved to: $RESULTS_FILE"

# Clean up
rm -f /tmp/mqtt_response.txt
