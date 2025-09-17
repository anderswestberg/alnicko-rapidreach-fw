import winston from 'winston';
import config from '../config/index.js';
import { getCollection } from '../db/mongo.js';

const mongoSink = winston.format((info) => {
  try {
    // Attempt to write to MongoDB if initialized
    const col = getCollection('logs');
    // Non-blocking insert; ignore result
    const now = new Date();
    const doc: any = {
      ts: now,
      timestamp: now, // React Admin expects 'timestamp'
      level: info.level,
      message: info.message,
      device: 'device-server', // Use 'device' for server/web-app distinction
      source: info.source || info.service || 'device-server', // Module within device
    };
    // Merge extra meta fields
    for (const k of Object.keys(info)) {
      if (!['level', 'message', 'timestamp', 'service'].includes(k)) {
        doc[k] = info[k];
      }
    }
    // Fire and forget
    // eslint-disable-next-line @typescript-eslint/no-floating-promises
    col.insertOne(doc).catch(() => {});
  } catch (_e) {
    // DB not initialized yet or other transient issue; skip
  }
  return info;
});

const logger = winston.createLogger({
  level: config.logging.level,
  format: winston.format.combine(
    winston.format.timestamp(),
    mongoSink(),
    winston.format.errors({ stack: true }),
    winston.format.json()
  ),
  defaultMeta: { service: 'device-server' },
  transports: [
    new winston.transports.Console({
      format: winston.format.combine(
        winston.format.colorize(),
        winston.format.simple()
      ),
    }),
  ],
});

export default logger;
