import express from 'express';
import cors from 'cors';
import config from './config/index.js';
import logger from './utils/logger.js';
import { DeviceMqttClient } from './services/mqtt-client.js';
import { createDeviceRoutes } from './routes/devices.js';
import { createDataProviderRoutes } from './routes/dataprovider.js';
import { createAudioRoutes } from './routes/audio.js';
import { authMiddleware } from './middleware/auth.js';
import { errorHandler } from './middleware/error.js';
import { connectMongo, disconnectMongo } from './db/mongo.js';

async function startServer() {
  const app = express();
  
  // Middleware
  app.use(cors());
  app.use(express.json());
  app.use(express.urlencoded({ extended: true }));

  // Health check endpoint (no auth required)
  app.get('/health', (_req, res) => {
    res.json({
      success: true,
      status: 'healthy',
      timestamp: new Date(),
      uptime: process.uptime(),
    });
  });

  // Initialize MQTT client
  const mqttClient = new DeviceMqttClient();
  
  // Initialize MongoDB with timeout
  const mongoUri = process.env.MONGODB_URI || 'mongodb://localhost:27017';
  const mongoDb = process.env.MONGODB_DB || 'rapidreach';
  
  logger.info(`Connecting to MongoDB at ${mongoUri}...`);
  try {
    await Promise.race([
      connectMongo(mongoUri, mongoDb),
      new Promise((_, reject) => 
        setTimeout(() => reject(new Error('MongoDB connection timeout')), 5000)
      )
    ]);
    logger.info('MongoDB connected successfully');
  } catch (error) {
    logger.error('MongoDB connection failed:', error);
    logger.warn('Starting server without MongoDB - some features may not work');
  }

  // Skip waiting for MQTT - it connects asynchronously
  logger.info('MQTT client initialized (connecting in background)');

  // API routes (with auth)
  app.use('/api', authMiddleware, createDeviceRoutes(mqttClient));
  app.use('/api', authMiddleware, createDataProviderRoutes(mqttClient));
  app.use('/api', authMiddleware, createAudioRoutes(mqttClient));

  // Error handling
  app.use(errorHandler);

  // Start server
  const server = app.listen(config.server.port, () => {
    logger.info(`Device server running on port ${config.server.port}`);
    logger.info(`Environment: ${config.server.nodeEnv}`);
  });

  // Graceful shutdown
  process.on('SIGTERM', async () => {
    logger.info('SIGTERM received, shutting down gracefully');
    
    server.close(() => {
      logger.info('HTTP server closed');
    });

    await mqttClient.disconnect();
    await disconnectMongo();
    process.exit(0);
  });

  process.on('SIGINT', async () => {
    logger.info('SIGINT received, shutting down gracefully');
    
    server.close(() => {
      logger.info('HTTP server closed');
    });

    await mqttClient.disconnect();
    await disconnectMongo();
    process.exit(0);
  });
}

// Start the server
startServer().catch((error) => {
  logger.error('Failed to start server:', error);
  process.exit(1);
});
