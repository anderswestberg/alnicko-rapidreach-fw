import { MqttTerminal } from './mqtt-client.js';
import { config } from './config.js';
import axios from 'axios';
import chalk from 'chalk';
import * as fs from 'fs';

interface TestCase {
    id: string;
    name: string;
    commands: string[];
    expectedPatterns: string[];
    timeMinutes: number;
}

interface TestResult {
    id: string;
    passed: boolean;
    elapsedSeconds: number;
    responses: string[];
}

// Test cases that can be tested via MQTT CLI
const TEST_CASES: TestCase[] = [
    {
        id: 'RDP-210',
        name: 'Test shell commands execution',
        commands: ['help', 'rapidreach test'],
        expectedPatterns: ['Available commands', 'Hello'],
        timeMinutes: 2
    },
    {
        id: 'RDP-221',
        name: 'Test MQTT connection',
        commands: ['rapidreach mqtt status'],
        expectedPatterns: ['Connected', 'MQTT'],
        timeMinutes: 2
    },
    {
        id: 'RDP-223',
        name: 'Test MQTT heartbeat',
        commands: ['rapidreach mqtt heartbeat status', 'rapidreach mqtt heartbeat start'],
        expectedPatterns: ['Heartbeat', 'Started', 'heartbeat'],
        timeMinutes: 2
    },
    {
        id: 'RDP-207',
        name: 'Check system uptime',
        commands: ['rapidreach system uptime', 'kernel uptime'],
        expectedPatterns: ['Uptime', 'seconds', 'up'],
        timeMinutes: 1
    },
    {
        id: 'RDP-208',
        name: 'Monitor CPU usage',
        commands: ['rapidreach system cpu', 'kernel threads'],
        expectedPatterns: ['CPU', 'threads', 'Usage'],
        timeMinutes: 2
    },
    {
        id: 'RDP-209',
        name: 'Check memory usage',
        commands: ['rapidreach system memory', 'kernel stacks'],
        expectedPatterns: ['Memory', 'bytes', 'stack'],
        timeMinutes: 2
    }
];

class AcceptanceTestRunner {
    private client: MqttTerminal;
    private results: TestResult[] = [];

    constructor() {
        this.client = new MqttTerminal();
    }

    async run(): Promise<void> {
        console.log(chalk.blue('\n🧪 RapidReach Acceptance Test Runner\n'));
        
        try {
            // Connect to MQTT
            console.log(chalk.yellow('Connecting to MQTT broker...'));
            await this.client.connect();
            console.log(chalk.green('✓ Connected to MQTT broker\n'));

            // Run each test
            for (const testCase of TEST_CASES) {
                await this.runTest(testCase);
                await this.sleep(1000); // Brief pause between tests
            }

            // Summary
            this.printSummary();
            
            // Update Jira
            await this.updateJiraTasks();

            // Save results
            this.saveResults();

        } catch (error) {
            console.error(chalk.red('Error:'), error);
        } finally {
            await this.client.disconnect();
        }
    }

    private async runTest(testCase: TestCase): Promise<void> {
        console.log(chalk.cyan(`\n${'='.repeat(60)}`));
        console.log(chalk.cyan(`Test ${testCase.id}: ${testCase.name}`));
        console.log(chalk.cyan(`${'='.repeat(60)}\n`));

        const startTime = Date.now();
        const responses: string[] = [];
        let passed = false;

        for (const command of testCase.commands) {
            try {
                console.log(chalk.gray(`> ${command}`));
                const response = await this.client.sendCommand(command);
                console.log(chalk.gray(`< ${response.substring(0, 100)}${response.length > 100 ? '...' : ''}`));
                responses.push(response);

                // Check if any expected pattern matches
                const responseText = response.toLowerCase();
                for (const pattern of testCase.expectedPatterns) {
                    if (responseText.includes(pattern.toLowerCase())) {
                        passed = true;
                        break;
                    }
                }
            } catch (error) {
                console.log(chalk.red(`  Error: ${error}`));
                responses.push(`Error: ${error}`);
            }
        }

        const elapsedSeconds = (Date.now() - startTime) / 1000;
        
        // Store result
        this.results.push({
            id: testCase.id,
            passed,
            elapsedSeconds,
            responses
        });

        // Print result
        if (passed) {
            console.log(chalk.green(`\n✓ PASSED`));
        } else {
            console.log(chalk.red(`\n✗ FAILED`));
            console.log(chalk.yellow(`  Expected patterns: ${testCase.expectedPatterns.join(', ')}`));
        }
        console.log(chalk.gray(`  Time: ${elapsedSeconds.toFixed(1)}s`));
    }

