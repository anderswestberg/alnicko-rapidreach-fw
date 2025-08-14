#!/bin/bash

# RapidReach Services Management Script

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

usage() {
    echo "Usage: $0 {start|stop|restart|status|logs|clean}"
    echo ""
    echo "Commands:"
    echo "  start   - Start all services"
    echo "  stop    - Stop all services"
    echo "  restart - Restart all services"
    echo "  status  - Show status of all services"
    echo "  logs    - Show logs (optional: service name)"
    echo "  clean   - Stop services and remove volumes"
    exit 1
}

check_docker() {
    if ! command -v docker &> /dev/null; then
        echo -e "${RED}Docker is not installed${NC}"
        exit 1
    fi
    
    if ! docker compose version &> /dev/null; then
        echo -e "${RED}Docker Compose is not installed${NC}"
        exit 1
    fi
}

start_services() {
    echo -e "${GREEN}Starting RapidReach services...${NC}"
    
    # Stop any existing EMQX container that might be running
    if docker ps -a | grep -q "emqx"; then
        echo -e "${YELLOW}Stopping existing EMQX container...${NC}"
        docker stop emqx 2>/dev/null || true
        docker rm emqx 2>/dev/null || true
    fi
    
    docker compose up -d --build
    
    echo -e "${GREEN}Services started!${NC}"
    echo ""
    echo "Access points:"
    echo "  - EMQX Dashboard: http://localhost:18083 (admin/public)"
    echo "  - Log Server API: http://localhost:3001"
    echo "  - MQTT Broker: localhost:1883"
}

stop_services() {
    echo -e "${YELLOW}Stopping RapidReach services...${NC}"
    docker compose down
    echo -e "${GREEN}Services stopped${NC}"
}

restart_services() {
    stop_services
    sleep 2
    start_services
}

show_status() {
    echo -e "${GREEN}RapidReach Services Status:${NC}"
    docker compose ps
}

show_logs() {
    if [ -z "$1" ]; then
        docker compose logs -f
    else
        docker compose logs -f "$1"
    fi
}

clean_all() {
    echo -e "${RED}This will stop all services and remove all data!${NC}"
    read -p "Are you sure? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        docker compose down -v
        rm -rf logs/*
        echo -e "${GREEN}Cleanup complete${NC}"
    else
        echo "Cancelled"
    fi
}

# Main script
check_docker

case "$1" in
    start)
        start_services
        ;;
    stop)
        stop_services
        ;;
    restart)
        restart_services
        ;;
    status)
        show_status
        ;;
    logs)
        show_logs "$2"
        ;;
    clean)
        clean_all
        ;;
    *)
        usage
        ;;
esac
