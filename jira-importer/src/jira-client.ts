import axios, { AxiosInstance } from 'axios';
import {
  JiraConfig,
  JiraField,
  JiraProject,
  JiraComponent,
  JiraIssueResponse,
  JiraTransition,
  CreateIssuePayload,
  JiraIssue
} from './types.js';

export class JiraClient {
  private client: AxiosInstance;
  private config: JiraConfig;
  private fieldsCache?: JiraField[];
  private projectCache?: JiraProject;

  constructor(config: JiraConfig) {
    this.config = config;
    this.client = axios.create({
      baseURL: `${config.url}/rest/api/2`,
      auth: {
        username: config.username,
        password: config.token
      },
      headers: {
        'Content-Type': 'application/json',
        'Accept': 'application/json'
      },
      timeout: 30000
    });
  }

  /**
   * Get all fields from Jira and cache them
   */
  async getFields(): Promise<JiraField[]> {
    if (this.fieldsCache) {
      return this.fieldsCache;
    }

    const response = await this.client.get<JiraField[]>('/field');
    this.fieldsCache = response.data;
    return this.fieldsCache;
  }

  /**
   * Find a field ID by name
   */
  async getFieldId(fieldName: string): Promise<string | null> {
    const fields = await this.getFields();
    const field = fields.find(f => f.name === fieldName);
    return field?.id || null;
  }

  /**
   * Get project information including components
   */
  async getProject(projectKey: string = this.config.projectKey): Promise<JiraProject> {
    if (this.projectCache && this.projectCache.key === projectKey) {
      return this.projectCache;
    }

    const response = await this.client.get<JiraProject>(`/project/${projectKey}`);
    this.projectCache = response.data;
    return this.projectCache;
  }

  /**
   * Ensure components exist in the project, create if missing
   */
  async ensureComponents(componentNames: string[]): Promise<JiraComponent[]> {
    const project = await this.getProject();
    const existingComponents = project.components || [];
    const results: JiraComponent[] = [];

    for (const name of componentNames) {
      const trimmedName = name.trim();
      if (!trimmedName) continue;

      let component = existingComponents.find(c => c.name === trimmedName);
      
      if (!component) {
        // Create the component
        const response = await this.client.post<JiraComponent>('/component', {
          name: trimmedName,
          project: project.key
        });
        component = response.data;
        existingComponents.push(component);
      }
      
      results.push(component);
    }

    return results;
  }

  /**
   * Search for an issue by summary or key to resolve epic/parent references
   */
  async findIssueByKey(issueKey: string): Promise<JiraIssueResponse | null> {
    try {
      const response = await this.client.get<JiraIssueResponse>(`/issue/${issueKey}`);
      return response.data;
    } catch (error) {
      if (axios.isAxiosError(error) && error.response?.status === 404) {
        return null;
      }
      throw error;
    }
  }

  /**
   * Search for issues by JQL
   */
  async searchIssues(jql: string): Promise<JiraIssueResponse[]> {
    const response = await this.client.get('/search', {
      params: {
        jql,
        fields: 'key,summary,issuetype',
        maxResults: 50
      }
    });
    return response.data.issues || [];
  }

  /**
   * Resolve epic link - first try as key, then search by epic name
   */
  async resolveEpicKey(epicReference: string): Promise<string | null> {
    // First try direct key lookup
    const directIssue = await this.findIssueByKey(epicReference);
    if (directIssue) {
      return directIssue.key;
    }

    // Search by epic name or summary
    const jql = `project = "${this.config.projectKey}" AND issuetype = Epic AND (summary ~ "${epicReference}" OR "Epic Name" ~ "${epicReference}")`;
    const searchResults = await this.searchIssues(jql);
    
    return searchResults.length > 0 ? searchResults[0].key : null;
  }

  /**
   * Resolve parent key - try direct key lookup first, then search
   */
  async resolveParentKey(parentReference: string): Promise<string | null> {
    // First try direct key lookup
    const directIssue = await this.findIssueByKey(parentReference);
    if (directIssue) {
      return directIssue.key;
    }

    // Search by summary in the same project
    const jql = `project = "${this.config.projectKey}" AND summary ~ "${parentReference}"`;
    const searchResults = await this.searchIssues(jql);
    
    return searchResults.length > 0 ? searchResults[0].key : null;
  }

