#!/usr/bin/env node

import { Command } from 'commander';
import chalk from 'chalk';
import { MqttTerminal } from './mqtt-client.js';
import { config } from './config.js';

const program = new Command();

program
  .name('mqtt-terminal')
  .description('MQTT-based terminal for RapidReach device CLI')
  .version('1.0.0')
  .option('-d, --device <id>', 'Device ID (required)')
  .option('-h, --host <host>', 'MQTT broker host', config.mqtt.brokerHost)
  .option('-p, --port <port>', 'MQTT broker port', config.mqtt.brokerPort.toString())
  .option('-u, --username <username>', 'MQTT username', config.mqtt.username)
  .option('-w, --password <password>', 'MQTT password', config.mqtt.password)
  .option('-c, --command <cmd>', 'Execute single command and exit')
  .option('--timeout <ms>', 'Response timeout in milliseconds', config.terminal.responseTimeout.toString())
  .option('-i, --interactive', 'Start interactive mode (default)')
  .option('-v, --verbose', 'Enable verbose logging')
  .option('-q, --quiet', 'Suppress connection messages and verbose output');

program.parse();

const options = program.opts();

// Validate required options
if (!options.device) {
  console.error(chalk.red('Error: Device ID is required'));
  console.error(chalk.gray('Usage: mqtt-terminal -d <device_id> [options]'));
  console.error(chalk.gray('Example: mqtt-terminal -d 313938 -c "app led on 0"'));
  process.exit(1);
}

// Override config with command line options
if (options.host) config.mqtt.brokerHost = options.host;
if (options.port) config.mqtt.brokerPort = parseInt(options.port);
if (options.username) config.mqtt.username = options.username;
if (options.password) config.mqtt.password = options.password;
if (options.timeout) config.terminal.responseTimeout = parseInt(options.timeout);

// Set device ID
config.device.id = options.device;

let mqttClient: MqttTerminal;

async function main() {
  mqttClient = new MqttTerminal(config.device.id, options.quiet);

  try {
    await mqttClient.connect();

    if (options.command) {
      // Single command mode
      if (options.verbose && !options.quiet) {
        console.log(chalk.gray(`Executing: ${options.command}`));
        console.log(chalk.gray(`Device: ${config.device.id}`));
        console.log(chalk.gray(`Broker: ${config.mqtt.brokerHost}:${config.mqtt.brokerPort}`));
      }
      
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
      if (options.verbose && !options.quiet) {
        console.log(chalk.gray(`Starting interactive mode for device ${config.device.id}`));
        console.log(chalk.gray(`Broker: ${config.mqtt.brokerHost}:${config.mqtt.brokerPort}`));
      }
      
      const { InteractiveTerminal } = await import('./terminal.js');
      const terminal = new InteractiveTerminal(mqttClient, config.device.id);
      await terminal.start();
    }
  } catch (error) {
    console.error(chalk.red('Failed to connect:'), error);
    process.exit(1);
  }
}

// Handle graceful shutdown
process.on('SIGINT', async () => {
  console.log(chalk.gray('\nShutting down...'));
  try {
    await mqttClient.disconnect();
    process.exit(0);
  } catch (error) {
    console.error(chalk.red('Error during shutdown:'), error);
    process.exit(1);
  }
});

main().catch(error => {
  console.error(chalk.red('Fatal error:'), error);
  process.exit(1);
});
