import dotenv from 'dotenv';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Load environment variables
dotenv.config({ path: join(__dirname, '../.env') });

export const config = {
  mqtt: {
    brokerHost: process.env.MQTT_BROKER_HOST || 'localhost',
    brokerPort: parseInt(process.env.MQTT_BROKER_PORT || '1883'),
    username: process.env.MQTT_USERNAME || 'admin',
    password: process.env.MQTT_PASSWORD || 'public',
    clientId: `mqtt-terminal-${Date.now()}`,
    connectTimeout: 10000,
    reconnectPeriod: 5000,
  },
  device: {
    id: process.env.DEVICE_ID || 'rapidreach_device',
    // Zephyr built-in MQTT shell topics: devices/<deviceId>/rx (input), devices/<deviceId>/tx (output)
    commandTopic: (deviceId: string) => {
      // Use deviceId as-is to allow IDs like "313938-shell"
      return `devices/${deviceId}/rx`;
    },
    responseTopic: (deviceId: string) => {
      // Use deviceId as-is to allow IDs like "313938-shell"
      return `devices/${deviceId}/tx`;
    },
  },
  terminal: {
    prompt: process.env.TERMINAL_PROMPT || '> ',  // Will be prefixed with mqtt-{deviceId}:~$
    responseTimeout: parseInt(process.env.RESPONSE_TIMEOUT || '5000'),
  },
};
