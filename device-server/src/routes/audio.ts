import { Router, Request, Response } from 'express';
import { z } from 'zod';
import { DeviceMqttClient } from '../services/mqtt-client.js';
import logger from '../utils/logger.js';
import multer from 'multer';
import { promises as fs } from 'fs';
import { exec } from 'child_process';
import { promisify } from 'util';
import path from 'path';
import { randomUUID } from 'crypto';

const execAsync = promisify(exec);

// Configure multer for file uploads
const upload = multer({
  dest: '/tmp/audio-uploads/',
  limits: {
    fileSize: 10 * 1024 * 1024, // 10MB max
  },
  fileFilter: (_req, file, cb) => {
    // Accept common audio formats
    const allowedMimes = [
      'audio/mpeg',
      'audio/mp3',
      'audio/wav',
      'audio/wave',
      'audio/x-wav',
      'audio/ogg',
      'audio/opus',
      'audio/flac',
      'audio/aac',
      'audio/m4a',
      'audio/x-m4a',
    ];
    if (allowedMimes.includes(file.mimetype)) {
      cb(null, true);
    } else {
      cb(new Error('Invalid file type. Only audio files are allowed.'));
    }
  },
});

// Request validation schemas (accept string numerics; make volume required, no default)
const numberFromString = (schema: z.ZodNumber, fallback?: number) =>
  z.preprocess((v) => {
    if (typeof v === 'string' && v.trim() !== '') return Number(v);
    if (typeof v === 'number') return v;
    return fallback !== undefined ? fallback : v;
  }, schema);

const booleanFromString = (schema: z.ZodBoolean, fallback?: boolean) =>
  z.preprocess((v) => {
    if (typeof v === 'string') return v === 'true';
    if (typeof v === 'boolean') return v;
    return fallback !== undefined ? fallback : v;
  }, schema);

const audioAlertSchema = z.object({
  deviceId: z.string().min(1),
  priority: numberFromString(z.number().min(0).max(255), 5).default(5),
  volume: numberFromString(z.number().min(0).max(100), 25).default(25),
  playCount: numberFromString(z.number().min(0), 1).default(1),
  interruptCurrent: booleanFromString(z.boolean(), false).default(false),
  saveToFile: booleanFromString(z.boolean(), false).default(false),
  filename: z.string().optional(),
});

