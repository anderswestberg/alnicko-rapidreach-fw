import mqtt, { MqttClient } from 'mqtt';
import { EventEmitter } from 'events';
import { v4 as uuidv4 } from 'uuid';
import config from '../config/index.js';
import logger from '../utils/logger.js';
import { Device, DeviceMessage } from '../types/device.js';

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
        'devices/+/tx',           // All device responses
        'rapidreach/+/shell/out', // For the other MQTT shell pattern
        'rapidreach/heartbeat/+', // Device heartbeat messages
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
}
