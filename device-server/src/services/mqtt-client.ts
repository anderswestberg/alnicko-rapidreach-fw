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
  private shellInitialized: Set<string> = new Set();
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

  // Coordination
  private coordName?: string;
  private coordStartedAt?: number;
  private coordWaitMs: number = 5000;
  private coordInitialized: boolean = false;
  private coordExitRequested: boolean = false;
  private standbyMode: boolean = false;

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
      
      if (!this.standbyMode) {
        patterns.forEach(pattern => {
          this.client.subscribe(pattern, { qos: 1 }, (err, granted) => {
            if (err) {
              logger.error(`Failed to subscribe to ${pattern}:`, err);
            } else {
              logger.info(`Subscribe result for ${pattern}:`, granted);
              if (granted && granted.length > 0 && granted[0].qos !== 128) {
                logger.info(`Successfully subscribed to ${pattern} with QoS ${granted[0].qos}`);
              } else {
                logger.warn(`Subscription to ${pattern} was rejected (QoS 128 or empty grant)`);
              }
            }
          });
        });
      }

      // Coordination subscriptions/publish
      if (this.coordName && !this.coordInitialized) {
        void this.setupCoordination();
      }
    });

    this.client.on('disconnect', () => {
      logger.warn('Disconnected from MQTT broker');
      this.emit('disconnected');
    });

    this.client.on('error', (error) => {
      logger.error('MQTT client error:', error);
    });

    this.client.on('message', (topic, payload) => {
      // Coordination handling first
      if (this.coordName && (topic === `rapidreach/coord/${this.coordName}/current` || topic === `rapidreach/coord/${this.coordName}/announce`)) {
        try {
          const data = JSON.parse(payload.toString());
          if (data && data.name === this.coordName && typeof data.startedAt === 'number' && this.coordStartedAt && data.startedAt > this.coordStartedAt) {
            logger.warn(`${this.coordName}: newer instance detected (startedAt=${data.startedAt}). Initiating shutdown.`);
            this.initiateSelfShutdown();
            return;
          }
        } catch {
          // ignore
        }
      }
      if (this.standbyMode) {
        // Ignore non-coordination traffic while in standby
        return;
      }
      this.handleMessage(topic, payload);
    });
  }

  private async setupCoordination(): Promise<void> {
    if (!this.coordName) return;
    const name = this.coordName;
    const startedAt = this.coordStartedAt ?? Date.now();
    this.coordStartedAt = startedAt;
    const base = `rapidreach/coord/${name}`;
    const topicCurrent = `${base}/current`;
    const topicAnnounce = `${base}/announce`;

    await new Promise<void>((resolve) => {
      this.client.subscribe([topicCurrent, topicAnnounce], { qos: 0 }, () => resolve());
    });
    logger.info(`[coord] Subscribed to ${topicCurrent}, ${topicAnnounce}`);

    // Publish retained leader and announces
    this.client.publish(topicCurrent, JSON.stringify({ name, startedAt }), { qos: 0, retain: true });
    logger.info(`[coord] Published retained leader to ${topicCurrent} (startedAt=${startedAt})`);
    for (let i = 0; i < 5; i += 1) {
      this.client.publish(topicAnnounce, JSON.stringify({ name, startedAt }), { qos: 0, retain: false });
      await new Promise((r) => setTimeout(r, 100));
    }
    logger.info(`[coord] Sent announces on ${topicAnnounce}`);
    await new Promise((r) => setTimeout(r, this.coordWaitMs));
    this.coordInitialized = true;
  }

  private initiateSelfShutdown(): void {
    if (this.coordExitRequested) return;
    this.coordExitRequested = true;
    if (this.isRunningInContainer()) {
      // Enter standby: free resources and unsubscribe device topics, keep coordination
      void this.enterStandby();
      this.emit('standby');
      return;
    }
    // Stop periodic timers owned by this client
    if (this.deviceTimeoutInterval) {
      try { clearInterval(this.deviceTimeoutInterval); } catch {}
      this.deviceTimeoutInterval = null as any;
    }
    // Clear all log flush timers
    try {
      this.logFlushTimers.forEach((t) => { try { clearTimeout(t); } catch {} });
      this.logFlushTimers.clear();
    } catch {}
    // Clear all pending command timeouts
    try {
      this.pendingCommands.forEach((pc) => { try { clearTimeout(pc.timeout); } catch {} });
      this.pendingCommands.clear();
    } catch {}
    // End MQTT to help free resources quickly
    try { this.client.end(true); } catch {}
    // Trigger app-level graceful shutdown (index.ts handles SIGTERM)
    try { process.kill(process.pid, 'SIGTERM'); } catch {}
    // If running under a dev watcher (e.g., tsx watch), also signal parent to stop restarting
    try {
      if (process.ppid && process.ppid !== 1) {
        process.kill(process.ppid, 'SIGTERM');
      }
    } catch {}
    // Fallback hard-exit after grace period
    setTimeout(() => {
      try { process.exit(0); } catch {}
    }, 300);
  }

  private async enterStandby(): Promise<void> {
    this.standbyMode = true;
    // Unsubscribe device and log topics
    const patterns = [
      'devices/+/tx',
      'rapidreach/+/shell/out',
      'rapidreach/heartbeat/+',
      'logs/+',
      'rapidreach/logs/+',
    ];
    try {
      await new Promise<void>((resolve) => this.client.unsubscribe(patterns, () => resolve()));
    } catch {}
    // Clear timers and pending
    try {
      if (this.deviceTimeoutInterval) { clearInterval(this.deviceTimeoutInterval); this.deviceTimeoutInterval = null as any; }
      this.logFlushTimers.forEach((t) => { try { clearTimeout(t); } catch {} });
      this.logFlushTimers.clear();
      this.pendingCommands.forEach((pc) => { try { clearTimeout(pc.timeout); } catch {} });
      this.pendingCommands.clear();
    } catch {}
    logger.info('Entered standby mode (container): resources freed, coordination active');
  }

  private isRunningInContainer(): boolean {
    try {
      // Common Docker hint
      // eslint-disable-next-line @typescript-eslint/no-var-requires
      const fs = require('fs');
      if (fs.existsSync('/.dockerenv')) return true;
      const cgroup = fs.readFileSync('/proc/1/cgroup', 'utf8');
      return /docker|kubepods|containerd/i.test(cgroup);
    } catch {
      return false;
    }
  }

  public async coordinateSingleInstance(name: string, waitMs: number = 5000): Promise<void> {
    this.coordName = name;
    this.coordWaitMs = waitMs;
    this.coordStartedAt = Date.now();
    if (this.client.connected) {
      await this.setupCoordination();
    } else {
      await new Promise<void>((resolve) => this.once('connected', () => resolve()));
      await this.setupCoordination();
    }
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
        // Clean control characters from the text before parsing
        // Replace control characters with their escaped versions or remove them
        const cleanedText = text.replace(/[\x00-\x1F\x7F]/g, (match) => {
          // Convert control characters to escaped unicode
          return '\\u' + ('0000' + match.charCodeAt(0).toString(16)).slice(-4);
        });
        const parsed = JSON.parse(cleanedText);
        
        // Handle batched logs from device firmware (format: {source: "deviceId", logs: [...]})
        if (parsed.logs && Array.isArray(parsed.logs)) {
          logger.info(`Processing batched logs from ${deviceId}:`, { 
            count: parsed.logs.length,
            source: parsed.source,
            firstLog: parsed.logs[0]
          });
          items = parsed.logs.map((p: any) => {
            const level = (p.level || 'info').toLowerCase();
            // Map level to numeric severity
            const levelMap: Record<string, number> = {
              'error': 4,
              'warn': 3,
              'warning': 3,
              'info': 2,
              'debug': 1
            };
            
            return {
              deviceId: parsed.source || deviceId,
              device: parsed.source || deviceId,  // Add device field for Source column
              source: p.module || 'unknown',  // Use module as source instead of deviceId
              level: level,
              levelNo: levelMap[level] || 2, // Default to info (2) if unknown
              message: p.message !== undefined ? p.message : JSON.stringify(p),
              // Handle timestamp - if it's a number less than 1 billion, assume it's ms since boot
              // Otherwise it's a Unix timestamp in milliseconds
              timestamp: p.timestamp 
                ? (typeof p.timestamp === 'number' && p.timestamp < 1000000000 
                   ? now  // For boot-relative timestamps, just use current time for now
                   : new Date(p.timestamp))
                : now,
              module: p.module || 'unknown',
            };
          });
        } else if (Array.isArray(parsed)) {
          // Handle array of logs
          items = parsed.map((p) => {
            const level = (p.level || 'info').toLowerCase();
            const levelMap: Record<string, number> = {
              'error': 4,
              'warn': 3,
              'warning': 3,
              'info': 2,
              'debug': 1
            };
            
            return {
              deviceId,
              device: deviceId,  // Add device field for Source column
              source: p.module || 'unknown',  // Use module as source
              level: level,
              levelNo: levelMap[level] || 2,
              message: p.message || JSON.stringify(p),
              timestamp: p.timestamp ? new Date(p.timestamp) : now,
              ...p,
            };
          });
        } else {
          // Handle single log entry
          logger.warn(`Treating as single log entry from ${deviceId}:`, {
            hasLogs: 'logs' in parsed,
            keys: Object.keys(parsed),
            textPreview: text.substring(0, 100)
          });
          const level = (parsed.level || 'info').toLowerCase();
          const levelMap: Record<string, number> = {
            'error': 4,
            'warn': 3,
            'warning': 3,
            'info': 2,
            'debug': 1
          };
          items = [{
            deviceId,
            device: deviceId,  // Add device field for Source column
            source: parsed.module || 'unknown',  // Use module as source
            level: level,
            levelNo: levelMap[level] || 2,
            message: parsed.message || text,
            timestamp: parsed.timestamp ? new Date(parsed.timestamp) : now,
            ...parsed,
          }];
        }
      } catch (_e) {
        // Not JSON, store as simple text log
        items = [{
          deviceId,
          device: deviceId,  // Add device field for Source column
          source: 'unknown',  // No module info in plain text
          level: 'info',
          levelNo: 2,  // Info level
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
          hwId: heartbeat.hwId,  // Hardware device ID for audio topic
        });
        
        // Send initial shell command to prevent 90-second timeout
        // The MQTT shell backend expects an initial command after connection
        if (clientId.endsWith('-shell')) {
          const shellTopic = `rapidreach/${clientId}/shell/in`;
          logger.info(`Sending keepalive to shell ${clientId} on topic ${shellTopic}`);
          // Send empty string to initialize the shell session
          this.client.publish(shellTopic, '', { qos: 1 });
        }
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
    
    // Handle shell output messages - indicates shell is connected
    if (topic.match(/^rapidreach\/.*-shell\/shell\/out$/)) {
      const parts = topic.split('/');
      const shellClientId = parts[1]; // e.g., "313938-shell"
      const shellTopic = `rapidreach/${shellClientId}/shell/in`;
      
      // Send initial command to keep shell alive (prevent 90s timeout)
      if (!this.shellInitialized.has(shellClientId)) {
        logger.info(`Shell connected: ${shellClientId}, sending keepalive to ${shellTopic}`);
        this.client.publish(shellTopic, '\n', { qos: 1 });
        this.shellInitialized.add(shellClientId);
        
        // Clear after 2 hours to allow re-initialization if needed
        setTimeout(() => {
          this.shellInitialized.delete(shellClientId);
        }, 2 * 60 * 60 * 1000);
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
      hwId?: string;  // Hardware device ID
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