    private async updateJiraTasks(): Promise<void> {
        console.log(chalk.yellow('\n\nUpdating Jira tasks...'));

        const jiraUrl = process.env.JIRA_URL;
        const jiraUser = process.env.JIRA_USER;
        const jiraToken = process.env.JIRA_TOKEN;

        if (!jiraUrl || !jiraUser || !jiraToken) {
            console.log(chalk.red('Jira credentials not found in environment'));
            return;
        }

        const auth = {
            username: jiraUser,
            password: jiraToken
        };

        for (const result of this.results) {
            if (!result.passed) continue; // Only update passed tests

            try {
                // Log work
                const worklogData = {
                    timeSpent: `${Math.max(1, Math.round(result.elapsedSeconds / 60))}m`,
                    comment: 'Automated test passed via MQTT CLI interface',
                    started: new Date().toISOString().replace('Z', '+0000')
                };

                await axios.post(
                    `${jiraUrl}/rest/api/2/issue/${result.id}/worklog`,
                    worklogData,
                    { auth }
                );

                // Get transitions
                const transitionsResponse = await axios.get(
                    `${jiraUrl}/rest/api/2/issue/${result.id}/transitions`,
                    { auth }
                );

                // Find Done transition
                const doneTransition = transitionsResponse.data.transitions.find(
                    (t: any) => t.name.toLowerCase() === 'done'
                );

                if (doneTransition) {
                    // Move to Done
                    await axios.post(
                        `${jiraUrl}/rest/api/2/issue/${result.id}/transitions`,
                        { transition: { id: doneTransition.id } },
                        { auth }
                    );
                    console.log(chalk.green(`  ✓ ${result.id} moved to Done`));
                }
            } catch (error: any) {
                console.log(chalk.red(`  ✗ Failed to update ${result.id}: ${error.message}`));
            }
        }
    }

    private printSummary(): void {
        const passed = this.results.filter(r => r.passed).length;
        const failed = this.results.filter(r => !r.passed).length;
        const totalTime = this.results.reduce((sum, r) => sum + r.elapsedSeconds, 0);

        console.log(chalk.cyan(`\n${'='.repeat(60)}`));
        console.log(chalk.cyan('ACCEPTANCE TEST SUMMARY'));
        console.log(chalk.cyan(`${'='.repeat(60)}`));
        console.log(`Total tests: ${this.results.length}`);
        console.log(chalk.green(`Passed: ${passed}`));
        console.log(chalk.red(`Failed: ${failed}`));
        console.log(`Total time: ${(totalTime / 60).toFixed(1)} minutes`);
        console.log(chalk.cyan(`${'='.repeat(60)}\n`));
    }

    private saveResults(): void {
        const resultsFile = 'acceptance-test-results.json';
        const data = {
            timestamp: new Date().toISOString(),
            summary: {
                total: this.results.length,
                passed: this.results.filter(r => r.passed).length,
                failed: this.results.filter(r => !r.passed).length
            },
            results: this.results
        };

        fs.writeFileSync(resultsFile, JSON.stringify(data, null, 2));
        console.log(chalk.gray(`Results saved to ${resultsFile}`));
    }

    private sleep(ms: number): Promise<void> {
        return new Promise(resolve => setTimeout(resolve, ms));
    }
}

// Run if called directly
if (import.meta.url === `file://${process.argv[1]}`) {
    const runner = new AcceptanceTestRunner();
    runner.run().catch(console.error);
}
