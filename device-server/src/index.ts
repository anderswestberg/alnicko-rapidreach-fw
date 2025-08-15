import express from 'express';
import cors from 'cors';
import config from './config/index.js';
import logger from './utils/logger.js';
import { DeviceMqttClient } from './services/mqtt-client.js';
import { createDeviceRoutes } from './routes/devices.js';
import { createDataProviderRoutes } from './routes/dataprovider.js';
import { authMiddleware } from './middleware/auth.js';
import { errorHandler } from './middleware/error.js';

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
  
  // Wait for MQTT connection
  await new Promise<void>((resolve) => {
    mqttClient.once('connected', () => {
      logger.info('MQTT client ready');
      resolve();
    });
  });

  // API routes (with auth)
  app.use('/api', authMiddleware, createDeviceRoutes(mqttClient));
  app.use('/api', authMiddleware, createDataProviderRoutes(mqttClient));

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
    process.exit(0);
  });

  process.on('SIGINT', async () => {
    logger.info('SIGINT received, shutting down gracefully');
    
    server.close(() => {
      logger.info('HTTP server closed');
    });

    await mqttClient.disconnect();
    process.exit(0);
  });
}

// Start the server
startServer().catch((error) => {
  logger.error('Failed to start server:', error);
  process.exit(1);
});
