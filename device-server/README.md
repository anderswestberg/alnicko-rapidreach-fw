# RapidReach Device Server

A Node.js TypeScript server that provides a REST API interface for controlling RapidReach IoT devices via MQTT.

## Features

- 🚀 REST API for device command execution
- 📡 MQTT client for device communication
- 🔒 API key authentication
- 📊 Device status tracking
- 🔄 Automatic reconnection
- 📝 Comprehensive logging
- ⚡ Batch command execution

## Installation

```bash
cd device-server
npm install
```

## Configuration

1. Copy the example environment file:
```bash
cp env.example .env
```

2. Edit `.env` with your configuration:
```env
# Server
PORT=3002

# MQTT
MQTT_BROKER_HOST=192.168.2.62
MQTT_BROKER_PORT=1883
MQTT_USERNAME=admin
MQTT_PASSWORD=public

# API Security (optional in development)
API_KEY=your-secret-key
```

## Usage

### Development
```bash
npm run dev
```

### Production
```bash
npm run build
npm start
```

## API Endpoints

### Health Check
```http
GET /health
```

### List All Devices
```http
GET /api/devices
X-API-Key: your-api-key
```

Response:
```json
{
  "success": true,
  "devices": [
    {
      "id": "313938",
      "type": "speaker",
      "status": "online",
      "lastSeen": "2024-01-15T10:30:00Z"
    }
  ],
  "count": 1
}
```

### Get Device Info
```http
GET /api/devices/:deviceId
X-API-Key: your-api-key
```

### Execute Command
```http
POST /api/devices/:deviceId/execute
X-API-Key: your-api-key
Content-Type: application/json

{
  "command": "app led on 0",
  "timeout": 5000
}
```

Response:
```json
{
  "success": true,
  "deviceId": "313938",
  "command": "app led on 0",
  "output": "LED 0 ON\n",
  "timestamp": "2024-01-15T10:30:00Z",
  "executionTime": 245
}
```

### Batch Execute Commands
```http
POST /api/devices/batch/execute
X-API-Key: your-api-key
Content-Type: application/json

[
  {
    "deviceId": "313938",
    "command": "app led on 0",
    "timeout": 5000
  },
  {
    "deviceId": "313938",
    "command": "app led on 1",
    "timeout": 5000
  }
]
```

## Examples

### Using cURL

```bash
# Execute a command
curl -X POST http://localhost:3002/api/devices/313938/execute \
  -H "X-API-Key: your-api-key" \
  -H "Content-Type: application/json" \
  -d '{"command": "help"}'

# Get device list
curl http://localhost:3002/api/devices \
  -H "X-API-Key: your-api-key"
```

### Using JavaScript/TypeScript

```typescript
const response = await fetch('http://localhost:3002/api/devices/313938/execute', {
  method: 'POST',
  headers: {
    'X-API-Key': 'your-api-key',
    'Content-Type': 'application/json',
  },
  body: JSON.stringify({
    command: 'app battery',
    timeout: 3000,
  }),
});

const result = await response.json();
console.log(result.output);
```

## Device ID Format

- Speaker devices: 6-character hex ID (e.g., `313938`)
- Sensor devices: TBD

## Error Handling

The API returns consistent error responses:

```json
{
  "success": false,
  "error": "Error description",
  "details": ["Additional error details if available"]
}
```

Common HTTP status codes:
- `200` - Success
- `400` - Bad request (invalid parameters)
- `401` - Unauthorized (missing API key)
- `403` - Forbidden (invalid API key)
- `404` - Device not found
- `408` - Command timeout
- `500` - Internal server error

## Development

### Run Tests
```bash
npm test
```

### Lint Code
```bash
npm run lint
```

### Clean Build
```bash
npm run clean
```

## Architecture

```
┌─────────────┐     REST API      ┌──────────────┐
│   Client    │ ◄───────────────► │ Device Server│
└─────────────┘                   └──────┬───────┘
                                         │
                                    MQTT │
                                         │
                               ┌─────────▼────────┐
                               │   MQTT Broker    │
                               └─────────┬────────┘
                                         │
                      ┌──────────────────┴──────────────────┐
                      │                                      │
              ┌───────▼────────┐                   ┌────────▼───────┐
              │ Speaker Device │                   │ Sensor Device  │
              └────────────────┘                   └────────────────┘
```

## License

MIT
