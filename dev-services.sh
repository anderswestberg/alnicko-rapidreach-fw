#!/bin/bash

# Helper script for managing development services

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

case "$1" in
    start)
        echo -e "${YELLOW}Starting development services...${NC}"
        
        # Ensure Docker services are running
        if ! docker ps | grep -q rapidreach-emqx-dev; then
            echo -e "${YELLOW}Starting Docker services (MQTT & MongoDB)...${NC}"
            docker compose -f docker-compose.dev.yml up -d
            sleep 3
        fi
        
        # Start device server
        cd device-server
        if [ ! -f .env ]; then
            if [ -f env.example ]; then
                cp env.example .env
            else
                # Create a development .env file
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
            fi
            echo -e "${GREEN}Created .env file for device server${NC}"
        fi
        
        # Install dependencies if needed
        if [ ! -d "node_modules" ]; then
            echo -e "${YELLOW}Installing device-server dependencies...${NC}"
            npm install
        fi
        
        screen -dmS device-server bash -c 'npm run dev 2>&1 | tee ../logs/device-server-debug.log'
        echo -e "${GREEN}✓ Device server started on port 3002${NC}"
        
        # Start web app if it exists
        if [ -d "../web-app" ]; then
            cd ../web-app
            if [ ! -f .env ]; then
                # Create a development .env file for web app
                cat > .env << EOF
# Web App Configuration - Development Mode
VITE_API_URL=http://localhost:3002/api
VITE_API_KEY=dev-api-key-12345
EOF
                echo -e "${GREEN}Created .env file for web app${NC}"
            fi
            if [ ! -d "node_modules" ]; then
                echo -e "${YELLOW}Installing web-app dependencies...${NC}"
                npm install
            fi
            screen -dmS web-app bash -c 'npm run dev 2>&1 | tee ../logs/web-app-debug.log'
            echo -e "${GREEN}✓ Web app started on port 5173${NC}"
        fi
        
        cd ..
        echo -e "\n${GREEN}All services started!${NC}"
        echo -e "MQTT Dashboard: http://localhost:18083/ (admin/public)"
        if [ -d "web-app" ]; then
            echo -e "Web App: http://localhost:5173/"
        fi
        echo -e "API: http://localhost:3002/"
        ;;
        
    stop)
        echo -e "${YELLOW}Stopping development services...${NC}"
        screen -X -S device-server quit 2>/dev/null && echo -e "${GREEN}✓ Device server stopped${NC}"
        screen -X -S web-app quit 2>/dev/null && echo -e "${GREEN}✓ Web app stopped${NC}"
        
        # Ask about Docker services
        echo -e "\n${YELLOW}Stop Docker services (MQTT & MongoDB)? [y/N]${NC}"
        read -r response
        if [[ "$response" =~ ^[Yy]$ ]]; then
            docker compose -f docker-compose.dev.yml down
            echo -e "${GREEN}✓ Docker services stopped${NC}"
        fi
        ;;
        
    status)
        echo -e "${YELLOW}Service Status:${NC}\n"
        
        # Docker services status
        echo -e "${YELLOW}Docker Services:${NC}"
        if docker ps | grep -q rapidreach-emqx-dev; then
            echo -e "${GREEN}✓ MQTT broker is running (port 1883)${NC}"
        else
            echo -e "${RED}✗ MQTT broker is not running${NC}"
        fi
        
        if docker ps | grep -q rapidreach-mongo-dev; then
            echo -e "${GREEN}✓ MongoDB is running (port 27017)${NC}"
        else
            echo -e "${RED}✗ MongoDB is not running${NC}"
        fi
        
        echo -e "\n${YELLOW}Application Services:${NC}"
        if screen -ls | grep -q "device-server"; then
            echo -e "${GREEN}✓ Device server is running (port 3002)${NC}"
        else
            echo -e "${RED}✗ Device server is not running${NC}"
        fi
        
        if screen -ls | grep -q "web-app"; then
            echo -e "${GREEN}✓ Web app is running (port 5173)${NC}"
        else
            echo -e "${RED}✗ Web app is not running${NC}"
        fi
        
        echo -e "\n${YELLOW}All screen sessions:${NC}"
        screen -ls
        ;;
        
    logs)
        case "$2" in
            device|device-server)
                screen -r device-server
                ;;
            web|web-app)
                screen -r web-app
                ;;
            *)
                echo "Usage: $0 logs [device|web]"
                ;;
        esac
        ;;
        
    docker)
        case "$2" in
            start)
                docker compose -f docker-compose.dev.yml up -d
                ;;
            stop)
                docker compose -f docker-compose.dev.yml down
                ;;
            logs)
                docker compose -f docker-compose.dev.yml logs -f
                ;;
            *)
                echo "Usage: $0 docker {start|stop|logs}"
                ;;
        esac
        ;;
        
    *)
        echo "Usage: $0 {start|stop|status|logs|docker}"
        echo "  start  - Start all development services (Docker + Apps)"
        echo "  stop   - Stop all development services"
        echo "  status - Show service status"
        echo "  logs [device|web] - View application logs"
        echo "  docker {start|stop|logs} - Manage Docker services"
        ;;
esac

