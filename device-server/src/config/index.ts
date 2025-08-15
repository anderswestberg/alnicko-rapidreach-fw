import { z } from 'zod';
import dotenv from 'dotenv';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Load environment variables
dotenv.config({ path: join(__dirname, '../../.env') });

// Configuration schema
const configSchema = z.object({
  server: z.object({
    port: z.number().min(1).max(65535),
    nodeEnv: z.enum(['development', 'production', 'test']),
  }),
  mqtt: z.object({
    brokerHost: z.string(),
    brokerPort: z.number(),
    username: z.string().optional(),
    password: z.string().optional(),
    connectTimeout: z.number(),
    reconnectPeriod: z.number(),
  }),
  api: z.object({
    apiKey: z.string().optional(),
  }),
  logging: z.object({
    level: z.enum(['error', 'warn', 'info', 'debug']),
  }),
});

// Parse and validate configuration
const config = configSchema.parse({
  server: {
    port: parseInt(process.env.PORT || '3002', 10),
    nodeEnv: process.env.NODE_ENV || 'development',
  },
  mqtt: {
    brokerHost: process.env.MQTT_BROKER_HOST || 'localhost',
    brokerPort: parseInt(process.env.MQTT_BROKER_PORT || '1883', 10),
    username: process.env.MQTT_USERNAME,
    password: process.env.MQTT_PASSWORD,
    connectTimeout: 10000,
    reconnectPeriod: 5000,
  },
  api: {
    apiKey: process.env.API_KEY,
  },
  logging: {
    level: (process.env.LOG_LEVEL as any) || 'info',
  },
});

export type Config = z.infer<typeof configSchema>;
export default config;
