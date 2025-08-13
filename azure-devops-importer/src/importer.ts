import { readFileSync } from 'fs';
import { join } from 'path';
import { AzureDevOpsClient } from './azure-devops-client.js';
import { AzureDevOpsWorkItem } from './types.js';

interface ImporterConfig {
  orgUrl: string;
  project: string;
  pat: string;
  apiVersion?: string;
  dryRun?: boolean;
  jsonFiles?: string[];
}

export class AzureDevOpsImporter {
  private client: AzureDevOpsClient;
  private config: ImporterConfig;
  private keyMapping: Record<string, number> = {};

  constructor(config: ImporterConfig) {
    this.config = config;
    this.client = new AzureDevOpsClient({
      orgUrl: config.orgUrl,
      project: config.project,
      pat: config.pat,
      apiVersion: config.apiVersion
    });
  }

  async importWorkItems(): Promise<void> {
    try {
      // Verify project access
      console.log(`Connecting to Azure DevOps project: ${this.config.project}...`);
      const project = await this.client.getProject();
      console.log(`✓ Connected to project: ${project.name} (${project.state})`);

      // Get files to import
      const jsonFiles = this.config.jsonFiles || [
        'data/platform-epic.json',
        'data/smart-speaker-epic.json',
        'data/platform-done.json',
        'data/platform-remaining.json',
        'data/smart-speaker-done.json',
        'data/smart-speaker-remaining.json'
      ];

      console.log(`\nImporting from ${jsonFiles.length} JSON files...`);

      // Phase 1: Import epics first
      console.log('\n=== Phase 1: Importing Epics ===');
      for (const file of jsonFiles) {
        if (file.includes('epic')) {
          await this.importFile(file, 'epic');
        }
      }

      // Phase 2: Import features and user stories
      console.log('\n=== Phase 2: Importing Features and User Stories ===');
      for (const file of jsonFiles) {
        if (!file.includes('epic')) {
          await this.importFile(file, 'story');
        }
      }

      // Phase 3: Import tasks and sub-tasks
      console.log('\n=== Phase 3: Importing Tasks ===');
      for (const file of jsonFiles) {
        if (!file.includes('epic')) {
          await this.importFile(file, 'task');
        }
      }

      console.log('\n✓ Import completed successfully!');
      console.log(`\nCreated work items:`);
      for (const [issueId, workItemId] of Object.entries(this.keyMapping)) {
        console.log(`  ${issueId} → Work Item #${workItemId}`);
      }

    } catch (error) {
      console.error('Import failed:', error);
      throw error;
    }
  }

  private async importFile(filename: string, phase: 'epic' | 'story' | 'task'): Promise<void> {
    try {
      console.log(`\nProcessing ${filename}...`);
      
      const filePath = join(process.cwd(), filename);
      const content = readFileSync(filePath, 'utf8');
      const items: AzureDevOpsWorkItem[] = JSON.parse(content);

      if (!Array.isArray(items)) {
        console.warn(`Skipping ${filename}: not an array`);
        return;
      }

      console.log(`Found ${items.length} items in ${filename}`);

      // Filter items based on phase
      const filteredItems = items.filter(item => {
        if (phase === 'epic') {
          return item.issueType === 'Epic';
        } else if (phase === 'story') {
          return ['Feature', 'Story', 'User Story'].includes(item.issueType) && !item.parent;
        } else {
          return ['Task', 'Sub-task'].includes(item.issueType) || 
                 (item.parent && ['Feature', 'Story', 'User Story'].includes(item.issueType));
        }
      });

      if (filteredItems.length === 0) {
        console.log(`No ${phase} items found in ${filename}`);
        return;
      }

      console.log(`Importing ${filteredItems.length} ${phase} items...`);

      for (const item of filteredItems) {
        await this.createWorkItem(item);
      }

    } catch (error) {
      console.error(`Error processing ${filename}:`, error);
      throw error;
    }
  }

  private async createWorkItem(item: AzureDevOpsWorkItem): Promise<void> {
    console.log(`\nCreating ${item.issueType}: ${item.summary}`);

    if (this.config.dryRun) {
      console.log(`[DRY RUN] Would create:`, {
        type: item.issueType,
        title: item.summary,
        parent: item.parent,
        epicLink: item.epicLink
      });
      
      // Simulate ID assignment for dry run
      if (item.issueId) {
        this.keyMapping[item.issueId] = Math.floor(Math.random() * 10000);
      }
      return;
    }

    try {
      // Map issue type to Azure DevOps work item type
      const workItemType = this.client.mapIssueType(item.issueType);
      
      // Build the payload
      const payload = await this.client.buildWorkItemPayload(item, this.keyMapping);
      
      // Handle epic linking for stories
      if (item.epicLink && ['Feature', 'Story', 'User Story'].includes(item.issueType)) {
        const epicId = await this.resolveEpicReference(item.epicLink);
        if (epicId) {
          payload.push({
            op: 'add',
            path: '/relations/-',
            value: {
              rel: 'System.LinkTypes.Hierarchy-Reverse',
              url: `${this.config.orgUrl}/${this.config.project}/_apis/wit/workItems/${epicId}`
            }
          });
        }
      }

      // Create the work item
      const response = await this.client.createWorkItem(workItemType, payload);
      console.log(`✓ Created ${workItemType} #${response.id}: ${response.fields['System.Title']}`);

      // Store mapping
      if (item.issueId) {
        this.keyMapping[item.issueId] = response.id;
        this.client.storeKeyMapping(item.issueId, response.id);
      }

      // Update state if needed
      if (item.status && item.status !== 'New' && item.status !== 'To Do') {
        await this.updateWorkItemStatus(response.id, item.status);
      }

    } catch (error: any) {
      console.error(`Failed to create ${item.issueType}: ${item.summary}`, error.message);
      throw error;
    }
  }

  private async resolveEpicReference(epicRef: string): Promise<number | null> {
    // Check local mapping first
    if (this.keyMapping[epicRef]) {
      return this.keyMapping[epicRef];
    }

    // Try to resolve through client
    return await this.client.resolveEpicKey(epicRef);
  }

  private async updateWorkItemStatus(id: number, status: string): Promise<void> {
    // Map our status to Azure DevOps states
    const stateMap: Record<string, string> = {
      'To Do': 'New',
      'In Progress': 'Active',
      'Done': 'Closed',
      'Resolved': 'Resolved'
    };

    const targetState = stateMap[status] || status;
    
    try {
      await this.client.updateWorkItemState(id, targetState);
      console.log(`  → Updated state to: ${targetState}`);
    } catch (error) {
      console.warn(`  ⚠ Could not update state to ${targetState}:`, error);
    }
  }
}