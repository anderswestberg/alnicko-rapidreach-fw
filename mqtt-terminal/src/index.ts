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
  .option('-h, --host <host>', 'MQTT broker host', 'localhost')
  .option('-p, --port <port>', 'MQTT broker port', '1883')
  .option('-d, --device <id>', 'Device ID (required - use first 6 chars from EMQX dashboard)')
  .option('-u, --username <username>', 'MQTT username', 'admin')
  .option('-P, --password <password>', 'MQTT password', 'public')
  .option('-c, --command <cmd>', 'Execute single command and exit')
  .option('--timeout <ms>', 'Response timeout in milliseconds', '5000');

program.parse();

const options = program.opts();

// Check if device ID is provided
if (!options.device) {
  console.error(chalk.red('Error: Device ID is required'));
  console.log(chalk.yellow('\nHow to find your device ID:'));
  console.log('1. Check EMQX dashboard at http://localhost:18083 (login: admin/public)');
  console.log('2. Look for your device in the Clients list');
  console.log('3. Use the first 6 characters of the client ID\n');
  console.log(chalk.gray('Example: npm run start -- -d 313938'));
  process.exit(1);
}

// Override config with command line options
config.mqtt.brokerHost = options.host;
config.mqtt.brokerPort = parseInt(options.port);
config.device.id = options.device;
config.mqtt.username = options.username;
config.mqtt.password = options.password;
config.terminal.responseTimeout = parseInt(options.timeout);

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
      const terminal = new InteractiveTerminal(mqttClient, config.device.id);
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
