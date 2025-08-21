import mqtt, { MqttClient } from 'mqtt';
import { EventEmitter } from 'events';
import { v4 as uuidv4 } from 'uuid';
import config from '../config/index.js';
import logger from '../utils/logger.js';
import { Device, DeviceMessage } from '../types/device.js';
import { getCollection } from '../db/mongo.js';

export interface MqttClientEvents {
  'device:online': (deviceId: string) => void;
  'device:offline': (deviceId: string) => void;
  'device:message': (message: DeviceMessage) => void;
  'connected': () => void;
  'disconnected': () => void;
}

export class DeviceMqttClient extends EventEmitter {
  private client: MqttClient;
  private devices: Map<string, Device> = new Map();
  private pendingCommands: Map<string, {
    resolve: (value: string) => void;
    reject: (reason: any) => void;
    timeout: NodeJS.Timeout;
  }> = new Map();
  private deviceTimeoutInterval: NodeJS.Timeout | null = null;
  private readonly DEVICE_TIMEOUT_MS = 60000; // Consider device offline after 60 seconds
  // Log batching
  private logBuffers: Map<string, any[]> = new Map();
  private logFlushTimers: Map<string, NodeJS.Timeout> = new Map();
  private readonly LOG_BATCH_DELAY_MS = 500; // wait for more logs for 500ms
  private readonly LOG_BATCH_MAX = 200; // flush when buffer reaches this size

  constructor() {
    super();
    
    const brokerUrl = `mqtt://${config.mqtt.brokerHost}:${config.mqtt.brokerPort}`;
    
    this.client = mqtt.connect(brokerUrl, {
      clientId: `device-server-${uuidv4()}`,
      username: config.mqtt.username,
      password: config.mqtt.password,
      connectTimeout: config.mqtt.connectTimeout,
      reconnectPeriod: config.mqtt.reconnectPeriod,
    });

    this.setupEventHandlers();
    this.startDeviceTimeoutCheck();
  }

  private setupEventHandlers(): void {
    this.client.on('connect', () => {
      logger.info('Connected to MQTT broker');
      this.emit('connected');
      
      // Subscribe to device response topics using wildcard
      // New hierarchical topic format: devices/{deviceId}/tx
      const patterns = [
        'devices/+/tx',            // All device responses
        'rapidreach/+/shell/out',  // For the other MQTT shell pattern
        'rapidreach/heartbeat/+',  // Device heartbeat messages
        'logs/+',                  // Device logs (shortId)
        'rapidreach/logs/+',       // Alternate logs prefix
      ];
      
      patterns.forEach(pattern => {
        this.client.subscribe(pattern, { qos: 1 }, (err, granted) => {
          if (err) {
            logger.error(`Failed to subscribe to ${pattern}:`, err);
          } else {
            logger.info(`Subscribe result for ${pattern}:`, granted);
            // Check if subscription was actually granted
            if (granted && granted.length > 0 && granted[0].qos !== 128) {
              logger.info(`Successfully subscribed to ${pattern} with QoS ${granted[0].qos}`);
            } else {
              logger.warn(`Subscription to ${pattern} was rejected (QoS 128 or empty grant)`);
            }
          }
        });
      });
    });

    this.client.on('disconnect', () => {
      logger.warn('Disconnected from MQTT broker');
      this.emit('disconnected');
    });

    this.client.on('error', (error) => {
      logger.error('MQTT client error:', error);
    });

    this.client.on('message', (topic, payload) => {
      this.handleMessage(topic, payload);
    });
  }

