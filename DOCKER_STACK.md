# RapidReach Docker Stack

This document describes the complete Docker stack for the RapidReach system.

## Components

### 1. EMQX MQTT Broker
- **Container**: `rapidreach-emqx`
- **Image**: `emqx/emqx:5.3.2`
- **Ports**:
  - `1883`: MQTT TCP
  - `8883`: MQTT SSL
  - `8083`: MQTT WebSocket
  - `8084`: MQTT WSS  
  - `18083`: Dashboard (http://localhost:18083)
- **Credentials**: admin/public

### 2. Log Server
- **Container**: `rapidreach-log-server`
- **Port**: `3001`
- **Purpose**: Centralized logging for all RapidReach components
- **API**: HTTP REST API for log ingestion and retrieval

### 3. Device Server
- **Container**: `rapidreach-device-server`
- **Port**: `3002`
- **Purpose**: HTTP-to-MQTT bridge for device control
- **API Key**: `your-secure-api-key-here` (configure in production)

## Quick Start

### Starting the Stack

```bash
cd log-server
docker compose up -d
```

### Checking Status

```bash
docker compose ps
docker compose logs -f
```

### Testing the Stack

1. **Check EMQX Dashboard**:
   ```
   http://localhost:18083
   Username: admin
   Password: public
   ```

2. **Test Device Server**:
   ```bash
   # Health check
   curl http://localhost:3002/health | jq

   # Execute device command (when device is connected)
   curl -X POST http://localhost:3002/api/devices/313938/execute \
     -H "Content-Type: application/json" \
     -H "X-API-Key: your-secure-api-key-here" \
     -d '{"command": "app led on 0"}' | jq
   ```

3. **Check Log Server**:
   ```bash
   # Health check
   curl http://localhost:3001/health | jq
   
   # View logs
   curl http://localhost:3001/logs | jq
   ```

## Device Server API

### Endpoints

- `GET /health` - Health check (no auth required)
- `GET /api/devices` - List all devices
- `GET /api/devices/:deviceId` - Get device details
- `POST /api/devices/:deviceId/execute` - Execute command on device
- `POST /api/devices/batch/execute` - Execute commands on multiple devices

### Example Usage

```bash
# Turn on LED
curl -X POST http://localhost:3002/api/devices/313938/execute \
  -H "Content-Type: application/json" \
  -H "X-API-Key: your-secure-api-key-here" \
  -d '{"command": "app led on 0"}'

# Get device status
curl -X POST http://localhost:3002/api/devices/313938/execute \
  -H "Content-Type: application/json" \
  -H "X-API-Key: your-secure-api-key-here" \
  -d '{"command": "device id"}'
```

## Environment Variables

### Device Server
- `PORT`: HTTP server port (default: 3002)
- `MQTT_BROKER_HOST`: MQTT broker hostname (default: emqx)
- `MQTT_BROKER_PORT`: MQTT broker port (default: 1883)
- `MQTT_USERNAME`: MQTT username (default: admin)
- `MQTT_PASSWORD`: MQTT password (default: public)
- `API_KEY`: API key for authentication
- `LOG_SERVER_URL`: Log server URL (default: http://log-server:3000)

### Log Server
- `PORT`: HTTP server port (default: 3000)
- `LOG_DIR`: Directory for log storage (default: /app/logs)

## Maintenance

### Viewing Logs
```bash
# All services
docker compose logs -f

# Specific service
docker compose logs -f device-server
```

### Restarting Services
```bash
# Restart all
docker compose restart

# Restart specific service
docker compose restart device-server
```

### Updating Images
```bash
# Rebuild and restart
docker compose build --no-cache
docker compose up -d
```

### Cleanup
```bash
# Stop all services
docker compose down

# Remove volumes (warning: deletes data)
docker compose down -v
```

## Production Considerations

1. **API Key**: Change the default API key in production
2. **MQTT Credentials**: Use strong credentials for MQTT
3. **SSL/TLS**: Enable HTTPS for device server
4. **Persistence**: Ensure volumes are properly backed up
5. **Monitoring**: Add health checks and monitoring
6. **Scaling**: Consider load balancing for multiple instances
