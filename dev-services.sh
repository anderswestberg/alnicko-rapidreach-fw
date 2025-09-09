#!/bin/bash

# Helper script for managing development services

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

case "$1" in
    start)
        echo -e "${YELLOW}Starting development services...${NC}"
        
        # Start device server
        cd device-server
        if [ ! -f .env ]; then
            cp env.example .env
            echo -e "${GREEN}Created .env file for device server${NC}"
        fi
        screen -dmS device-server bash -c 'npm run dev 2>&1 | tee ../logs/device-server-debug.log'
        echo -e "${GREEN}✓ Device server started on port 3002${NC}"
        
        # Start web app
        cd ../web-app
        screen -dmS web-app bash -c 'npm run dev 2>&1 | tee ../logs/web-app-debug.log'
        echo -e "${GREEN}✓ Web app started on port 5173${NC}"
        
        cd ..
        echo -e "\n${GREEN}All services started!${NC}"
        echo -e "Web App: http://localhost:5173/"
        echo -e "API: http://localhost:3002/"
        ;;
        
    stop)
        echo -e "${YELLOW}Stopping development services...${NC}"
        screen -X -S device-server quit 2>/dev/null && echo -e "${GREEN}✓ Device server stopped${NC}"
        screen -X -S web-app quit 2>/dev/null && echo -e "${GREEN}✓ Web app stopped${NC}"
        ;;
        
    status)
        echo -e "${YELLOW}Service Status:${NC}"
        if screen -ls | grep -q "device-server"; then
            echo -e "${GREEN}✓ Device server is running${NC}"
        else
            echo -e "${RED}✗ Device server is not running${NC}"
        fi
        
        if screen -ls | grep -q "web-app"; then
            echo -e "${GREEN}✓ Web app is running${NC}"
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
        
    *)
        echo "Usage: $0 {start|stop|status|logs}"
        echo "  start  - Start all development services"
        echo "  stop   - Stop all development services"
        echo "  status - Show service status"
        echo "  logs [device|web] - View service logs"
        ;;
esac

