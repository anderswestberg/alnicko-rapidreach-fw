#!/bin/bash
# Setup script for MQTT broker - RapidReach firmware testing

echo "=== RapidReach MQTT Broker Setup ==="
echo ""

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo "Docker not found. Installing Docker..."
    
    # Update package index
    sudo apt-get update
    
    # Install required packages
    sudo apt-get install -y \
        ca-certificates \
        curl \
        gnupg \
        lsb-release
    
    # Add Docker's official GPG key
    sudo mkdir -p /etc/apt/keyrings
    curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
    
    # Set up the repository
    echo \
      "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu \
      $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
    
    # Update package index again
    sudo apt-get update
    
    # Install Docker Engine
    sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin
    
    # Add current user to docker group
    sudo usermod -aG docker $USER
    
    echo "Docker installed! Please log out and log back in, then run this script again."
    exit 0
fi

echo "Docker found! Starting MQTT broker..."

# Create necessary directories
mkdir -p mqtt/data mqtt/log

# Set proper permissions
sudo chown -R 1883:1883 mqtt/data mqtt/log

# Start the MQTT broker
echo "Starting Mosquitto MQTT broker..."
docker compose up -d

# Check if containers are running
echo ""
echo "Checking container status..."
docker compose ps

# Get the host IP for device configuration
HOST_IP=$(hostname -I | awk '{print $1}')

echo ""
echo "=== MQTT Broker Setup Complete ==="
echo ""
echo "MQTT Broker is running on:"
echo "  Host: $HOST_IP"
echo "  Port: 1883 (MQTT)"
echo "  Port: 9001 (WebSocket)"
echo ""
echo "Configure your RapidReach device with:"
echo "  Broker Host: $HOST_IP"
echo "  Broker Port: 1883"
echo "  Client ID: rapidreach-device-01"
echo ""
echo "To monitor messages:"
echo "  docker exec -it mqtt-test-client /scripts/monitor_all.sh"
echo ""
echo "To stop the broker:"
echo "  docker compose down"
echo ""