  private handleMessage(topic: string, payload: Buffer): void {
    const message: DeviceMessage = {
      topic,
      payload: payload.toString(),
      timestamp: new Date(),
    };

    // Handle device response messages (format: devices/{deviceId}/tx)
    if (topic.startsWith('devices/') && topic.endsWith('/tx')) {
      const parts = topic.split('/');
      const deviceId = parts[1];
      
      // Check if this is a response to a pending command
      const pendingCommand = this.pendingCommands.get(deviceId);
      if (pendingCommand) {
        clearTimeout(pendingCommand.timeout);
        pendingCommand.resolve(payload.toString());
        this.pendingCommands.delete(deviceId);
      }

      this.emit('device:message', message);
    }

    // Handle device log messages via MQTT with batching
    // Convention: logs/{deviceId} or rapidreach/logs/{deviceId}
    if (topic.startsWith('logs/') || topic.startsWith('rapidreach/logs/')) {
      const parts = topic.split('/');
      const deviceId = topic.startsWith('logs/') ? parts[1] : parts[2];
      const now = new Date();
      const text = payload.toString();
      let items: any[] = [];
      try {
        const parsed = JSON.parse(text);
        if (Array.isArray(parsed)) {
          items = parsed.map((p) => ({
            deviceId,
            source: deviceId,
            level: (p.level || 'info').toLowerCase(),
            message: p.message || JSON.stringify(p),
            timestamp: p.timestamp ? new Date(p.timestamp) : now,
            ...p,
          }));
        } else {
          items = [{
            deviceId,
            source: deviceId,
            level: (parsed.level || 'info').toLowerCase(),
            message: parsed.message || text,
            timestamp: parsed.timestamp ? new Date(parsed.timestamp) : now,
            ...parsed,
          }];
        }
      } catch (_e) {
        // Not JSON, store as simple text log
        items = [{
          deviceId,
          source: deviceId,
          level: 'info',
          message: text,
          timestamp: now,
        }];
      }

      this.enqueueLogs(deviceId, items);
    }

    // Handle device heartbeat messages (format: rapidreach/heartbeat/{clientId})
    if (topic.startsWith('rapidreach/heartbeat/')) {
      const parts = topic.split('/');
      const clientId = parts[2]; // clientId format: NNNNNN-speaker
      
      try {
        const heartbeat = JSON.parse(payload.toString());
        const deviceId = heartbeat.deviceId || clientId;
        
        // Update device as online when we receive heartbeat
        this.updateDeviceStatus(deviceId, 'online', {
          clientId,
          type: clientId.endsWith('-speaker') ? 'speaker' : 'unknown',
          firmwareVersion: heartbeat.version,
          uptime: heartbeat.uptime,
          ipAddress: heartbeat.ip,
        });
      } catch (error) {
        logger.error(`Failed to parse heartbeat from ${clientId}:`, error);
      }
    }
    
    // Handle device status messages
    if (topic.endsWith('/status')) {
      const deviceId = topic.split('/')[0];
      const status = payload.toString();
      
      if (status === 'online') {
        this.updateDeviceStatus(deviceId, 'online');
      } else if (status === 'offline') {
        this.updateDeviceStatus(deviceId, 'offline');
      }
    }
  }

  private enqueueLogs(deviceId: string, items: any[]): void {
    const buf = this.logBuffers.get(deviceId) || [];
    buf.push(...items);
    this.logBuffers.set(deviceId, buf);

    if (buf.length >= this.LOG_BATCH_MAX) {
      this.flushLogs(deviceId);
      return;
    }

    if (!this.logFlushTimers.has(deviceId)) {
      const timer = setTimeout(() => {
        this.flushLogs(deviceId);
      }, this.LOG_BATCH_DELAY_MS);
      this.logFlushTimers.set(deviceId, timer);
    }
  }

  private flushLogs(deviceId: string): void {
    const timer = this.logFlushTimers.get(deviceId);
    if (timer) {
      clearTimeout(timer);
      this.logFlushTimers.delete(deviceId);
    }
    const buf = this.logBuffers.get(deviceId) || [];
    if (buf.length === 0) return;
    this.logBuffers.set(deviceId, []);

    try {
      const col = getCollection('logs');
      col.insertMany(buf).catch(err => logger.error('Mongo insertMany logs failed', { err }));
    } catch (_e) {
      // ignore if Mongo is not configured
    }
  }

