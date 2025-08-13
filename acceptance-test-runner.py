#!/usr/bin/env python3
"""
Acceptance Test Runner for RapidReach Device via MQTT CLI Bridge
"""

import os
import sys
import time
import json
import subprocess
from datetime import datetime
import requests
from requests.auth import HTTPBasicAuth
import paho.mqtt.client as mqtt
from collections import defaultdict

# Configuration
MQTT_BROKER = os.getenv('MQTT_BROKER_HOST', '192.168.2.62')
MQTT_PORT = int(os.getenv('MQTT_BROKER_PORT', '1883'))
DEVICE_ID = os.getenv('DEVICE_ID', 'rapidreach_device')

JIRA_URL = os.getenv('JIRA_URL')
JIRA_USER = os.getenv('JIRA_USER')
JIRA_TOKEN = os.getenv('JIRA_TOKEN')

# Test definitions
TEST_CASES = {
    # Audio tests
    'RDP-179': {
        'name': 'Test audio playback with CLI commands',
        'commands': [
            'rapidreach audio play /path/to/test.wav',
            'rapidreach audio status'
        ],
        'expected': ['Playing', 'Status:'],
        'time_minutes': 5
    },
    'RDP-180': {
        'name': 'Test volume control (0-100 range)',
        'commands': [
            'rapidreach audio volume 0',
            'rapidreach audio volume 50',
            'rapidreach audio volume 100',
            'rapidreach audio volume'
        ],
        'expected': ['Volume set to', 'Current volume:'],
        'time_minutes': 3
    },
    'RDP-181': {
        'name': 'Test mute/unmute functionality',
        'commands': [
            'rapidreach audio mute',
            'rapidreach audio status',
            'rapidreach audio unmute',
            'rapidreach audio status'
        ],
        'expected': ['Muted', 'Unmuted'],
        'time_minutes': 3
    },
    # Power management tests
    'RDP-186': {
        'name': 'Check battery status via CLI',
        'commands': [
            'rapidreach power battery',
            'rapidreach power status'
        ],
        'expected': ['Battery:', 'Level:', '%'],
        'time_minutes': 2
    },
    'RDP-187': {
        'name': 'Monitor charging state',
        'commands': [
            'rapidreach power charging'
        ],
        'expected': ['Charging:', 'Status:'],
        'time_minutes': 2
    },
    # Network tests
    'RDP-193': {
        'name': 'Test WiFi connection and status',
        'commands': [
            'rapidreach network wifi status',
            'rapidreach network wifi scan'
        ],
        'expected': ['WiFi', 'Status:', 'SSID:'],
        'time_minutes': 3
    },
    'RDP-194': {
        'name': 'Test Ethernet connectivity',
        'commands': [
            'rapidreach network ethernet status'
        ],
        'expected': ['Ethernet', 'Status:', 'IP:'],
        'time_minutes': 2
    },
    'RDP-195': {
        'name': 'Test LTE modem connection',
        'commands': [
            'rapidreach network lte status',
            'rapidreach network lte signal'
        ],
        'expected': ['LTE', 'Status:', 'Signal:'],
        'time_minutes': 3
    },
    # Device I/O tests
    'RDP-200': {
        'name': 'Test all LED colors and patterns',
        'commands': [
            'rapidreach led red on',
            'rapidreach led green on',
            'rapidreach led blue on',
            'rapidreach led all off',
            'rapidreach led red blink',
            'rapidreach led status'
        ],
        'expected': ['LED', 'on', 'off', 'blink'],
        'time_minutes': 4
    },
    'RDP-201': {
        'name': 'Test switch/button detection',
        'commands': [
            'rapidreach gpio read button1',
            'rapidreach gpio status'
        ],
        'expected': ['GPIO', 'State:', 'Level:'],
        'time_minutes': 3
    },
    # System tests
    'RDP-207': {
        'name': 'Check system uptime',
        'commands': [
            'rapidreach system uptime'
        ],
        'expected': ['Uptime:', 'seconds', 'minutes'],
        'time_minutes': 1
    },
    'RDP-208': {
        'name': 'Monitor CPU usage',
        'commands': [
            'rapidreach system cpu'
        ],
        'expected': ['CPU:', 'Usage:', '%'],
        'time_minutes': 2
    },
    'RDP-209': {
        'name': 'Check memory usage',
        'commands': [
            'rapidreach system memory'
        ],
        'expected': ['Memory:', 'Used:', 'Free:', 'KB'],
        'time_minutes': 2
    },
    'RDP-210': {
        'name': 'Test shell commands execution',
        'commands': [
            'help',
            'version',
            'rapidreach test'
        ],
        'expected': ['Available commands:', 'Version:', 'Hello'],
        'time_minutes': 2
    },
    # MQTT specific tests
    'RDP-221': {
        'name': 'Test MQTT connection',
        'commands': [
            'rapidreach mqtt status'
        ],
        'expected': ['MQTT', 'Connected', 'Broker:'],
        'time_minutes': 2
    },
    'RDP-222': {
        'name': 'Test MQTT publish functionality',
        'commands': [
            'rapidreach mqtt publish test/topic "Test message"'
        ],
        'expected': ['Published', 'Success'],
        'time_minutes': 2
    },
    'RDP-223': {
        'name': 'Test MQTT heartbeat',
        'commands': [
            'rapidreach mqtt heartbeat status'
        ],
        'expected': ['Heartbeat', 'Active', 'Interval:'],
        'time_minutes': 2
    },
}

