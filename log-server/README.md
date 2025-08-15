# RapidReach Log Server

A centralized HTTP REST API log server for collecting and storing logs from various sources in the RapidReach project.

## Features

- HTTP REST API for log ingestion
- Batch log submission support
- Automatic log rotation (daily)
- Log compression for old files
- Multiple log sources with separate files
- Query logs by source and date
- Docker support
- Simple client library

## Quick Start

### Using Docker Compose

```bash
docker-compose up -d
```

The server will be available at `http://localhost:3001`

### Manual Installation

```bash
npm install
npm start
```

## API Endpoints

### Health Check
```
GET /health
```

### Submit Log Batch
```
POST /logs
Content-Type: application/json

{
  "source": "device-001",
  "logs": [
    {
      "timestamp": "2024-01-01T12:00:00Z",
      "level": "info",
      "message": "Device started",
      "deviceId": "313938343233510e003d0029"
    }
  ]
}
```

### Submit Single Log
```
POST /log
Content-Type: application/json

{
  "source": "mqtt-terminal",
  "level": "error",
  "message": "Connection failed",
  "error": "ETIMEDOUT"
}
```

### Query Logs
```
GET /logs/{source}?date=2024-01-01&limit=100
```

### List Available Sources
```
GET /sources
```

## Client Integration

### Node.js Example

```javascript
import { NodeLogClient } from './client.js';

const logger = new NodeLogClient({
  source: 'mqtt-terminal',
  baseUrl: 'http://localhost:3001',
  batchSize: 50,
  flushInterval: 5000
});

// Use the logger
logger.info('Application started');
logger.error('Connection failed', { error: 'timeout' });

// Ensure logs are flushed before exit
process.on('exit', () => logger.close());
```

### Device Integration (C/Zephyr)

For embedded devices, you can send logs via HTTP POST:

```c
// Example pseudo-code for Zephyr
struct log_entry {
    char timestamp[32];
    char level[16];
    char message[256];
};

void send_log_batch(struct log_entry *logs, int count) {
    // Create JSON payload
    // Send HTTP POST to http://192.168.2.62:3001/logs
}
```

## Environment Variables

- `PORT`: Server port (default: 3000)
- `LOG_DIR`: Directory for log files (default: ./logs)

## Log Files

Logs are stored in the following format:
- `{source}-{YYYY-MM-DD}.log`
- Compressed after rotation
- Kept for 14 days by default

## Security Considerations

- Use behind a reverse proxy in production
- Add authentication if exposed to internet
- Consider rate limiting for public deployments
