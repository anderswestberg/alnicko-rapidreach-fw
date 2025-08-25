import mqtt, { IClientOptions, MqttClient } from 'mqtt';
import os from 'os';
import logger from './logger.js';

type MqttCoordOptions = {
  host?: string;
  port?: number;
  username?: string;
  password?: string;
  useTLS?: boolean;
  waitMs?: number;
  timeoutMs?: number;
  retries?: number;
};

let sharedClient: MqttClient | null = null;

export async function startMqttCoordinator(serviceName: string, opts: MqttCoordOptions = {}): Promise<void> {
  const startedAt = Date.now();
  const instanceId = `${os.hostname()}-${process.pid}-${startedAt}`;
  const host = opts.host || process.env.MQTT_BROKER_HOST || 'localhost';
  const port = Number(opts.port || process.env.MQTT_BROKER_PORT || 1883);
  const protocol = (opts.useTLS || process.env.MQTT_USE_TLS === 'true') ? 'mqtts' : 'mqtt';
  const username = opts.username ?? process.env.MQTT_USERNAME;
  const password = opts.password ?? process.env.MQTT_PASSWORD;
  const waitMs = opts.waitMs ?? 5000;
  const timeoutMs = opts.timeoutMs ?? 4000;
  const retries = opts.retries ?? 3;

  const base = `rapidreach/coord/${serviceName}`;
  const topicCurrent = `${base}/current`; // retained leader marker
  const topicAnnounce = `${base}/announce`; // transient announcement

  const url = `${protocol}://${host}:${port}`;
  const options: IClientOptions = {
    clientId: `coord-${serviceName}-${instanceId}`,
    clean: true,
    username,
    password,
    connectTimeout: timeoutMs,
    will: {
      topic: `${base}/status/${instanceId}`,
      payload: Buffer.from(JSON.stringify({ name: serviceName, instanceId, status: 'offline', ts: Date.now() })),
      qos: 0,
      retain: false,
    },
  };

  let client: MqttClient | null = null;
  let attempt = 0;
  while (attempt <= retries) {
    attempt += 1;
    try {
      client = mqtt.connect(url, options);
      await new Promise<void>((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('MQTT coordination connect timeout')), timeoutMs);
        client!.once('connect', () => { clearTimeout(timer); resolve(); });
        client!.once('error', (e) => { clearTimeout(timer); reject(e); });
      });
      logger.info(`[coord] Connected to MQTT at ${url} (attempt ${attempt}/${retries + 1})`);
      break;
    } catch (e) {
      logger.warn(`[coord] MQTT connect failed (attempt ${attempt}/${retries + 1}): ${(e as Error).message}`);
      try { client?.end(true); } catch {}
      client = null;
      if (attempt > retries) throw e;
      await new Promise((r) => setTimeout(r, 500));
    }
  }
  if (!client) throw new Error('MQTT coordination failed to connect');

  await new Promise<void>((resolve, reject) => {
    client!.subscribe([topicCurrent, topicAnnounce], { qos: 0 }, (err) => (err ? reject(err) : resolve()));
  });
  logger.info(`[coord] Subscribed to ${topicCurrent}, ${topicAnnounce}`);

  let shouldExit = false;
  client.on('message', (topic, payload) => {
    try {
      const data = JSON.parse(payload.toString());
      if (!data || data.name !== serviceName) return;

      if (topic === topicCurrent) {
        if (typeof data.startedAt === 'number' && data.startedAt > startedAt) {
          logger.warn(`${serviceName}: newer instance detected via retained leader (startedAt=${data.startedAt}). Exiting.`);
          shouldExit = true;
        }
      }

      if (topic === topicAnnounce) {
        if (typeof data.startedAt === 'number' && data.startedAt > startedAt) {
          logger.warn(`${serviceName}: newer instance announced (startedAt=${data.startedAt}). Exiting.`);
          shouldExit = true;
        }
      }
    } catch {
      // ignore
    }
  });

  // Read retained leader, then publish ourselves if we are newer
  // Small delay to ensure retained message arrives
  await new Promise((r) => setTimeout(r, 250));

  if (shouldExit) {
    // tiny delay for logs to flush
    await new Promise((r) => setTimeout(r, 50));
    process.exit(0);
  }

  // Publish our candidacy as retained; last writer with greatest startedAt wins naturally
  client.publish(
    topicCurrent,
    JSON.stringify({ name: serviceName, instanceId, startedAt }),
    { qos: 0, retain: true }
  );
  logger.info(`[coord] Published retained leader to ${topicCurrent} (startedAt=${startedAt})`);

  // Announce to make older instances exit promptly
  for (let i = 0; i < 5; i += 1) {
    client.publish(topicAnnounce, JSON.stringify({ name: serviceName, instanceId, startedAt }), { qos: 0, retain: false });
    await new Promise((r) => setTimeout(r, 100));
  }
  logger.info(`[coord] Sent announces on ${topicAnnounce}`);

  // Wait grace period for older instances to release ports
  await new Promise((r) => setTimeout(r, waitMs));

  // Keep the coordinator client alive to catch future newer instances
  sharedClient = client;

  // Cleanup on exit
  const cleanup = () => {
    try { sharedClient?.end(true); } catch {}
    sharedClient = null;
  };
  process.once('SIGINT', cleanup);
  process.once('SIGTERM', cleanup);
  process.once('exit', cleanup);
}


