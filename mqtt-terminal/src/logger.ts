import fetch from 'node-fetch';

interface LogEntry {
  timestamp: string;
  level: string;
  message: string;
  [key: string]: any;
}

export class LogClient {
  private baseUrl: string;
  private source: string;
  private batchSize: number;
  private flushInterval: number;
  private queue: LogEntry[] = [];
  private timer: NodeJS.Timeout | null = null;
  private quiet: boolean;

  constructor(options: {
    baseUrl?: string;
    source?: string;
    batchSize?: number;
    flushInterval?: number;
    quiet?: boolean;
  } = {}) {
    this.baseUrl = options.baseUrl || 'http://localhost:3001';
    this.source = options.source || 'mqtt-terminal';
    this.batchSize = options.batchSize || 50;
    this.flushInterval = options.flushInterval || 5000;
    this.quiet = options.quiet || false;

    this.startFlushTimer();
    
    // Handle process exit
    process.on('exit', () => this.close());
    process.on('SIGINT', async () => {
      await this.close();
      process.exit(0);
    });
    process.on('SIGTERM', async () => {
      await this.close();
      process.exit(0);
    });
  }

  private startFlushTimer(): void {
    if (this.timer) {
      clearInterval(this.timer);
    }
    this.timer = setInterval(() => this.flush(), this.flushInterval);
  }

  async log(level: string, message: string, meta: Record<string, any> = {}): Promise<void> {
    const logEntry: LogEntry = {
      timestamp: new Date().toISOString(),
      level,
      message,
      ...meta
    };

    this.queue.push(logEntry);

    // Also log to console (unless quiet mode)
    if (!this.quiet) {
      const consoleMethod = level === 'error' ? console.error : 
                           level === 'warn' ? console.warn : 
                           console.log;
      consoleMethod(`[${level.toUpperCase()}] ${message}`, meta);
    }

    if (this.queue.length >= this.batchSize) {
      await this.flush();
    }
  }

  async debug(message: string, meta?: Record<string, any>): Promise<void> {
    return this.log('debug', message, meta);
  }

  async info(message: string, meta?: Record<string, any>): Promise<void> {
    return this.log('info', message, meta);
  }

  async warn(message: string, meta?: Record<string, any>): Promise<void> {
    return this.log('warn', message, meta);
  }

  async error(message: string, meta?: Record<string, any>): Promise<void> {
    return this.log('error', message, meta);
  }

  async flush(): Promise<void> {
    if (this.queue.length === 0) return;

    const logs = [...this.queue];
    this.queue = [];

    try {
      const response = await fetch(`${this.baseUrl}/logs`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({
          source: this.source,
          logs
        })
      });

      if (!response.ok) {
        console.error('Failed to send logs to server:', await response.text());
        // Don't re-queue to avoid infinite loop
      }
    } catch (error) {
      // Silently fail to avoid disrupting the main application
      console.error('Failed to send logs to server:', error);
    }
  }

  async close(): Promise<void> {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
    await this.flush();
  }
}

// Singleton instance
let loggerInstance: LogClient | null = null;

export function getLogger(options?: ConstructorParameters<typeof LogClient>[0]): LogClient {
  if (!loggerInstance) {
    loggerInstance = new LogClient(options);
  }
  return loggerInstance;
}