export function createAudioRoutes(mqttClient: DeviceMqttClient): Router {
  const router = Router();

  /**
   * List saved audio files
   * GET /api/audio/library
   */
  router.get('/audio/library', async (_req: Request, res: Response) => {
    try {
      await fs.mkdir('/tmp/audio-library', { recursive: true });
      const files = await fs.readdir('/tmp/audio-library');
      
      const fileDetails = await Promise.all(
        files.map(async (filename) => {
          const stats = await fs.stat(`/tmp/audio-library/${filename}`);
          return {
            filename,
            size: stats.size,
            modified: stats.mtime,
          };
        })
      );
      
      return res.json({
        success: true,
        files: fileDetails,
      });
    } catch (error) {
      return res.status(500).json({
        success: false,
        error: 'Failed to list audio library',
      });
    }
  });

  /**
   * Re-send audio from library
   * POST /api/audio/library/send
   */
  router.post('/audio/library/send', async (req: Request, res: Response) => {
    try {
      const { deviceId, filename, volume, priority, playCount, interruptCurrent } = req.body;
      
      if (!deviceId || !filename) {
        return res.status(400).json({
          success: false,
          error: 'deviceId and filename required',
        });
      }

      const device = mqttClient.getDevice(deviceId);
      if (!device) {
        return res.status(404).json({
          success: false,
          error: 'Device not found',
        });
      }

      // Read from library
      const libraryPath = `/tmp/audio-library/${filename}`;
      const opusData = await fs.readFile(libraryPath);
      
      // Prepare MQTT message
      const metadata = {
        opusDataSize: opusData.length,
        priority: priority || 5,
        saveToFile: false,
        filename: filename,
        playCount: playCount || 1,
        volume: volume || 25,
        interruptCurrent: interruptCurrent || false,
      };

      const jsonHeader = JSON.stringify(metadata);
      const jsonBuffer = Buffer.from(jsonHeader, 'utf-8');
      const lengthHeader = jsonBuffer.length.toString(16).padStart(4, '0');
      const lengthBuffer = Buffer.from(lengthHeader, 'ascii');
      const mqttPayload = Buffer.concat([lengthBuffer, jsonBuffer, opusData]);

      // Use deviceId (short ID like "373334") for audio topic
      // Device firmware subscribes using short ID extracted from hardware ID
      const topic = `rapidreach/audio/${deviceId}`;
      
      await mqttClient.publish(topic, mqttPayload);
      
      return res.json({
        success: true,
        message: 'Audio sent from library',
        filename,
        deviceId,
      });
      
    } catch (error) {
      return res.status(500).json({
        success: false,
        error: 'Failed to send from library',
      });
    }
  });

  /**
   * Send test ping to device
   * POST /api/audio/ping
   */
  router.post('/audio/ping', async (req: Request, res: Response) => {
    try {
      const { deviceId } = req.body;
      
      if (!deviceId) {
        return res.status(400).json({
          success: false,
          error: 'deviceId required',
        });
      }

      const device = mqttClient.getDevice(deviceId);
      if (!device) {
        return res.status(404).json({
          success: false,
          error: 'Device not found',
        });
      }

      // Use hardware ID for topic if available
      // Use deviceId (short ID like "373334") for audio topic
      // Device firmware subscribes using short ID extracted from hardware ID
      const topic = `rapidreach/audio/${deviceId}`;
      
      // Send tiny test message: [4-byte len][JSON]
      const testJson = JSON.stringify({ test: 'ping', timestamp: Date.now() });
      const lengthHeader = testJson.length.toString(16).padStart(4, '0');
      const payload = Buffer.concat([
        Buffer.from(lengthHeader, 'ascii'),
        Buffer.from(testJson, 'utf-8')
      ]);
      
      await mqttClient.publish(topic, payload);
      
      return res.json({
        success: true,
        message: 'Test ping sent',
        details: {
          deviceId,
          topic,
          payloadSize: payload.length,
        },
      });
    } catch (error) {
      return res.status(500).json({
        success: false,
        error: 'Failed to send ping',
      });
    }
  });

  /**
   * Upload and send audio alert to device
   * POST /api/audio/alert
   */
  router.post('/audio/alert', upload.single('audio'), async (req: Request, res: Response) => {
    let tempFiles: string[] = [];
    
    try {
      if (!req.file) {
        return res.status(400).json({
          success: false,
          error: 'No audio file provided',
        });
      }

      tempFiles.push(req.file.path);

      // Parse metadata from JSON or form-data; numeric/boolean strings handled by schema
      const parsedMetadata = (() => {
        try {
          return req.body.metadata ? JSON.parse(req.body.metadata) : req.body;
        } catch {
          return req.body;
        }
      })();

      // Parse and validate request body
      const validation = audioAlertSchema.safeParse(parsedMetadata);
      if (!validation.success) {
        return res.status(400).json({
          success: false,
          error: 'Invalid request parameters',
          details: validation.error.errors,
        });
      }

      const params = validation.data;
      const { deviceId } = params;

      // Check if device exists and is connected
      const device = mqttClient.getDevice(deviceId);
      if (!device) {
        logger.warn(`Device not found in MQTT client: ${deviceId}`);
        // List all registered devices for debugging
        const allDevices = mqttClient.getDevices();
        logger.debug(`Registered devices: ${allDevices.map(d => d.id).join(', ')}`);
        return res.status(404).json({
          success: false,
          error: 'Device not found',
        });
      }

      if (device.status !== 'online') {
        return res.status(503).json({
          success: false,
          error: 'Device is offline',
        });
      }

      logger.info(`Processing audio upload for device ${deviceId}`, {
        originalName: req.file.originalname,
        mimetype: req.file.mimetype,
        size: req.file.size,
        volume: params.volume,
        priority: params.priority,
        playCount: params.playCount,
        interruptCurrent: params.interruptCurrent,
        saveToFile: params.saveToFile,
      });

      // Generate output filename
      const outputId = randomUUID();
      const outputPath = `/tmp/audio-uploads/${outputId}.opus`;
      tempFiles.push(outputPath);

      // Convert to Opus format using ffmpeg
      // -i input: Input file
      // -c:a libopus: Use Opus codec
      // -b:a 32k: Bitrate (32kbps is good for speech, 64-128k for music)
      // -vbr on: Variable bitrate for better quality
      // -compression_level 10: Best compression
      // -application voip: Optimize for speech (use 'audio' for music)
      // -ac 1: Convert to mono (saves bandwidth)
      // -ar 48000: Sample rate 48kHz (matches device firmware I2S/decoder config)
      // Convert to Opus format - OGG container
      const ffmpegCommand = `ffmpeg -i "${req.file.path}" -c:a libopus -b:a 32k -vbr on -compression_level 10 -application voip -ac 1 -ar 48000 -f opus "${outputPath}" -y`;

      logger.debug(`Executing: ${ffmpegCommand}`);

      try {
        const { stderr } = await execAsync(ffmpegCommand);
        if (stderr && !stderr.includes('Qavg:')) { // ffmpeg writes progress to stderr
          logger.warn('ffmpeg stderr:', stderr);
        }
        logger.debug('ffmpeg conversion completed successfully');
      } catch (error) {
        logger.error('ffmpeg conversion failed:', error);
        return res.status(500).json({
          success: false,
          error: 'Failed to convert audio to Opus format',
          details: error instanceof Error ? error.message : 'Unknown error',
        });
      }

      // Read the encoded Opus file
      const opusData = await fs.readFile(outputPath);
      logger.info(`Opus file created: ${opusData.length} bytes`);
      
      // Save to audio library for re-use
      const libraryPath = `/tmp/audio-library/${req.file.originalname.replace(/\.[^.]+$/, '.opus')}`;
      await fs.mkdir('/tmp/audio-library', { recursive: true });
      await fs.copyFile(outputPath, libraryPath);
      logger.debug(`Saved to audio library: ${libraryPath}`);

      // Prepare MQTT message with JSON header + Opus data
      const metadata = {
        opusDataSize: opusData.length,
        priority: params.priority,
        saveToFile: params.saveToFile,
        filename: params.filename || `alert_${Date.now()}.opus`,
        playCount: params.playCount,
        volume: params.volume,
        interruptCurrent: params.interruptCurrent,
      };

      const jsonHeader = JSON.stringify(metadata);
      const jsonBuffer = Buffer.from(jsonHeader, 'utf-8');
      
      // Prepend 4-byte hex length header for reliable parsing
      const lengthHeader = jsonBuffer.length.toString(16).padStart(4, '0');
      const lengthBuffer = Buffer.from(lengthHeader, 'ascii');
      
      // Combine: [4-byte length][JSON header][Opus data]
      const mqttPayload = Buffer.concat([lengthBuffer, jsonBuffer, opusData]);

      // Use deviceId (short ID like "373334") for audio topic
      // Device firmware subscribes using short ID extracted from hardware ID
      const hwId = device.metadata?.hwId;  // For logging only
      const topic = `rapidreach/audio/${deviceId}`;
      
      logger.info(`Publishing audio alert to ${topic}`, {
        deviceId,
        hwId,
        jsonSize: jsonBuffer.length,
        opusSize: opusData.length,
        totalSize: mqttPayload.length,
        volume: params.volume,
        priority: params.priority,
        playCount: params.playCount,
        interruptCurrent: params.interruptCurrent,
        saveToFile: params.saveToFile,
        filename: params.filename,
        originalFile: req.file.originalname,
      });

      try {
        await mqttClient.publish(topic, mqttPayload);
        // Also persist a log entry in Mongo for traceability
        try {
          const col = (await import('../db/mongo.js')).getCollection('logs');
          await col.insertOne({
            timestamp: new Date(),
            device: 'device-server',
            source: 'audio',
            level: 'info',
            message: `Audio alert published - Volume: ${params.volume}%, Priority: ${params.priority}, PlayCount: ${params.playCount}`,
            deviceId,
            hwId,
            volume: params.volume,
            priority: params.priority,
            playCount: params.playCount,
            interruptCurrent: params.interruptCurrent,
            saveToFile: params.saveToFile,
            filename: params.filename,
            originalFile: req.file.originalname,
            opusSize: opusData.length,
            jsonSize: jsonBuffer.length,
            topic,
          });
        } catch (_e) {
          // Ignore DB errors here; main path already succeeded
        }
        
        return res.json({
          success: true,
          message: 'Audio alert sent successfully',
          details: {
            deviceId,
            topic,
            originalFile: req.file.originalname,
            opusSize: opusData.length,
            metadata,
          },
        });
      } catch (error) {
        logger.error('Failed to publish MQTT message:', error);
        return res.status(500).json({
          success: false,
          error: 'Failed to send audio alert',
          details: error instanceof Error ? error.message : 'Unknown error',
        });
      }

    } catch (error) {
      logger.error('Error processing audio alert:', error);
      return res.status(500).json({
        success: false,
        error: 'Internal server error',
        details: error instanceof Error ? error.message : 'Unknown error',
      });
    } finally {
      // Clean up temporary files
      for (const file of tempFiles) {
        try {
          await fs.unlink(file);
        } catch (err) {
          logger.warn(`Failed to delete temp file ${file}:`, err);
        }
      }
    }
  });

  /**
   * Send pre-encoded Opus audio to device
   * POST /api/audio/opus
   */
  router.post('/audio/opus', upload.single('opus'), async (req: Request, res: Response) => {
    let tempFile: string | null = null;
    
    try {
      if (!req.file) {
        return res.status(400).json({
          success: false,
          error: 'No Opus file provided',
        });
      }

      tempFile = req.file.path;

      // Validate that it's actually an Opus file
      if (!req.file.mimetype.includes('opus') && !req.file.originalname.endsWith('.opus')) {
        return res.status(400).json({
          success: false,
          error: 'File must be in Opus format',
        });
      }

      // Parse metadata from JSON or form-data; numeric/boolean strings handled by schema
      const parsedMetadata = (() => {
        try {
          return req.body.metadata ? JSON.parse(req.body.metadata) : req.body;
        } catch {
          return req.body;
        }
      })();

      const validation = audioAlertSchema.safeParse(parsedMetadata);
      if (!validation.success) {
        return res.status(400).json({
          success: false,
          error: 'Invalid request parameters',
          details: validation.error.errors,
        });
      }

      const params = validation.data;
      const { deviceId } = params;

      // Check device
      const device = mqttClient.getDevice(deviceId);
      if (!device) {
        return res.status(404).json({
          success: false,
          error: 'Device not found',
        });
      }

      if (device.status !== 'online') {
        return res.status(503).json({
          success: false,
          error: 'Device is offline',
        });
      }

      // Read Opus data
      const opusData = await fs.readFile(req.file.path);

      // Prepare MQTT message
      const metadata = {
        opusDataSize: opusData.length,
        priority: params.priority,
        saveToFile: params.saveToFile,
        filename: params.filename || req.file.originalname,
        playCount: params.playCount,
        volume: params.volume,
        interruptCurrent: params.interruptCurrent,
      };

      const jsonHeader = JSON.stringify(metadata);
      const jsonBuffer = Buffer.from(jsonHeader, 'utf-8');
      
      // Prepend 4-byte hex length header for reliable parsing
      const lengthHeader = jsonBuffer.length.toString(16).padStart(4, '0');
      const lengthBuffer = Buffer.from(lengthHeader, 'ascii');
      
      // Combine: [4-byte length][JSON header][Opus data]
      const mqttPayload = Buffer.concat([lengthBuffer, jsonBuffer, opusData]);

      // Use hardware ID for audio topic if available, fallback to deviceId
      // Use deviceId (short ID like "373334") for audio topic
      // Device firmware subscribes using short ID extracted from hardware ID
      const topic = `rapidreach/audio/${deviceId}`;
      await mqttClient.publish(topic, mqttPayload);

      return res.json({
        success: true,
        message: 'Opus audio sent successfully',
        details: {
          deviceId,
          topic,
          opusSize: opusData.length,
          metadata,
        },
      });

    } catch (error) {
      logger.error('Error sending Opus audio:', error);
      return res.status(500).json({
        success: false,
        error: 'Internal server error',
        details: error instanceof Error ? error.message : 'Unknown error',
      });
    } finally {
      if (tempFile) {
        try {
          await fs.unlink(tempFile);
        } catch (err) {
          logger.warn(`Failed to delete temp file:`, err);
        }
      }
    }
  });

  /**
   * Get audio encoding options
   * GET /api/audio/encoding-options
   */
  router.get('/audio/encoding-options', (_req: Request, res: Response) => {
    return res.json({
      success: true,
      options: {
        codecs: ['opus'],
        bitrates: {
          speech: '32k',
          music: '128k',
          highQualityMusic: '256k',
        },
        sampleRates: {
          speech: 16000,
          music: 48000,
        },
        applications: {
          voip: 'Optimized for speech',
          audio: 'Optimized for music',
          lowdelay: 'Low latency mode',
        },
        maxFileSize: '10MB',
        supportedFormats: [
          'mp3', 'wav', 'ogg', 'opus', 'flac', 'aac', 'm4a'
        ],
      },
    });
  });

  // Download endpoint for audio files
  router.get('/download/:audioId', async (req, res) => {
    try {
      const { audioId } = req.params;
      const audioPath = path.join('/tmp/audio-cache', `${audioId}.opus`);
      
      // Check if file exists
      try {
        await fs.access(audioPath);
      } catch {
        return res.status(404).json({
          success: false,
          error: 'Audio file not found',
        });
      }
      
      // Send the audio file
      res.setHeader('Content-Type', 'audio/opus');
      res.setHeader('Content-Disposition', `attachment; filename="${audioId}.opus"`);
      res.sendFile(audioPath);
      
      // Clean up old files after 5 minutes
      setTimeout(async () => {
        try {
          await fs.unlink(audioPath);
        } catch {
          // Ignore errors
        }
      }, 5 * 60 * 1000);
      
      return; // Explicitly return for TypeScript
      
    } catch (error) {
      logger.error('Error serving audio file:', error);
      return res.status(500).json({
        success: false,
        error: 'Failed to serve audio file',
      });
    }
  });

  return router;
}
