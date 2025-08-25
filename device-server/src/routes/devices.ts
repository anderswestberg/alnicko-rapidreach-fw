import { Router, Request, Response } from 'express';
import { z } from 'zod';
import { DeviceMqttClient } from '../services/mqtt-client.js';
import logger from '../utils/logger.js';
import { getCollection } from '../db/mongo.js';
import { ObjectId } from 'mongodb';

// Request validation schemas
const executeCommandSchema = z.object({
  command: z.string().min(1).max(1000),
  timeout: z.number().min(100).max(30000).optional(),
});

export function createDeviceRoutes(mqttClient: DeviceMqttClient): Router {
  const router = Router();

  // Get device statistics
  router.get('/devices/stats', (_req: Request, res: Response) => {
    try {
      const devices = mqttClient.getDevices();
      const connectedCount = mqttClient.getConnectedDevicesCount();
      
      // Calculate device types
      const deviceTypes = devices.reduce((acc, device) => {
        acc[device.type] = (acc[device.type] || 0) + 1;
        return acc;
      }, {} as Record<string, number>);
      
      res.json({
        success: true,
        stats: {
          total: devices.length,
          connected: connectedCount,
          offline: devices.length - connectedCount,
          byType: deviceTypes,
          timestamp: new Date(),
        },
      });
    } catch (error) {
      logger.error('Error getting device stats:', error);
      res.status(500).json({
        success: false,
        error: 'Internal server error',
      });
    }
  });

  // Get all devices
  router.get('/devices', (_req: Request, res: Response) => {
    try {
      const devices = mqttClient.getDevices();
      res.json({
        success: true,
        devices,
        count: devices.length,
      });
    } catch (error) {
      logger.error('Error getting devices:', error);
      res.status(500).json({
        success: false,
        error: 'Internal server error',
      });
    }
  });

  // Get specific device
  router.get('/devices/:deviceId', (req: Request, res: Response) => {
    try {
      const { deviceId } = req.params;
      const device = mqttClient.getDevice(deviceId);
      
      if (!device) {
        res.status(404).json({
          success: false,
          error: 'Device not found',
        });
        return;
      }

      res.json({
        success: true,
        device,
      });
    } catch (error) {
      logger.error('Error getting device:', error);
      res.status(500).json({
        success: false,
        error: 'Internal server error',
      });
    }
  });

  // Execute command on device
  router.post('/devices/:deviceId/execute', async (req: Request, res: Response) => {
    try {
      const { deviceId } = req.params;
      const validation = executeCommandSchema.safeParse(req.body);
      
      if (!validation.success) {
        res.status(400).json({
          success: false,
          error: 'Invalid request',
          details: validation.error.errors,
        });
        return;
      }

      // Look up the device in MongoDB to get its clientId
      const devicesCollection = getCollection('devices');
      let device: any;
      
      // Try to parse as ObjectId if it looks like one
      if (ObjectId.isValid(deviceId) && deviceId.length === 24) {
        device = await devicesCollection.findOne({ _id: new ObjectId(deviceId) });
      } else {
        device = await devicesCollection.findOne({ $or: [{ clientId: deviceId }, { id: deviceId }] });
      }
      
      if (!device || !device.clientId) {
        res.status(404).json({
          success: false,
          error: 'Device not found or missing clientId',
        });
        return;
      }

      const { command, timeout = 5000 } = validation.data;
      const startTime = Date.now();

      logger.info(`Executing command on device ${device.clientId}: ${command}`);

      try {
        // Use the clientId + "-shell" for MQTT shell commands
        const shellClientId = device.clientId.endsWith('-shell') ? device.clientId : `${device.clientId.split('-')[0]}-shell`;
        const output = await mqttClient.sendCommand(shellClientId, command, timeout);
        const executionTime = Date.now() - startTime;

        res.json({
          success: true,
          deviceId: device.clientId,
          command,
          output,
          timestamp: new Date(),
          executionTime,
        });
      } catch (error) {
        const executionTime = Date.now() - startTime;
        
        if (error instanceof Error && error.message.includes('timeout')) {
          res.status(408).json({
            success: false,
            error: 'Command timeout',
            deviceId,
            command,
            executionTime,
          });
          return;
        }

        throw error;
      }
    } catch (error) {
      logger.error('Error executing command:', error);
      res.status(500).json({
        success: false,
        error: 'Internal server error',
      });
    }
  });

  // Batch execute commands
  router.post('/devices/batch/execute', async (req: Request, res: Response) => {
    try {
      const batchSchema = z.array(z.object({
        deviceId: z.string(),
        command: z.string(),
        timeout: z.number().optional(),
      }));

      const validation = batchSchema.safeParse(req.body);
      
      if (!validation.success) {
        res.status(400).json({
          success: false,
          error: 'Invalid request',
          details: validation.error.errors,
        });
        return;
      }

      const commands = validation.data;
      const results = await Promise.allSettled(
        commands.map(async ({ deviceId, command, timeout }) => {
          const startTime = Date.now();
          try {
            const output = await mqttClient.sendCommand(deviceId, command, timeout || 5000);
            return {
              success: true,
              deviceId,
              command,
              output,
              executionTime: Date.now() - startTime,
            };
          } catch (error) {
            return {
              success: false,
              deviceId,
              command,
              error: error instanceof Error ? error.message : 'Unknown error',
              executionTime: Date.now() - startTime,
            };
          }
        })
      );

      const responses = results.map(result => 
        result.status === 'fulfilled' ? result.value : result.reason
      );

      res.json({
        success: true,
        results: responses,
        summary: {
          total: responses.length,
          successful: responses.filter(r => r.success).length,
          failed: responses.filter(r => !r.success).length,
        },
      });
    } catch (error) {
      logger.error('Error in batch execute:', error);
      res.status(500).json({
        success: false,
        error: 'Internal server error',
      });
    }
  });

  return router;
}