  private updateDeviceStatus(
    deviceId: string, 
    status: 'online' | 'offline',
    metadata?: {
      clientId?: string;
      type?: 'speaker' | 'sensor' | 'unknown';
      firmwareVersion?: string;
      uptime?: number;
      ipAddress?: string;
    }
  ): void {
    let device = this.devices.get(deviceId);
    
    if (!device) {
      device = {
        id: deviceId,
        type: metadata?.type || 'speaker',
        status,
        lastSeen: new Date(),
        metadata: metadata ? {
          clientId: metadata.clientId,
          firmwareVersion: metadata.firmwareVersion,
          uptime: metadata.uptime,
          ipAddress: metadata.ipAddress,
        } : undefined,
      };
      this.devices.set(deviceId, device);
    } else {
      device.status = status;
      device.lastSeen = new Date();
      
      // Update metadata if provided
      if (metadata) {
        device.metadata = {
          ...device.metadata,
          ...metadata,
        };
      }
    }

    this.emit(`device:${status}`, deviceId);
    logger.info(`Device ${deviceId} is ${status}`, metadata || {});

    // Upsert into MongoDB devices collection
    try {
      const col = getCollection('devices');
      const doc: any = {
        id: device.id,
        type: device.type,
        status: device.status,
        lastSeen: device.lastSeen,
        metadata: device.metadata || {},
      };
      // Normalize metadata fields for querying
      if (device.metadata?.clientId) doc.clientId = device.metadata.clientId;
      if (device.metadata?.firmwareVersion) doc.firmwareVersion = device.metadata.firmwareVersion;
      if (device.metadata?.ipAddress) doc.ipAddress = device.metadata.ipAddress;
      if (typeof device.metadata?.uptime === 'number') doc.uptime = device.metadata.uptime;

      col.updateOne(
        { id: device.id },
        { $set: doc },
        { upsert: true }
      ).catch(err => logger.error('Mongo upsert device failed', { err }));
    } catch (e) {
      // Mongo may not be configured; ignore
    }
  }

  public async sendCommand(deviceId: string, command: string, timeout: number = 5000): Promise<string> {
    return new Promise((resolve, reject) => {
      const commandTopic = `devices/${deviceId}/rx`;
      
      // Set up timeout
      const timeoutHandle = setTimeout(() => {
        this.pendingCommands.delete(deviceId);
        reject(new Error(`Command timeout for device ${deviceId}`));
      }, timeout);

      // Store pending command
      this.pendingCommands.set(deviceId, {
        resolve: (result: string) => {
          clearTimeout(timeoutHandle);
          resolve(result);
        },
        reject: (error: Error) => {
          clearTimeout(timeoutHandle);
          reject(error);
        },
        timeout: timeoutHandle,
      });

      // Send command
      this.client.publish(commandTopic, command, (err) => {
        if (err) {
          clearTimeout(timeoutHandle);
          this.pendingCommands.delete(deviceId);
          reject(err);
        } else {
          logger.debug(`Sent command to ${deviceId}: ${command}`);
        }
      });
    });
  }

  public getDevices(): Device[] {
    return Array.from(this.devices.values());
  }

  public getDevice(deviceId: string): Device | undefined {
    return this.devices.get(deviceId);
  }

  private startDeviceTimeoutCheck(): void {
    // Check for offline devices every 10 seconds
    this.deviceTimeoutInterval = setInterval(() => {
      const now = Date.now();
      
      this.devices.forEach((device, deviceId) => {
        if (device.status === 'online') {
          const lastSeenMs = device.lastSeen.getTime();
          
          if (now - lastSeenMs > this.DEVICE_TIMEOUT_MS) {
            this.updateDeviceStatus(deviceId, 'offline');
            logger.info(`Device ${deviceId} marked offline due to timeout`);
          }
        }
      });
    }, 10000);
  }

  public getConnectedDevicesCount(): number {
    let count = 0;
    this.devices.forEach(device => {
      if (device.status === 'online') {
        count++;
      }
    });
    return count;
  }

  public async disconnect(): Promise<void> {
    // Clear the timeout check interval
    if (this.deviceTimeoutInterval) {
      clearInterval(this.deviceTimeoutInterval);
      this.deviceTimeoutInterval = null;
    }

    return new Promise((resolve) => {
      this.client.end(false, {}, () => {
        logger.info('MQTT client disconnected');
        resolve();
      });
    });
  }

  public async publish(topic: string, payload: Buffer | string, options?: any): Promise<void> {
    return new Promise((resolve, reject) => {
      this.client.publish(topic, payload, options || { qos: 1 }, (error) => {
        if (error) {
          logger.error(`Failed to publish to ${topic}:`, error);
          reject(error);
        } else {
          logger.debug(`Published to ${topic}, size: ${Buffer.isBuffer(payload) ? payload.length : payload.length}`);
          resolve();
        }
      });
    });
  }
}
