#!/usr/bin/env node

import { Command } from 'commander';
import fetch from 'node-fetch';
import chalk from 'chalk';

const program = new Command();

program
  .name('device-cli')
  .description('CLI for RapidReach Device Server')
  .version('1.0.0');

program
  .command('exec <deviceId> <command>')
  .description('Execute a command on a device')
  .option('-s, --server <url>', 'Server URL', 'http://localhost:3002')
  .option('-k, --api-key <key>', 'API key')
  .option('-t, --timeout <ms>', 'Command timeout', '5000')
  .action(async (deviceId: string, command: string, options) => {
    try {
      const response = await fetch(`${options.server}/api/devices/${deviceId}/execute`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          ...(options.apiKey && { 'X-API-Key': options.apiKey }),
        },
        body: JSON.stringify({
          command,
          timeout: parseInt(options.timeout),
        }),
      });

      const result = await response.json() as any;

      if (result.success) {
        console.log(chalk.green('✓ Command executed successfully'));
        console.log(chalk.gray(`Device: ${result.deviceId}`));
        console.log(chalk.gray(`Time: ${result.executionTime}ms`));
        console.log('\nOutput:');
        console.log(result.output);
      } else {
        console.error(chalk.red('✗ Command failed'));
        console.error(chalk.red(result.error));
      }
    } catch (error) {
      console.error(chalk.red('✗ Failed to connect to server'));
      console.error(error);
    }
  });

program
  .command('devices')
  .description('List all devices')
  .option('-s, --server <url>', 'Server URL', 'http://localhost:3002')
  .option('-k, --api-key <key>', 'API key')
  .action(async (options) => {
    try {
      const response = await fetch(`${options.server}/api/devices`, {
        headers: {
          ...(options.apiKey && { 'X-API-Key': options.apiKey }),
        },
      });

      const result = await response.json() as any;

      if (result.success) {
        console.log(chalk.green(`✓ Found ${result.count} device(s)`));
        console.log();
        
        result.devices.forEach((device: any) => {
          const statusColor = device.status === 'online' ? chalk.green : chalk.red;
          console.log(`${chalk.cyan(device.id)} - ${device.type} - ${statusColor(device.status)}`);
          console.log(chalk.gray(`  Last seen: ${device.lastSeen}`));
        });
      } else {
        console.error(chalk.red('✗ Failed to get devices'));
        console.error(chalk.red(result.error));
      }
    } catch (error) {
      console.error(chalk.red('✗ Failed to connect to server'));
      console.error(error);
    }
  });

program.parse();
