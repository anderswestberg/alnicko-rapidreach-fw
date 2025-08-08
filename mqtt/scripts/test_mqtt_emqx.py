#!/usr/bin/env python3
"""
MQTT Test Client for RapidReach - EMQX Compatible
This script tests MQTT connectivity and message publishing/subscribing
"""

import paho.mqtt.client as mqtt
import json
import time
import threading
from datetime import datetime

# MQTT Configuration
BROKER_HOST = "emqx"  # Docker service name
BROKER_PORT = 1883
TOPIC_HEARTBEAT = "rapidreach/heartbeat"
TOPIC_STATUS = "rapidreach/status"

class MQTTTestClient:
    def __init__(self):
        self.client = mqtt.Client(client_id="rapidreach_test_client")
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.client.on_disconnect = self.on_disconnect
        self.connected = False

    def on_connect(self, client, userdata, flags, rc):
        """Callback for when the client receives a CONNACK response from the server."""
        if rc == 0:
            print(f"✅ Connected to EMQX broker at {BROKER_HOST}:{BROKER_PORT}")
            self.connected = True
            
            # Subscribe to topics
            client.subscribe(f"{TOPIC_HEARTBEAT}")
            client.subscribe(f"{TOPIC_STATUS}")
            print(f"📡 Subscribed to {TOPIC_HEARTBEAT} and {TOPIC_STATUS}")
            
        else:
            print(f"❌ Failed to connect, return code {rc}")

    def on_message(self, client, userdata, msg):
        """Callback for when a PUBLISH message is received from the server."""
        try:
            topic = msg.topic
            payload = msg.payload.decode()
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            
            print(f"\n📨 [{timestamp}] Message received:")
            print(f"   Topic: {topic}")
            print(f"   Payload: {payload}")
            
            # Try to parse as JSON for pretty printing
            try:
                json_data = json.loads(payload)
                print(f"   JSON Data: {json.dumps(json_data, indent=2)}")
            except:
                pass
                
        except Exception as e:
            print(f"❌ Error processing message: {e}")

    def on_disconnect(self, client, userdata, rc):
        """Callback for when the client disconnects from the server."""
        self.connected = False
        print(f"🔌 Disconnected from broker (rc={rc})")

    def connect(self):
        """Connect to the MQTT broker."""
        try:
            print(f"🔗 Connecting to EMQX broker at {BROKER_HOST}:{BROKER_PORT}...")
            self.client.connect(BROKER_HOST, BROKER_PORT, 60)
            self.client.loop_start()
            
            # Wait for connection
            timeout = 10
            while not self.connected and timeout > 0:
                time.sleep(0.5)
                timeout -= 0.5
                
            if not self.connected:
                print("❌ Connection timeout")
                return False
                
            return True
            
        except Exception as e:
            print(f"❌ Connection error: {e}")
            return False

    def publish_test_message(self, topic, message):
        """Publish a test message."""
        if not self.connected:
            print("❌ Not connected to broker")
            return False
            
        try:
            timestamp = datetime.now().isoformat()
            if isinstance(message, dict):
                message['timestamp'] = timestamp
                payload = json.dumps(message)
            else:
                payload = str(message)
                
            result = self.client.publish(topic, payload)
            
            if result.rc == 0:
                print(f"✅ Published to {topic}: {payload}")
                return True
            else:
                print(f"❌ Failed to publish to {topic}")
                return False
                
        except Exception as e:
            print(f"❌ Publish error: {e}")
            return False

    def disconnect(self):
        """Disconnect from the broker."""
        if self.connected:
            self.client.loop_stop()
            self.client.disconnect()

def main():
    print("🚀 RapidReach MQTT Test Client for EMQX")
    print("=" * 50)
    
    client = MQTTTestClient()
    
    if not client.connect():
        print("❌ Failed to connect to MQTT broker")
        return
    
    try:
        # Send some test messages
        print("\n📤 Sending test messages...")
        
        # Test heartbeat message
        heartbeat_msg = {
            "device_id": "rapidreach_test",
            "status": "alive",
            "uptime": 12345,
            "memory_free": 85,
            "signal_strength": -67
        }
        client.publish_test_message(TOPIC_HEARTBEAT, heartbeat_msg)
        
        time.sleep(1)
        
        # Test status message
        status_msg = {
            "device_id": "rapidreach_test",
            "firmware_version": "1.0.0",
            "hardware_version": "rev_a",
            "temperature": 23.5,
            "battery_level": 87
        }
        client.publish_test_message(TOPIC_STATUS, status_msg)
        
        # Keep the client running to receive messages
        print(f"\n👂 Listening for messages on {TOPIC_HEARTBEAT} and {TOPIC_STATUS}...")
        print("Press Ctrl+C to exit")
        
        while True:
            time.sleep(1)
            
    except KeyboardInterrupt:
        print("\n🛑 Stopping MQTT test client...")
    except Exception as e:
        print(f"❌ Error: {e}")
    finally:
        client.disconnect()
        print("👋 Goodbye!")

if __name__ == "__main__":
    main()
