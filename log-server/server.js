import express from 'express';
import cors from 'cors';
import morgan from 'morgan';
import winston from 'winston';
import DailyRotateFile from 'winston-daily-rotate-file';
import compression from 'compression';
import helmet from 'helmet';
import { promises as fs } from 'fs';
import { MongoClient } from 'mongodb';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const app = express();
const PORT = process.env.PORT || 3000;
const LOG_DIR = process.env.LOG_DIR || path.join(__dirname, 'logs');

// Ensure log directory exists
await fs.mkdir(LOG_DIR, { recursive: true });

// Configure Winston logger
const logFormat = winston.format.combine(
  winston.format.timestamp(),
  winston.format.json()
);

// Create different transports for different log sources
const createTransport = (filename) => new DailyRotateFile({
  filename: path.join(LOG_DIR, `${filename}-%DATE%.log`),
  datePattern: 'YYYY-MM-DD',
  zippedArchive: true,
  maxSize: '20m',
  maxFiles: '14d',
  format: logFormat
});

// Main logger for the log server itself
const serverLogger = winston.createLogger({
  format: logFormat,
  transports: [
    new winston.transports.Console({
      format: winston.format.combine(
        winston.format.colorize(),
        winston.format.simple()
      )
    }),
    createTransport('server')
  ]
});

// Create loggers for different sources
const loggers = new Map();

const getLogger = (source) => {
  if (!loggers.has(source)) {
    const logger = winston.createLogger({
      format: logFormat,
      transports: [
        createTransport(source),
        new winston.transports.Console({
          format: winston.format.combine(
            winston.format.colorize(),
            winston.format.printf(({ timestamp, level, message, ...meta }) => {
              return `[${timestamp}] [${source}] ${level}: ${message} ${Object.keys(meta).length ? JSON.stringify(meta) : ''}`;
            })
          )
        })
      ]
    });
    loggers.set(source, logger);
  }
  return loggers.get(source);
};

// Middleware
app.use(helmet());
app.use(compression());
app.use(cors());
app.use(express.json({ limit: '10mb' }));
app.use(morgan('combined', { stream: { write: message => serverLogger.info(message.trim()) } }));

// Health check endpoint
app.get('/health', (req, res) => {
  res.json({ status: 'ok', timestamp: new Date().toISOString() });
});

// Initialize Mongo (optional)
const MONGO_URI = process.env.MONGODB_URI;
const MONGO_DB = process.env.MONGODB_DB || 'rapidreach';
let mongoClient = null;
let mongoDb = null;

async function ensureMongo() {
  if (!MONGO_URI) return null;
  if (mongoDb) return mongoDb;
  mongoClient = new MongoClient(MONGO_URI);
  await mongoClient.connect();
  mongoDb = mongoClient.db(MONGO_DB);
  return mongoDb;
}

// Log batch endpoint
app.post('/logs', async (req, res) => {
  const { source, logs } = req.body;
  
  if (!source) {
    return res.status(400).json({ error: 'Source is required' });
  }
  
  if (!logs || !Array.isArray(logs)) {
    return res.status(400).json({ error: 'Logs must be an array' });
  }
  
  const logger = getLogger(source);
  const db = await ensureMongo();
  const collection = db ? db.collection('logs') : null;
  let processedCount = 0;
  
  for (const log of logs) {
    try {
      const { timestamp, level = 'info', message, ...meta } = log;
      
      // Log with appropriate level
      logger.log({
        level: level.toLowerCase(),
        message,
        timestamp: timestamp || new Date().toISOString(),
        ...meta
      });
      if (collection) {
        await collection.insertOne({
          source,
          level: level.toLowerCase(),
          message,
          timestamp: timestamp ? new Date(timestamp) : new Date(),
          ...meta
        });
      }
      
      processedCount++;
    } catch (error) {
      serverLogger.error('Failed to process log entry', { error: error.message, log });
    }
  }
  
  res.json({ 
    success: true, 
    processed: processedCount,
    total: logs.length 
  });
});

// Single log endpoint
app.post('/log', async (req, res) => {
  const { source, level = 'info', message, ...meta } = req.body;
  
  if (!source || !message) {
    return res.status(400).json({ error: 'Source and message are required' });
  }
  
  const logger = getLogger(source);
  const db = await ensureMongo();
  const collection = db ? db.collection('logs') : null;
  
  logger.log({
    level: level.toLowerCase(),
    message,
    timestamp: new Date().toISOString(),
    ...meta
  });
  if (collection) {
    await collection.insertOne({
      source,
      level: level.toLowerCase(),
      message,
      timestamp: new Date(),
      ...meta
    });
  }
  
  res.json({ success: true });
});

// Query logs endpoint
app.get('/logs/:source', async (req, res) => {
  const { source } = req.params;
  const { date = new Date().toISOString().split('T')[0], limit = 100 } = req.query;
  
  try {
    const logFile = path.join(LOG_DIR, `${source}-${date}.log`);
    
    // Check if file exists
    try {
      await fs.access(logFile);
    } catch {
      return res.json({ logs: [] });
    }
    
    // Read file and parse logs
    const content = await fs.readFile(logFile, 'utf-8');
    const lines = content.trim().split('\n').filter(line => line);
    const logs = lines.slice(-limit).map(line => {
      try {
        return JSON.parse(line);
      } catch {
        return { message: line, parseError: true };
      }
    });
    
    res.json({ logs, count: logs.length, date });
  } catch (error) {
    serverLogger.error('Failed to read logs', { error: error.message, source });
    res.status(500).json({ error: 'Failed to read logs' });
  }
});

// List available log sources
app.get('/sources', async (req, res) => {
  try {
    const files = await fs.readdir(LOG_DIR);
    const sources = new Set();
    
    for (const file of files) {
      if (file.endsWith('.log')) {
        const match = file.match(/^(.+)-\d{4}-\d{2}-\d{2}\.log$/);
        if (match) {
          sources.add(match[1]);
        }
      }
    }
    
    res.json({ sources: Array.from(sources) });
  } catch (error) {
    serverLogger.error('Failed to list sources', { error: error.message });
    res.status(500).json({ error: 'Failed to list sources' });
  }
});

// Error handling
app.use((err, req, res, next) => {
  serverLogger.error('Unhandled error', { error: err.message, stack: err.stack });
  res.status(500).json({ error: 'Internal server error' });
});

// Start server
app.listen(PORT, '0.0.0.0', () => {
  serverLogger.info(`Log server listening on port ${PORT}`);
  serverLogger.info(`Logs directory: ${LOG_DIR}`);
});
