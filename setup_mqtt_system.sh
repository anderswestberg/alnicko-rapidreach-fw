#!/bin/bash
# Alternative MQTT broker setup using system packages (no Docker required)

echo "=== RapidReach MQTT Broker Setup (System Package) ==="
echo ""

# Check if mosquitto is already installed
if command -v mosquitto &> /dev/null; then
    echo "Mosquitto already installed!"
else
    echo "Installing Mosquitto MQTT broker..."
    sudo apt-get update
    sudo apt-get install -y mosquitto mosquitto-clients
fi

# Create configuration directory
sudo mkdir -p /etc/mosquitto/conf.d

# Create RapidReach-specific configuration
sudo tee /etc/mosquitto/conf.d/rapidreach.conf > /dev/null << EOF
# RapidReach MQTT Configuration

# Listen on all interfaces
listener 1883 0.0.0.0

# Logging
log_dest file /var/log/mosquitto/mosquitto.log
log_dest stdout
log_type all

# Allow anonymous connections (for testing)
allow_anonymous true

# Persistence
persistence true
persistence_location /var/lib/mosquitto/

# Connection settings
max_connections 100
max_keepalive 300
keepalive_interval 60
EOF

# Ensure log directory exists and has correct permissions
sudo mkdir -p /var/log/mosquitto
sudo chown mosquitto:mosquitto /var/log/mosquitto

# Start and enable mosquitto service
sudo systemctl start mosquitto
sudo systemctl enable mosquitto

# Check if service is running
if sudo systemctl is-active --quiet mosquitto; then
    echo ""
    echo "=== MQTT Broker Setup Complete ==="
    echo ""
    
    # Get the host IP
    HOST_IP=$(hostname -I | awk '{print $1}')
    
    echo "MQTT Broker is running on:"
    echo "  Host: $HOST_IP"
    echo "  Port: 1883"
    echo ""
    echo "Configure your RapidReach device with:"
    echo "  Broker Host: $HOST_IP"
    echo "  Broker Port: 1883"
    echo "  Client ID: rapidreach-device-01"
    echo ""
    echo "Useful commands:"
    echo "  # Monitor all RapidReach messages:"
    echo "  mosquitto_sub -h localhost -t 'rapidreach/#' -v"
    echo ""
    echo "  # Monitor heartbeat only:"
    echo "  mosquitto_sub -h localhost -t 'rapidreach/heartbeat' -v"
    echo ""
    echo "  # Send test message:"
    echo "  mosquitto_pub -h localhost -t 'rapidreach/test' -m 'Hello from system broker'"
    echo ""
    echo "  # Check broker status:"
    echo "  sudo systemctl status mosquitto"
    echo ""
    echo "  # View broker logs:"
    echo "  sudo tail -f /var/log/mosquitto/mosquitto.log"
    echo ""
else
    echo "ERROR: Failed to start Mosquitto service!"
    echo "Check status with: sudo systemctl status mosquitto"
    exit 1
fi
