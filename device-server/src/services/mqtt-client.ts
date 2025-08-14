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

  private updateDeviceStatus(deviceId: string, status: 'online' | 'offline'): void {
    let device = this.devices.get(deviceId);
    
    if (!device) {
      device = {
        id: deviceId,
        type: 'speaker', // Default type, should be determined from device
        status,
        lastSeen: new Date(),
      };
      this.devices.set(deviceId, device);
    } else {
      device.status = status;
      device.lastSeen = new Date();
    }

    this.emit(`device:${status}`, deviceId);
    logger.info(`Device ${deviceId} is ${status}`);
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

  public async disconnect(): Promise<void> {
    return new Promise((resolve) => {
      this.client.end(false, {}, () => {
        logger.info('MQTT client disconnected');
        resolve();
      });
    });
  }
}
