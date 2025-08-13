#!/usr/bin/env node

import { AzureDevOpsImporter } from './importer.js';

function printUsage() {
  console.log(`
Azure DevOps Work Item Importer

Usage: npm run dev -- [options] [file1.json file2.json ...]

Options:
  --dry-run     Simulate import without creating work items
  --help        Show this help message

Environment Variables:
  AZDO_ORG_URL    Azure DevOps organization URL (required)
  AZDO_PROJECT    Project name (required)  
  AZDO_PAT        Personal Access Token (required)
  AZDO_API_VERSION API version (optional, defaults to 7.1)

Examples:
  # Import all files
  npm run dev

  # Dry run
  npm run dev -- --dry-run

  # Import specific files
  npm run dev -- data/platform-epic.json data/platform-done.json
`);
}

async function main() {
  // Parse command line arguments
  const args = process.argv.slice(2);
  
  if (args.includes('--help')) {
    printUsage();
    process.exit(0);
  }

  // Check required environment variables
  const orgUrl = process.env.AZDO_ORG_URL;
  const project = process.env.AZDO_PROJECT;
  const pat = process.env.AZDO_PAT;
  const apiVersion = process.env.AZDO_API_VERSION;

  if (!orgUrl || !project || !pat) {
    console.error('Error: Missing required environment variables');
    console.error('Please set AZDO_ORG_URL, AZDO_PROJECT, and AZDO_PAT');
    console.error('\nExample:');
    console.error('export AZDO_ORG_URL="https://dev.azure.com/your-org"');
    console.error('export AZDO_PROJECT="your-project"');
    console.error('export AZDO_PAT="your-personal-access-token"');
    process.exit(1);
  }

  // Parse options
  const dryRun = args.includes('--dry-run');
  const jsonFiles = args.filter(arg => arg.endsWith('.json'));

  // Create and run importer
  const importer = new AzureDevOpsImporter({
    orgUrl,
    project,
    pat,
    apiVersion,
    dryRun,
    jsonFiles: jsonFiles.length > 0 ? jsonFiles : undefined
  });

  try {
    await importer.importWorkItems();
  } catch (error) {
    console.error('Import failed:', error);
    process.exit(1);
  }
}

// Run if called directly
if (import.meta.url === `file://${process.argv[1]}`) {
  main().catch(console.error);
}