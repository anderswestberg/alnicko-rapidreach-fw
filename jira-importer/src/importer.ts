import { readFile } from 'fs/promises';
import { JiraClient } from './jira-client.js';
import { JiraIssue, JiraConfig, IssueKeyMapping } from './types.js';

export class JiraImporter {
  private client: JiraClient;
  private keyMapping: IssueKeyMapping = {};

  constructor(config: JiraConfig) {
    this.client = new JiraClient(config);
  }

  /**
   * Load issues from JSON file
   */
  async loadIssuesFromFile(filePath: string): Promise<JiraIssue[]> {
    try {
      const content = await readFile(filePath, 'utf-8');
      return JSON.parse(content);
    } catch (error) {
      throw new Error(`Failed to load issues from ${filePath}: ${error}`);
    }
  }

  /**
   * Import epics first (they have no dependencies)
   */
  async importEpics(issues: JiraIssue[], dryRun: boolean = false): Promise<void> {
    const epics = issues.filter(issue => issue.issueType.toLowerCase() === 'epic');
    
    console.log(`\n=== Importing ${epics.length} Epics ===`);
    
    for (const epic of epics) {
      if (dryRun) {
        console.log(`[DRY-RUN] Epic: ${epic.summary}`);
        if (epic.issueId) {
          this.keyMapping[epic.issueId] = epic.epicKey || 'DRY-RUN-KEY';
        }
        continue;
      }

      try {
        const payload = await this.client.buildIssuePayload(epic, this.keyMapping);
        const response = await this.client.createIssue(payload);
        
        console.log(`✓ Created Epic: ${response.key} - ${epic.summary}`);
        
        // Map issueId to actual Jira key
        if (epic.issueId) {
          this.keyMapping[epic.issueId] = response.key;
        }
        
        // Also map epicKey if provided (for convenience)
        if (epic.epicKey) {
          this.keyMapping[epic.epicKey] = response.key;
        }

        // Set status if specified
        if (epic.status && epic.status.toLowerCase() !== 'to do') {
          await this.client.transitionIssue(response.key, epic.status);
          console.log(`  → Status set to: ${epic.status}`);
        }
      } catch (error) {
        console.error(`✗ Failed to create Epic: ${epic.summary}`, error);
        throw error;
      }
    }
  }

  /**
   * Import stories and tasks (no parent dependencies)
   */
  async importStoriesAndTasks(issues: JiraIssue[], dryRun: boolean = false): Promise<void> {
    const storiesAndTasks = issues.filter(issue => 
      issue.issueType.toLowerCase() !== 'epic' && 
      issue.issueType.toLowerCase() !== 'sub-task' &&
      !issue.parent
    );
    
    console.log(`\n=== Importing ${storiesAndTasks.length} Stories and Tasks ===`);
    
    for (const issue of storiesAndTasks) {
      if (dryRun) {
        console.log(`[DRY-RUN] ${issue.issueType}: ${issue.summary}`);
        if (issue.issueId) {
          this.keyMapping[issue.issueId] = 'DRY-RUN-KEY';
        }
        continue;
      }

      try {
        const payload = await this.client.buildIssuePayload(issue, this.keyMapping);
        const response = await this.client.createIssue(payload);
        
        console.log(`✓ Created ${issue.issueType}: ${response.key} - ${issue.summary}`);
        
        // Map issueId to actual Jira key
        if (issue.issueId) {
          this.keyMapping[issue.issueId] = response.key;
        }

        // Set status if specified
        if (issue.status && issue.status.toLowerCase() !== 'to do') {
          await this.client.transitionIssue(response.key, issue.status);
          console.log(`  → Status set to: ${issue.status}`);
        }
      } catch (error) {
        console.error(`✗ Failed to create ${issue.issueType}: ${issue.summary}`, error);
        throw error;
      }
    }
  }

  /**
   * Import sub-tasks (depend on parent stories/tasks)
   */
  async importSubTasks(issues: JiraIssue[], dryRun: boolean = false): Promise<void> {
    const subTasks = issues.filter(issue => 
      issue.issueType.toLowerCase() === 'sub-task' || issue.parent
    );
    
    console.log(`\n=== Importing ${subTasks.length} Sub-tasks ===`);
    
    for (const issue of subTasks) {
      if (dryRun) {
        console.log(`[DRY-RUN] Sub-task: ${issue.summary} (parent: ${issue.parent})`);
        continue;
      }

      try {
        const payload = await this.client.buildIssuePayload(issue, this.keyMapping);
        const response = await this.client.createIssue(payload);
        
        const parentKey = this.keyMapping[issue.parent || ''] || issue.parent;
        console.log(`✓ Created Sub-task: ${response.key} - ${issue.summary} (parent: ${parentKey})`);

        // Set status if specified
        if (issue.status && issue.status.toLowerCase() !== 'to do') {
          await this.client.transitionIssue(response.key, issue.status);
          console.log(`  → Status set to: ${issue.status}`);
        }
      } catch (error) {
        console.error(`✗ Failed to create Sub-task: ${issue.summary} (parent: ${issue.parent})`, error);
        throw error;
      }
    }
  }

  /**
   * Import issues from multiple JSON files in the correct order
   */
  async importFromFiles(filePaths: string[], dryRun: boolean = false): Promise<void> {
    console.log(`${dryRun ? '[DRY-RUN] ' : ''}Starting Jira import from ${filePaths.length} files...`);
    
    // Load all issues from all files
    const allIssues: JiraIssue[] = [];
    for (const filePath of filePaths) {
      console.log(`Loading issues from: ${filePath}`);
      const issues = await this.loadIssuesFromFile(filePath);
      allIssues.push(...issues);
    }

    console.log(`Loaded ${allIssues.length} total issues`);

    // Import in dependency order
    await this.importEpics(allIssues, dryRun);
    await this.importStoriesAndTasks(allIssues, dryRun);
    await this.importSubTasks(allIssues, dryRun);

    if (!dryRun) {
      console.log('\n=== Import Summary ===');
      console.log(`Total issues created: ${Object.keys(this.keyMapping).length}`);
      console.log('Key mappings:');
      Object.entries(this.keyMapping).forEach(([issueId, jiraKey]) => {
        console.log(`  ${issueId} → ${jiraKey}`);
      });
    }
    
    console.log(`\n${dryRun ? 'Dry-run' : 'Import'} completed successfully!`);
  }

  /**
   * Get the current key mapping (useful for debugging)
   */
  getKeyMapping(): IssueKeyMapping {
    return { ...this.keyMapping };
  }
}
