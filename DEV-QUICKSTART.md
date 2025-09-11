# RapidReach Development Quick Start

This guide helps you set up a development environment with the device-server running on the host for easier debugging, while keeping only the database and MQTT broker in Docker containers.

## Prerequisites

- Docker & Docker Compose
- Node.js (v16 or later)
- Screen (for managing background processes)

## Quick Start

### 1. Initial Setup

Run the setup script to prepare your environment:

```bash
./dev-setup.sh
```

This will:
- Start MQTT broker (EMQX) and MongoDB in Docker containers
- Create necessary directories
- Set up the device-server environment

### 2. Start All Services

Use the helper script to start everything:

```bash
./dev-services.sh start
```

This will:
- Ensure Docker services are running
- Start the device-server on port 3002
- Start the web app on port 5173 (if available)

### 3. Check Service Status

```bash
./dev-services.sh status
```

## Available Services

| Service | URL/Port | Credentials |
|---------|----------|-------------|
| MQTT Broker | localhost:1883 | admin/public |
| MQTT Dashboard | http://localhost:18083 | admin/public |
| MongoDB | localhost:27017 | - |
| Device Server API | http://localhost:3002 | API Key: dev-api-key-12345 |
| Web App | http://localhost:5173 | - |

## Common Commands

### Application Management

```bash
# Start all services
./dev-services.sh start

# Stop application services (asks about Docker)
./dev-services.sh stop

# View device-server logs
./dev-services.sh logs device

# View web-app logs
./dev-services.sh logs web
```

### Docker Services Only

```bash
# Start Docker services
./dev-services.sh docker start
# or
docker compose -f docker-compose.dev.yml up -d

# Stop Docker services
./dev-services.sh docker stop
# or
docker compose -f docker-compose.dev.yml down

# View Docker logs
./dev-services.sh docker logs
# or
docker compose -f docker-compose.dev.yml logs -f
```

### Direct Device Server Control

```bash
cd device-server
npm run dev
```

## Debugging Tips

1. **Device Server Logs**: Check `logs/device-server-debug.log` or attach to the screen session
2. **MQTT Dashboard**: Visit http://localhost:18083 to monitor MQTT activity
3. **MongoDB**: Use MongoDB Compass or `mongosh` to inspect the database

## Testing with Hardware

When testing with actual RapidReach devices:

1. Ensure the device can reach your development machine's IP
2. The device will connect to MQTT on port 1883
3. Use the MQTT Dashboard to monitor device connections and messages

## Troubleshooting

### Web App 403 Forbidden Errors

If you see 403 errors in the browser console when accessing the web app:
```
GET http://localhost:3002/api/devices/stats 403 (Forbidden)
```

This means the web app isn't configured with the correct API key. Fix it by:

1. Ensure the web app has a `.env` file:
```bash
cd web-app
cat .env
# Should show:
# VITE_API_URL=http://localhost:3002/api
# VITE_API_KEY=dev-api-key-12345
```

2. If missing, create it:
```bash
cat > .env << 'EOF'
VITE_API_URL=http://localhost:3002/api
VITE_API_KEY=dev-api-key-12345
EOF
```

3. Restart the web app:
```bash
cd ..
./dev-services.sh stop
./dev-services.sh start
```

### Port Already in Use

If you get port conflicts:
```bash
# Check what's using the ports
lsof -i :1883  # MQTT
lsof -i :3002  # Device Server
lsof -i :27017 # MongoDB

# Kill the process if needed
kill -9 <PID>
```

### Device Server Won't Start

1. Check if `.env` file exists in `device-server/`
2. Ensure MongoDB and MQTT are running: `./dev-services.sh status`
3. Check logs: `tail -f logs/device-server-debug.log`

### MQTT Connection Issues

1. Verify MQTT is accessible: `nc -zv localhost 1883`
2. Check credentials in device-server/.env match Docker config
3. Monitor MQTT logs: `docker logs rapidreach-emqx-dev`

## Environment Variables

The device-server uses these environment variables (set in `.env`):

```env
PORT=3002
NODE_ENV=development
MQTT_BROKER_HOST=localhost
MQTT_BROKER_PORT=1883
MQTT_USERNAME=admin
MQTT_PASSWORD=public
MONGODB_URI=mongodb://localhost:27017
MONGODB_DB=rapidreach
API_KEY=dev-api-key-12345
LOG_LEVEL=debug
```

Adjust as needed for your setup.