class AcceptanceTestRunner:
    def __init__(self):
        self.mqtt_client = None
        self.responses = []
        self.current_test = None
        self.test_results = {}
        self.jira_auth = HTTPBasicAuth(JIRA_USER, JIRA_TOKEN)
        
    def setup_mqtt(self):
        """Setup MQTT client for testing"""
        self.mqtt_client = mqtt.Client()
        self.mqtt_client.on_connect = self.on_connect
        self.mqtt_client.on_message = self.on_message
        
        try:
            self.mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
            self.mqtt_client.loop_start()
            time.sleep(2)  # Wait for connection
            return True
        except Exception as e:
            print(f"Failed to connect to MQTT: {e}")
            return False
    
    def on_connect(self, client, userdata, flags, rc):
        """MQTT connection callback"""
        if rc == 0:
            print(f"Connected to MQTT broker at {MQTT_BROKER}:{MQTT_PORT}")
            # Subscribe to response topic
            response_topic = f"rapidreach/{DEVICE_ID}/cli/response"
            client.subscribe(response_topic)
            print(f"Subscribed to {response_topic}")
        else:
            print(f"Failed to connect, return code {rc}")
    
    def on_message(self, client, userdata, msg):
        """MQTT message callback"""
        try:
            response = msg.payload.decode('utf-8')
            self.responses.append(response)
            print(f"Response: {response[:100]}...")
        except Exception as e:
            print(f"Error processing message: {e}")
    
    def send_command(self, command):
        """Send command via MQTT"""
        self.responses = []
        command_topic = f"rapidreach/{DEVICE_ID}/cli/command"
        
        print(f"  Sending: {command}")
        self.mqtt_client.publish(command_topic, command)
        
        # Wait for response
        time.sleep(1)
        
        return '\n'.join(self.responses)
    
    def run_test_case(self, test_id, test_info):
        """Run a single test case"""
        print(f"\n{'='*60}")
        print(f"Running test {test_id}: {test_info['name']}")
        print(f"{'='*60}")
        
        start_time = time.time()
        test_passed = True
        results = []
        
        for command in test_info['commands']:
            response = self.send_command(command)
            results.append({
                'command': command,
                'response': response
            })
            
            # Check if expected strings are in response
            if response:
                found_expected = False
                for expected in test_info['expected']:
                    if expected.lower() in response.lower():
                        found_expected = True
                        break
                
                if not found_expected and test_info['expected']:
                    test_passed = False
                    print(f"  ❌ Expected string not found in response")
            else:
                print(f"  ⚠️  No response received")
                test_passed = False
        
        elapsed_time = time.time() - start_time
        
        # Store results
        self.test_results[test_id] = {
            'passed': test_passed,
            'elapsed_minutes': elapsed_time / 60,
            'estimated_minutes': test_info['time_minutes'],
            'results': results
        }
        
        print(f"\nTest {test_id}: {'PASSED ✓' if test_passed else 'FAILED ✗'}")
        print(f"Time: {elapsed_time:.1f}s (estimated: {test_info['time_minutes']}min)")
        
        return test_passed
    
    def update_jira_task(self, test_id, passed, time_minutes):
        """Update Jira task status and log work"""
        try:
            # Log work
            worklog_data = {
                'timeSpent': f'{int(time_minutes)}m',
                'comment': f'Automated test {"passed" if passed else "failed"} via MQTT CLI interface',
                'started': datetime.now().strftime('%Y-%m-%dT%H:%M:%S.000+0000')
            }
            
            worklog_response = requests.post(
                f'{JIRA_URL}/rest/api/2/issue/{test_id}/worklog',
                auth=self.jira_auth,
                json=worklog_data
            )
            
            if worklog_response.status_code in [200, 201]:
                print(f"  ✓ Logged {time_minutes:.1f} minutes to {test_id}")
            else:
                print(f"  ✗ Failed to log work: {worklog_response.status_code}")
            
            # Move to Done if passed
            if passed:
                # First get available transitions
                transitions_response = requests.get(
                    f'{JIRA_URL}/rest/api/2/issue/{test_id}/transitions',
                    auth=self.jira_auth
                )
                
                if transitions_response.status_code == 200:
                    transitions = transitions_response.json()['transitions']
                    done_transition = None
                    
                    for transition in transitions:
                        if transition['name'].lower() == 'done':
                            done_transition = transition['id']
                            break
                    
                    if done_transition:
                        transition_data = {
                            'transition': {
                                'id': done_transition
                            }
                        }
                        
                        transition_response = requests.post(
                            f'{JIRA_URL}/rest/api/2/issue/{test_id}/transitions',
                            auth=self.jira_auth,
                            json=transition_data
                        )
                        
                        if transition_response.status_code == 204:
                            print(f"  ✓ Moved {test_id} to Done")
                        else:
                            print(f"  ✗ Failed to transition: {transition_response.status_code}")
                    else:
                        print(f"  ⚠️  'Done' transition not found")
                        
        except Exception as e:
            print(f"  ✗ Error updating Jira: {e}")
    
    def run_all_tests(self):
        """Run all acceptance tests"""
        if not self.setup_mqtt():
            print("Failed to setup MQTT connection")
            return
        
        print(f"\nStarting acceptance tests at {datetime.now()}")
        print(f"Testing {len(TEST_CASES)} test cases via MQTT CLI bridge\n")
        
        passed_count = 0
        failed_count = 0
        total_time = 0
        
        for test_id, test_info in TEST_CASES.items():
            try:
                passed = self.run_test_case(test_id, test_info)
                
                if passed:
                    passed_count += 1
                else:
                    failed_count += 1
                
                # Update Jira
                actual_time = self.test_results[test_id]['elapsed_minutes']
                self.update_jira_task(test_id, passed, max(actual_time, 1))
                total_time += actual_time
                
                # Small delay between tests
                time.sleep(2)
                
            except Exception as e:
                print(f"Error running test {test_id}: {e}")
                failed_count += 1
        
        # Summary
        print(f"\n{'='*60}")
        print(f"ACCEPTANCE TEST SUMMARY")
        print(f"{'='*60}")
        print(f"Total tests: {len(TEST_CASES)}")
        print(f"Passed: {passed_count}")
        print(f"Failed: {failed_count}")
        print(f"Total time: {total_time:.1f} minutes")
        print(f"{'='*60}\n")
        
        # Save detailed results
        with open('acceptance-test-results.json', 'w') as f:
            json.dump({
                'timestamp': datetime.now().isoformat(),
                'summary': {
                    'total': len(TEST_CASES),
                    'passed': passed_count,
                    'failed': failed_count,
                    'total_minutes': total_time
                },
                'results': self.test_results
            }, f, indent=2)
        
        print("Detailed results saved to acceptance-test-results.json")
        
        # Cleanup
        self.mqtt_client.loop_stop()
        self.mqtt_client.disconnect()

if __name__ == '__main__':
    # Check environment
    if not all([JIRA_URL, JIRA_USER, JIRA_TOKEN]):
        print("Error: Jira environment variables not set")
        print("Please source ~/.config/rapidreach/jira.env")
        sys.exit(1)
    
    runner = AcceptanceTestRunner()
    runner.run_all_tests()
