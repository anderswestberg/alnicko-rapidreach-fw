import axios, { AxiosInstance } from 'axios';
import { 
  AzureDevOpsWorkItem, 
  WorkItemPayload, 
  CreateWorkItemResponse, 
  QueryResponse,
  WorkItemDetails,
  Project,
  AreaPath,
  IterationPath 
} from './types.js';

interface AzureDevOpsConfig {
  orgUrl: string;
  project: string;
  pat: string;
  apiVersion?: string;
}

export class AzureDevOpsClient {
  private client: AxiosInstance;
  private project: string;
  private apiVersion: string;
  private keyMapping: Map<string, number> = new Map();

  constructor(config: AzureDevOpsConfig) {
    this.project = config.project;
    this.apiVersion = config.apiVersion || '7.1';
    
    // Create axios instance with PAT authentication
    this.client = axios.create({
      baseURL: `${config.orgUrl}/${config.project}/_apis`,
      headers: {
        'Content-Type': 'application/json-patch+json',
        'Authorization': `Basic ${Buffer.from(`:${config.pat}`).toString('base64')}`
      },
      params: {
        'api-version': this.apiVersion
      }
    });

    // Add response interceptor for debugging
    this.client.interceptors.response.use(
      response => response,
      error => {
        if (error.response) {
          console.error('Azure DevOps API Error:', {
            status: error.response.status,
            statusText: error.response.statusText,
            data: error.response.data
          });
        }
        throw error;
      }
    );
  }

  /**
   * Get project details
   */
  async getProject(): Promise<Project> {
    const response = await this.client.get<Project>('', {
      baseURL: this.client.defaults.baseURL?.replace(`/${this.project}/_apis`, '/_apis/projects'),
      params: {
        'api-version': this.apiVersion
      }
    });
    return response.data;
  }

  /**
   * Create a work item
   */
  async createWorkItem(type: string, fields: WorkItemPayload[]): Promise<CreateWorkItemResponse> {
    try {
      const response = await this.client.post<CreateWorkItemResponse>(
        `/wit/workitems/$${type}`,
        fields,
        {
          headers: {
            'Content-Type': 'application/json-patch+json'
          }
        }
      );
      return response.data;
    } catch (error: any) {
      if (error.response?.data) {
        console.error('Azure DevOps API Error Response:', JSON.stringify(error.response.data, null, 2));
        console.error('Request payload:', JSON.stringify(fields, null, 2));
      }
      throw error;
    }
  }

  /**
   * Update a work item
   */
  async updateWorkItem(id: number, updates: WorkItemPayload[]): Promise<CreateWorkItemResponse> {
    const response = await this.client.patch<CreateWorkItemResponse>(
      `/wit/workitems/${id}`,
      updates,
      {
        headers: {
          'Content-Type': 'application/json-patch+json'
        }
      }
    );
    return response.data;
  }

  /**
   * Get work item by ID
   */
  async getWorkItem(id: number): Promise<WorkItemDetails> {
    const response = await this.client.get<WorkItemDetails>(`/wit/workitems/${id}`);
    return response.data;
  }

  /**
   * Query work items by title
   */
  async queryWorkItemsByTitle(title: string, workItemType?: string): Promise<WorkItemDetails[]> {
    const typeClause = workItemType ? ` AND [System.WorkItemType] = '${workItemType}'` : '';
    const query = {
      query: `SELECT [System.Id] FROM WorkItems WHERE [System.Title] = '${title}'${typeClause} AND [System.TeamProject] = '${this.project}'`
    };

    const queryResponse = await this.client.post<QueryResponse>('/wit/wiql', query);
    
    if (queryResponse.data.workItems.length === 0) {
      return [];
    }

    // Get full details for each work item
    const ids = queryResponse.data.workItems.map(wi => wi.id).join(',');
    const detailsResponse = await this.client.get<{ value: WorkItemDetails[] }>(`/wit/workitems?ids=${ids}`);
    
    return detailsResponse.data.value;
  }

