#!/usr/bin/env node

import { Command } from 'commander';
import chalk from 'chalk';
import { MqttTerminal } from './mqtt-client.js';
import { InteractiveTerminal } from './terminal.js';
import { config } from './config.js';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import fs from 'fs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Check if .env exists, if not copy from env.example
const envPath = join(__dirname, '../.env');
const envExamplePath = join(__dirname, '../env.example');
if (!fs.existsSync(envPath) && fs.existsSync(envExamplePath)) {
  fs.copyFileSync(envExamplePath, envPath);
  console.log(chalk.yellow('Created .env file from env.example'));
}

const program = new Command();

program
  .name('mqtt-terminal')
  .description('MQTT-based terminal for RapidReach device CLI')
  .version('1.0.0')
  .option('-h, --host <host>', 'MQTT broker host', config.mqtt.brokerHost)
  .option('-p, --port <port>', 'MQTT broker port', String(config.mqtt.brokerPort))
  .option('-d, --device <id>', 'Device ID', config.device.id)
  .option('-u, --username <username>', 'MQTT username')
  .option('-P, --password <password>', 'MQTT password')
  .option('-c, --command <cmd>', 'Execute single command and exit')
  .option('--timeout <ms>', 'Response timeout in milliseconds', String(config.terminal.responseTimeout));

program.parse();

const options = program.opts();

// Override config with command line options
if (options.host) config.mqtt.brokerHost = options.host;
if (options.port) config.mqtt.brokerPort = parseInt(options.port);
if (options.device) config.device.id = options.device;
if (options.username) config.mqtt.username = options.username;
if (options.password) config.mqtt.password = options.password;
if (options.timeout) config.terminal.responseTimeout = parseInt(options.timeout);

async function main() {
  const mqttClient = new MqttTerminal(config.device.id);

  try {
    await mqttClient.connect();

    if (options.command) {
      // Single command mode
      console.log(chalk.gray(`Executing: ${options.command}`));
      try {
        const response = await mqttClient.sendCommand(options.command);
        console.log(response);
        process.exit(0);
      } catch (error) {
        console.error(chalk.red(`Error: ${error instanceof Error ? error.message : error}`));
        process.exit(1);
      }
    } else {
      // Interactive mode
      const terminal = new InteractiveTerminal(mqttClient);
      await terminal.start();
    }
  } catch (error) {
    console.error(chalk.red('Failed to connect:'), error);
    process.exit(1);
  }

  // Handle process termination
  process.on('SIGINT', () => {
    mqttClient.disconnect();
    process.exit(0);
  });
}

main().catch(error => {
  console.error(chalk.red('Fatal error:'), error);
  process.exit(1);
});
