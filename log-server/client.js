/**
 * Simple HTTP logger client for RapidReach log server
 * Can be used in both Node.js and browser environments
 */

export class LogClient {
  constructor(options = {}) {
    this.baseUrl = options.baseUrl || 'http://localhost:3001';
    this.source = options.source || 'unknown';
    this.batchSize = options.batchSize || 50;
    this.flushInterval = options.flushInterval || 5000; // 5 seconds
    this.queue = [];
    this.timer = null;
    
    // Start flush timer
    this.startFlushTimer();
  }
  
  startFlushTimer() {
    if (this.timer) {
      clearInterval(this.timer);
    }
    this.timer = setInterval(() => this.flush(), this.flushInterval);
  }
  
  async log(level, message, meta = {}) {
    const logEntry = {
      timestamp: new Date().toISOString(),
      level,
      message,
      ...meta
    };
    
    this.queue.push(logEntry);
    
    // Flush if batch size reached
    if (this.queue.length >= this.batchSize) {
      await this.flush();
    }
  }
  
  // Convenience methods
  async debug(message, meta) { return this.log('debug', message, meta); }
  async info(message, meta) { return this.log('info', message, meta); }
  async warn(message, meta) { return this.log('warn', message, meta); }
  async error(message, meta) { return this.log('error', message, meta); }
  
  async flush() {
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
        console.error('Failed to send logs:', await response.text());
        // Re-add logs to queue for retry
        this.queue.unshift(...logs);
      }
    } catch (error) {
      console.error('Failed to send logs:', error);
      // Re-add logs to queue for retry
      this.queue.unshift(...logs);
    }
  }
  
  async close() {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
    await this.flush();
  }
}

// Node.js specific implementation
export class NodeLogClient extends LogClient {
  constructor(options = {}) {
    super(options);
    
    // Handle process exit
    process.on('exit', () => this.close());
    process.on('SIGINT', () => this.close());
    process.on('SIGTERM', () => this.close());
  }
}

// Example usage:
// const logger = new NodeLogClient({ 
//   source: 'mqtt-terminal',
//   baseUrl: 'http://localhost:3001'
// });
// 
// logger.info('Application started');
// logger.error('Connection failed', { error: 'timeout', retries: 3 });