  /**
   * Build work item payload from our data model
   */
  async buildWorkItemPayload(item: AzureDevOpsWorkItem, keyMapping: Record<string, number>): Promise<WorkItemPayload[]> {
    const fields: WorkItemPayload[] = [];

    // Title (Summary in our model)
    fields.push({
      op: 'add',
      path: '/fields/System.Title',
      value: item.summary
    });

    // Description
    if (item.description) {
      fields.push({
        op: 'add',
        path: '/fields/System.Description',
        value: item.description
      });
    }

    // Priority mapping
    if (item.priority) {
      const priorityMap: Record<string, number> = {
        'Critical': 1,
        'High': 2,
        'Medium': 3,
        'Low': 4
      };
      fields.push({
        op: 'add',
        path: '/fields/Microsoft.VSTS.Common.Priority',
        value: priorityMap[item.priority] || 3
      });
    }

    // Tags (labels)
    if (item.labels && item.labels.length > 0) {
      fields.push({
        op: 'add',
        path: '/fields/System.Tags',
        value: item.labels.join('; ')
      });
    }

    // Story Points (Effort)
    if (item.storyPoints !== undefined) {
      fields.push({
        op: 'add',
        path: '/fields/Microsoft.VSTS.Scheduling.Effort',
        value: item.storyPoints
      });
    }

    // Assigned To
    if (item.assignee) {
      fields.push({
        op: 'add',
        path: '/fields/System.AssignedTo',
        value: item.assignee
      });
    }

    // Area Path
    if (item.areaPath) {
      fields.push({
        op: 'add',
        path: '/fields/System.AreaPath',
        value: `${this.project}\\${item.areaPath}`
      });
    }

    // Iteration Path
    if (item.iterationPath) {
      fields.push({
        op: 'add',
        path: '/fields/System.IterationPath',
        value: `${this.project}\\${item.iterationPath}`
      });
    }

    // Acceptance Criteria
    if (item.acceptanceCriteria) {
      fields.push({
        op: 'add',
        path: '/fields/Microsoft.VSTS.Common.AcceptanceCriteria',
        value: item.acceptanceCriteria
      });
    }

    // Parent link (for hierarchical work items)
    if (item.parent) {
      let parentId = keyMapping[item.parent];
      if (!parentId && !item.parent.includes('-')) {
        // Try to resolve by issueId reference
        parentId = this.keyMapping.get(item.parent) || 0;
      }
      
      if (parentId) {
        fields.push({
          op: 'add',
          path: '/relations/-',
          value: {
            rel: 'System.LinkTypes.Hierarchy-Reverse',
            url: `${this.client.defaults.baseURL?.replace('/_apis', '')}/_apis/wit/workItems/${parentId}`
          }
        });
      }
    }

    return fields;
  }

  /**
   * Map our issue types to Azure DevOps work item types
   */
  mapIssueType(issueType: AzureDevOpsWorkItem['issueType']): string {
    const typeMap: Record<string, string> = {
      'Epic': 'Epic',
      'Feature': 'Feature', 
      'Story': 'User Story',
      'User Story': 'User Story',
      'Task': 'Task',
      'Sub-task': 'Task',
      'Bug': 'Bug'
    };
    
    return typeMap[issueType] || 'Task';
  }

  /**
   * Store key mapping for later reference
   */
  storeKeyMapping(issueId: string, workItemId: number): void {
    if (issueId) {
      this.keyMapping.set(issueId, workItemId);
    }
  }

  /**
   * Get stored key mapping
   */
  getKeyMapping(): Map<string, number> {
    return this.keyMapping;
  }

  /**
   * Resolve epic by name or summary
   */
  async resolveEpicKey(epicRef: string): Promise<number | null> {
    // First check if it's already in our mapping
    const mappedId = this.keyMapping.get(epicRef);
    if (mappedId) {
      return mappedId;
    }

    // Query for epics with matching title
    const epics = await this.queryWorkItemsByTitle(epicRef, 'Epic');
    
    if (epics.length > 0) {
      console.log(`Found epic '${epicRef}' with ID: ${epics[0].id}`);
      return epics[0].id;
    }

    // Try searching by epic name pattern
    const epicNameResults = await this.queryWorkItemsByTitle(`RapidReach ${epicRef}`, 'Epic');
    if (epicNameResults.length > 0) {
      console.log(`Found epic by pattern '${epicRef}' with ID: ${epicNameResults[0].id}`);
      return epicNameResults[0].id;
    }

    console.warn(`Could not resolve epic reference: ${epicRef}`);
    return null;
  }

  /**
   * Update work item state
   */
  async updateWorkItemState(id: number, state: string): Promise<void> {
    const updates: WorkItemPayload[] = [{
      op: 'replace',
      path: '/fields/System.State',
      value: state
    }];

    await this.updateWorkItem(id, updates);
  }
}
