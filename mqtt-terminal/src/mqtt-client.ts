import mqtt, { MqttClient } from 'mqtt';
import chalk from 'chalk';
import { EventEmitter } from 'events';
import { config } from './config.js';
import { getLogger } from './logger.js';

const logger = getLogger();

export interface MqttTerminalEvents {
  connected: () => void;
  disconnected: () => void;
  response: (message: string) => void;
  error: (error: Error) => void;
}

export declare interface MqttTerminal {
  on<U extends keyof MqttTerminalEvents>(
    event: U, listener: MqttTerminalEvents[U]
  ): this;
  emit<U extends keyof MqttTerminalEvents>(
    event: U, ...args: Parameters<MqttTerminalEvents[U]>
  ): boolean;
}

export class MqttTerminal extends EventEmitter {
  private client: MqttClient | null = null;
  private deviceId: string;
  private commandTopic: string;
  private responseTopic: string;
  private responseTimeout: number;
  private responseTimer: NodeJS.Timeout | null = null;

  constructor(deviceId?: string) {
    super();
    this.deviceId = deviceId || config.device.id;
    this.commandTopic = config.device.commandTopic(this.deviceId);
    this.responseTopic = config.device.responseTopic(this.deviceId);
    this.responseTimeout = config.terminal.responseTimeout;
  }

  async connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      const brokerUrl = `mqtt://${config.mqtt.brokerHost}:${config.mqtt.brokerPort}`;
      
      console.log(chalk.yellow(`Connecting to MQTT broker at ${brokerUrl}...`));
      logger.info('Connecting to MQTT broker', { brokerUrl, deviceId: this.deviceId });

      this.client = mqtt.connect(brokerUrl, {
        clientId: config.mqtt.clientId,
        username: config.mqtt.username || undefined,
        password: config.mqtt.password || undefined,
        connectTimeout: config.mqtt.connectTimeout,
        reconnectPeriod: config.mqtt.reconnectPeriod,
      });

      this.client.on('connect', () => {
        console.log(chalk.green('✓ Connected to MQTT broker'));
        logger.info('Connected to MQTT broker');
        console.log(chalk.blue(`Command topic: ${this.commandTopic}`));
        console.log(chalk.blue(`Response topic: ${this.responseTopic}`));
        
        // Subscribe to response topic
        this.client!.subscribe(this.responseTopic, { qos: 1 }, (err) => {
          if (err) {
            console.error(chalk.red('Failed to subscribe to response topic:'), err);
            logger.error('Failed to subscribe to response topic', { error: err.message, topic: this.responseTopic });
            reject(err);
          } else {
            console.log(chalk.green(`✓ Subscribed to ${this.responseTopic}`));
            logger.info('Subscribed to response topic', { topic: this.responseTopic });
            this.emit('connected');
            resolve();
          }
        });
      });

      this.client.on('message', (topic, message) => {
        if (topic === this.responseTopic) {
          const response = message.toString();
          this.clearResponseTimeout();
          this.emit('response', response);
        }
      });

      this.client.on('error', (error) => {
        console.error(chalk.red('MQTT error:'), error);
        logger.error('MQTT client error', { error: error.message });
        this.emit('error', error);
        reject(error);
      });

      this.client.on('close', () => {
        console.log(chalk.yellow('MQTT connection closed'));
        logger.info('MQTT connection closed');
        this.emit('disconnected');
      });

      this.client.on('reconnect', () => {
        console.log(chalk.yellow('Reconnecting to MQTT broker...'));
      });
    });
  }

  async sendCommand(command: string): Promise<string> {
    return new Promise((resolve, reject) => {
      if (!this.client || !this.client.connected) {
        reject(new Error('Not connected to MQTT broker'));
        return;
      }

      let gotAnyChunk = false;
      let buffer = '';
      let settleTimer: NodeJS.Timeout | null = null;
      const settleDelayMs = 200;

      const responseHandler = (response: string) => {
        gotAnyChunk = true;
        // Don't add newlines between chunks - they might be mid-word splits
        buffer += response;
        if (settleTimer) clearTimeout(settleTimer);
        settleTimer = setTimeout(() => {
          this.clearResponseTimeout();
          this.removeListener('response', responseHandler);
          resolve(buffer);
        }, settleDelayMs);
      };

      this.on('response', responseHandler);

      // Set response timeout
      this.responseTimer = setTimeout(() => {
        if (settleTimer) clearTimeout(settleTimer);
        this.removeListener('response', responseHandler);
        if (gotAnyChunk) {
          resolve(buffer);
        } else {
          reject(new Error('Response timeout'));
        }
      }, this.responseTimeout);

      // Publish command - Zephyr shell expects newline-terminated
      const payload = command.endsWith('\n') ? command : `${command}\n`;
      this.client.publish(this.commandTopic, payload, { qos: 0 }, (err) => {
        if (err) {
          this.clearResponseTimeout();
          this.removeListener('response', responseHandler);
          if (settleTimer) clearTimeout(settleTimer);
          reject(err);
        }
      });
    });
  }

  private clearResponseTimeout(): void {
    if (this.responseTimer) {
      clearTimeout(this.responseTimer);
      this.responseTimer = null;
    }
  }

  disconnect(): void {
    this.clearResponseTimeout();
    if (this.client) {
      this.client.end();
      this.client = null;
    }
  }

  isConnected(): boolean {
    return this.client !== null && this.client.connected;
  }
}
