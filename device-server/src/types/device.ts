export interface Device {
  id: string;
  type: 'speaker' | 'sensor' | 'unknown';
  status: 'online' | 'offline' | 'unknown';
  lastSeen: Date;
  metadata?: Record<string, any>;
}

export interface CommandRequest {
  deviceId: string;
  command: string;
  timeout?: number;
}

export interface CommandResponse {
  deviceId: string;
  command: string;
  output: string;
  success: boolean;
  timestamp: Date;
  executionTime: number;
}

export interface DeviceMessage {
  topic: string;
  payload: Buffer | string;
  timestamp: Date;
}