  /**
   * Create an issue
   */
  async createIssue(payload: CreateIssuePayload): Promise<JiraIssueResponse> {
    try {
      const response = await this.client.post<JiraIssueResponse>('/issue', payload);
      return response.data;
    } catch (error: any) {
      if (error.response?.data) {
        console.error('Jira API Error Response:', JSON.stringify(error.response.data, null, 2));
        console.error('Request payload:', JSON.stringify(payload, null, 2));
      }
      throw error;
    }
  }

  /**
   * Get available transitions for an issue
   */
  async getTransitions(issueKey: string): Promise<JiraTransition[]> {
    const response = await this.client.get(`/issue/${issueKey}/transitions`);
    return response.data.transitions || [];
  }

  /**
   * Transition an issue to a new status
   */
  async transitionIssue(issueKey: string, targetStatus: string): Promise<void> {
    const transitions = await this.getTransitions(issueKey);
    const transition = transitions.find(t => 
      t.name.toLowerCase() === targetStatus.toLowerCase() ||
      t.to.name.toLowerCase() === targetStatus.toLowerCase()
    );

    if (transition) {
      await this.client.post(`/issue/${issueKey}/transitions`, {
        transition: { id: transition.id }
      });
    } else {
      console.warn(`No transition found for ${issueKey} to status: ${targetStatus}`);
    }
  }

  /**
   * Build issue payload from JiraIssue data
   */
  async buildIssuePayload(issue: JiraIssue, keyMapping: { [issueId: string]: string } = {}): Promise<CreateIssuePayload> {
    const epicNameFieldId = await this.getFieldId('Epic Name');
    const epicLinkFieldId = await this.getFieldId('Epic Link');
    const storyPointsFieldId = await this.getFieldId('Story Points');

    // Ensure components exist
    const components = await this.ensureComponents(issue.components);

    const fields: Record<string, any> = {
      project: { key: this.config.projectKey },
      summary: issue.summary,
      issuetype: { name: issue.issueType },
      description: issue.description || ''
    };

    // Priority
    if (issue.priority) {
      fields.priority = { name: issue.priority };
    }

    // Labels
    if (issue.labels && issue.labels.length > 0) {
      fields.labels = issue.labels;
    }

    // Reporter and Assignee (use name for Server/DC)
    if (issue.reporter) {
      fields.reporter = { name: issue.reporter };
    }
    if (issue.assignee) {
      fields.assignee = { name: issue.assignee };
    }

    // Components
    if (components.length > 0) {
      fields.components = components.map(c => ({ name: c.name }));
    }

    // Story Points - Skip for now as they're not on the appropriate screen
    // if (storyPointsFieldId && issue.storyPoints !== undefined) {
    //   fields[storyPointsFieldId] = issue.storyPoints;
    // }

    // Handle Epic-specific fields
    if (issue.issueType.toLowerCase() === 'epic') {
      if (epicNameFieldId && issue.epicName) {
        fields[epicNameFieldId] = issue.epicName;
      }
    } else {
      // Epic Link for non-epics
      if (epicLinkFieldId && issue.epicLink) {
        // Try to resolve epic key from mapping first, then from Jira
        let epicKey = keyMapping[issue.epicLink];
        if (!epicKey && !issue.epicLink.includes('-')) {
          // If not in mapping and not a key format, search Jira
          const resolvedKey = await this.resolveEpicKey(issue.epicLink);
          if (resolvedKey) {
            epicKey = resolvedKey;
          }
        } else if (!epicKey) {
          epicKey = issue.epicLink; // Use as-is if it looks like a key
        }
        
        if (epicKey) {
          fields[epicLinkFieldId] = epicKey;
        }
      }
    }

    // Handle parent for sub-tasks
    if (issue.parent) {
      // Force sub-task type if parent is specified
      fields.issuetype = { name: 'Sub-task' };
      
      // Try to resolve parent key from mapping first, then from Jira
      let parentKey = keyMapping[issue.parent];
      if (!parentKey && !issue.parent.includes('-')) {
        // If not in mapping and not a key format, search Jira
        const resolvedKey = await this.resolveParentKey(issue.parent);
        if (resolvedKey) {
          parentKey = resolvedKey;
        }
      } else if (!parentKey) {
        parentKey = issue.parent; // Use as-is if it looks like a key
      }
      
      if (parentKey) {
        fields.parent = { key: parentKey };
      } else {
        throw new Error(`Could not resolve parent reference: ${issue.parent}`);
      }
    }

    return { fields };
  }
}
