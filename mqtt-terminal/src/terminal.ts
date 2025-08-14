import readline from 'readline';
import chalk from 'chalk';
import { MqttTerminal } from './mqtt-client.js';
import { config } from './config.js';

export class InteractiveTerminal {
  private mqttClient: MqttTerminal;
  private rl: readline.Interface;
  private isRunning = false;
  private commandHistory: string[] = [];
  private historyIndex = -1;
  private deviceId: string;
  private customPrompt: string;

  constructor(mqttClient: MqttTerminal, deviceId: string) {
    this.mqttClient = mqttClient;
    this.deviceId = deviceId;
    
    // Create prompt like "mqtt-313938:~$ "
    const shortId = deviceId.substring(0, 6);
    this.customPrompt = chalk.green(`mqtt-${shortId}`) + chalk.gray(':~$ ');
    
    this.rl = readline.createInterface({
      input: process.stdin,
      output: process.stdout,
      terminal: true,
      prompt: this.customPrompt,
    });

    this.setupReadline();
  }

  private setupReadline(): void {
    // Tab completion
    this.rl.on('line', (line) => {
      this.handleCommand(line.trim());
    });

    // Handle Ctrl+C
    this.rl.on('SIGINT', () => {
      this.stop();
    });

    // Command history with arrow keys
    process.stdin.on('keypress', (str, key) => {
      if (!key) return;
      
      if (key.name === 'up') {
        if (this.historyIndex < this.commandHistory.length - 1) {
          this.historyIndex++;
          const cmd = this.commandHistory[this.commandHistory.length - 1 - this.historyIndex];
          this.rl.write(null, { ctrl: true, name: 'u' }); // Clear line
          this.rl.write(cmd);
        }
      } else if (key.name === 'down') {
        if (this.historyIndex > 0) {
          this.historyIndex--;
          const cmd = this.commandHistory[this.commandHistory.length - 1 - this.historyIndex];
          this.rl.write(null, { ctrl: true, name: 'u' }); // Clear line
          this.rl.write(cmd);
        } else if (this.historyIndex === 0) {
          this.historyIndex = -1;
          this.rl.write(null, { ctrl: true, name: 'u' }); // Clear line
        }
      }
    });

    // Simple tab completion - just repeat the line for now
    (this.rl as any).completer = (line: string) => {
      return [[line], line];
    };
  }

  async start(): Promise<void> {
    this.isRunning = true;
    
    const shortId = this.deviceId.substring(0, 6);
    console.log(chalk.cyan('\n╔════════════════════════════════════════════════════════════╗'));
    console.log(chalk.cyan('║') + chalk.white.bold('          RapidReach MQTT Shell Terminal v2.0               ') + chalk.cyan('║'));
    console.log(chalk.cyan('╚════════════════════════════════════════════════════════════╝'));
    console.log();
    console.log(chalk.green(`Connected to device: ${shortId}`));
    console.log(chalk.gray('Type commands to send to device, "exit" to quit'));
    console.log(chalk.gray('Use ↑/↓ for command history'));
    console.log();

    this.prompt();
  }

  private prompt(): void {
    if (this.isRunning && this.mqttClient.isConnected()) {
      this.rl.prompt();
    }
  }

  private async handleCommand(command: string): Promise<void> {
    if (!command) {
      this.prompt();
      return;
    }

    // Add to history
    if (command && command !== this.commandHistory[this.commandHistory.length - 1]) {
      this.commandHistory.push(command);
    }
    this.historyIndex = -1;

    // Handle only essential commands
    switch (command.toLowerCase()) {
      case 'exit':
      case 'quit':
      case 'q':
        this.stop();
        return;
      
      case 'clear':
      case 'cls':
        console.clear();
        this.prompt();
        return;
    }

    // Send command to device
    try {
      console.log(chalk.gray(`→ Sending: ${command}`));
      const response = await this.mqttClient.sendCommand(command);
      
      // Format response
      if (response.startsWith('Error:')) {
        console.log(chalk.red(`← ${response}`));
      } else if (response === 'OK') {
        console.log(chalk.green(`← ${response}`));
      } else {
        // Multi-line response
        response.split('\n').forEach(line => {
          console.log(chalk.blue(`← ${line}`));
        });
      }
    } catch (error) {
      if (error instanceof Error) {
        if (error.message === 'Response timeout') {
          console.log(chalk.red('← Error: Command timeout - no response received'));
        } else {
          console.log(chalk.red(`← Error: ${error.message}`));
        }
      }
    }

    this.prompt();
  }

  stop(): void {
    this.isRunning = false;
    console.log(chalk.yellow('\n\nDisconnecting...'));
    this.rl.close();
    this.mqttClient.disconnect();
    process.exit(0);
  }
}
