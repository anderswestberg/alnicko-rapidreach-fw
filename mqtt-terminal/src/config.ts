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
      // Use only first 6 characters of device ID for topic
      const shortId = deviceId.substring(0, 6);
      return `devices/${shortId}/rx`;
    },
    responseTopic: (deviceId: string) => {
      // Use only first 6 characters of device ID for topic
      const shortId = deviceId.substring(0, 6);
      return `devices/${shortId}/tx`;
    },
  },
  terminal: {
    prompt: process.env.TERMINAL_PROMPT || '> ',  // Will be prefixed with mqtt-{deviceId}:~$
    responseTimeout: parseInt(process.env.RESPONSE_TIMEOUT || '5000'),
  },
};
