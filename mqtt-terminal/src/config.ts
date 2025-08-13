import dotenv from 'dotenv';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Load environment variables
dotenv.config({ path: join(__dirname, '../.env') });

export const config = {
  mqtt: {
    brokerHost: process.env.MQTT_BROKER_HOST || '192.168.2.62',
    brokerPort: parseInt(process.env.MQTT_BROKER_PORT || '1883'),
    username: process.env.MQTT_USERNAME || '',
    password: process.env.MQTT_PASSWORD || '',
    clientId: `mqtt-terminal-${Date.now()}`,
    connectTimeout: 10000,
    reconnectPeriod: 5000,
  },
  device: {
    id: process.env.DEVICE_ID || 'rapidreach_device',
    commandTopic: (deviceId: string) => `rapidreach/${deviceId}/cli/command`,
    responseTopic: (deviceId: string) => `rapidreach/${deviceId}/cli/response`,
  },
  terminal: {
    prompt: process.env.TERMINAL_PROMPT || 'rapidreach> ',
    responseTimeout: parseInt(process.env.RESPONSE_TIMEOUT || '5000'),
  },
};
