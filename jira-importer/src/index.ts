#!/usr/bin/env node

import { readFile } from 'fs/promises';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import { JiraImporter } from './importer.js';
import { JiraConfig } from './types.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

function getEnvConfig(): JiraConfig {
  const url = process.env.JIRA_URL;
  const username = process.env.JIRA_USER;
  const token = process.env.JIRA_TOKEN;
  const projectKey = process.env.JIRA_PROJECT;

  if (!url || !username || !token || !projectKey) {
    console.error('Missing required environment variables:');
    console.error('  JIRA_URL      - Jira server URL (e.g., https://jira.company.com)');
    console.error('  JIRA_USER     - Jira username');
    console.error('  JIRA_TOKEN    - Jira password or API token');
    console.error('  JIRA_PROJECT  - Jira project key (e.g., RDP)');
    process.exit(1);
  }

  return { url, username, token, projectKey };
}

function printUsage() {
  console.log('Usage: npm run dev [options] [file1.json] [file2.json] ...');
  console.log('');
  console.log('Options:');
  console.log('  --dry-run     Show what would be created without actually creating issues');
  console.log('  --help        Show this help message');
  console.log('');
  console.log('Examples:');
  console.log('  npm run dev --dry-run data/platform-epic.json data/platform-done.json');
  console.log('  npm run dev data/smart-speaker-epic.json data/smart-speaker-remaining.json');
  console.log('');
  console.log('Environment variables required:');
  console.log('  JIRA_URL, JIRA_USER, JIRA_TOKEN, JIRA_PROJECT');
}

async function getDefaultDataFiles(): Promise<string[]> {
  const dataDir = join(__dirname, '..', 'data');
  return [
    join(dataDir, 'platform-epic.json'),
    join(dataDir, 'platform-done.json'),
    join(dataDir, 'platform-remaining.json'),
    join(dataDir, 'smart-speaker-epic.json'),
    join(dataDir, 'smart-speaker-done.json'),
    join(dataDir, 'smart-speaker-remaining.json')
  ];
}

async function main() {
  const args = process.argv.slice(2);
  
  if (args.includes('--help')) {
    printUsage();
    process.exit(0);
  }

  const dryRun = args.includes('--dry-run');
  const fileArgs = args.filter(arg => !arg.startsWith('--'));
  
  let filePaths: string[];
  
  if (fileArgs.length > 0) {
    // Use provided files
    filePaths = fileArgs;
  } else {
    // Use default data files
    filePaths = await getDefaultDataFiles();
    console.log('No files specified, using default data files:');
    filePaths.forEach(path => console.log(`  ${path}`));
  }

  // Verify files exist
  for (const filePath of filePaths) {
    try {
      await readFile(filePath);
    } catch (error) {
      console.error(`File not found: ${filePath}`);
      process.exit(1);
    }
  }

  try {
    const config = getEnvConfig();
    console.log(`Connecting to Jira: ${config.url}`);
    console.log(`Project: ${config.projectKey}`);
    console.log(`User: ${config.username}`);
    
    const importer = new JiraImporter(config);
    await importer.importFromFiles(filePaths, dryRun);
    
  } catch (error) {
    console.error('Import failed:', error);
    process.exit(1);
  }
}

// Handle unhandled promise rejections
process.on('unhandledRejection', (reason, promise) => {
  console.error('Unhandled Rejection at:', promise, 'reason:', reason);
  process.exit(1);
});

// Handle uncaught exceptions
process.on('uncaughtException', (error) => {
  console.error('Uncaught Exception:', error);
  process.exit(1);
});

main().catch(error => {
  console.error('Fatal error:', error);
  process.exit(1);
});
