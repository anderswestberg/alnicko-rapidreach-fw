#!/bin/bash

# Development Setup Script for RapidReach
# This script sets up the development environment with:
# - MQTT broker and MongoDB in Docker containers
# - Device server running on the host for easier debugging

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== RapidReach Development Setup ===${NC}\n"

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check prerequisites
echo -e "${YELLOW}Checking prerequisites...${NC}"

if ! command_exists docker; then
    echo -e "${RED}✗ Docker is not installed. Please install Docker first.${NC}"
    exit 1
fi

if ! command_exists node; then
    echo -e "${RED}✗ Node.js is not installed. Please install Node.js first.${NC}"
    exit 1
fi

echo -e "${GREEN}✓ All prerequisites met${NC}\n"

# Create necessary directories
echo -e "${YELLOW}Creating directories...${NC}"
mkdir -p logs
echo -e "${GREEN}✓ Created logs directory${NC}\n"

# Start Docker services
echo -e "${YELLOW}Starting Docker services (MQTT broker and MongoDB)...${NC}"
docker compose -f docker-compose.dev.yml up -d

# Wait for services to be healthy
echo -e "${YELLOW}Waiting for services to be ready...${NC}"
sleep 5

# Check if services are running
if docker ps | grep -q rapidreach-emqx-dev; then
    echo -e "${GREEN}✓ MQTT broker is running on port 1883${NC}"
    echo -e "  Dashboard: http://localhost:18083 (admin/public)"
else
    echo -e "${RED}✗ MQTT broker failed to start${NC}"
    exit 1
fi

if docker ps | grep -q rapidreach-mongo-dev; then
    echo -e "${GREEN}✓ MongoDB is running on port 27017${NC}"
else
    echo -e "${RED}✗ MongoDB failed to start${NC}"
    exit 1
fi

echo -e "\n${YELLOW}Setting up device-server environment...${NC}"

# Create .env file for device-server if it doesn't exist
cd device-server
if [ ! -f .env ]; then
    cat > .env << EOF
# Device Server Configuration - Development Mode
PORT=3002
NODE_ENV=development

# MQTT Configuration (Docker container on localhost)
MQTT_BROKER_HOST=localhost
MQTT_BROKER_PORT=1883
MQTT_USERNAME=admin
MQTT_PASSWORD=public

# MongoDB Configuration (Docker container on localhost)
MONGODB_URI=mongodb://localhost:27017
MONGODB_DB=rapidreach

# API Configuration
API_KEY=dev-api-key-12345

# Logging
LOG_LEVEL=debug
EOF
    echo -e "${GREEN}✓ Created .env file for device-server${NC}"
else
    echo -e "${BLUE}ℹ Using existing .env file${NC}"
fi

# Install dependencies if needed
if [ ! -d "node_modules" ]; then
    echo -e "${YELLOW}Installing device-server dependencies...${NC}"
    npm install
fi

cd ..

# Set up web-app environment if it exists
if [ -d "web-app" ]; then
    echo -e "\n${YELLOW}Setting up web-app environment...${NC}"
    cd web-app
    if [ ! -f .env ]; then
        cat > .env << EOF
# Web App Configuration - Development Mode
VITE_API_URL=http://localhost:3002/api
VITE_API_KEY=dev-api-key-12345
EOF
        echo -e "${GREEN}✓ Created .env file for web-app${NC}"
    else
        echo -e "${BLUE}ℹ Using existing .env file${NC}"
    fi
    cd ..
fi

echo -e "\n${GREEN}=== Setup Complete! ===${NC}\n"
echo -e "${BLUE}Services available:${NC}"
echo -e "  • MQTT Broker: localhost:1883"
echo -e "  • MQTT Dashboard: http://localhost:18083 (admin/public)"
echo -e "  • MongoDB: localhost:27017"
echo -e ""
echo -e "${BLUE}To start the device-server:${NC}"
echo -e "  cd device-server"
echo -e "  npm run dev"
echo -e ""
echo -e "${BLUE}Or use the helper script:${NC}"
echo -e "  ./dev-services.sh start"
echo -e ""
echo -e "${BLUE}To stop Docker services:${NC}"
echo -e "  docker compose -f docker-compose.dev.yml down"
echo -e ""
echo -e "${BLUE}To view logs:${NC}"
echo -e "  docker compose -f docker-compose.dev.yml logs -f"
