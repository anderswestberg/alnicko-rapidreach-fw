/**
 * Azure DevOps Work Item Types
 */

export interface AzureDevOpsWorkItem {
  // Basic fields
  summary: string;
  issueType: 'Epic' | 'Feature' | 'User Story' | 'Task' | 'Bug';
  description?: string;
  priority?: 'Low' | 'Medium' | 'High' | 'Critical';
  labels?: string[];
  storyPoints?: number;
  
  // User fields
  reporter?: string;
  assignee?: string;
  
  // Hierarchy and relationships
  issueId?: string;  // Internal reference ID for linking
  parent?: string;   // Reference to parent work item
  epicLink?: string; // Reference to epic
  
  // Status
  status?: 'To Do' | 'In Progress' | 'Done' | 'New' | 'Active' | 'Resolved' | 'Closed';
  
  // Additional fields
  components?: string[];
  epicName?: string;  // For epics only
  
  // Azure DevOps specific
  areaPath?: string;
  iterationPath?: string;
  acceptanceCriteria?: string;
}

export interface WorkItemPayload {
  op: 'add' | 'replace' | 'remove';
  path: string;
  value?: any;
  from?: string;
}

export interface CreateWorkItemResponse {
  id: number;
  fields: {
    'System.Title': string;
    'System.WorkItemType': string;
    'System.State': string;
    [key: string]: any;
  };
  _links: {
    self: { href: string };
    html: { href: string };
  };
}

export interface QueryResponse {
  workItems: Array<{ id: number; url: string }>;
}

export interface WorkItemDetails {
  id: number;
  fields: {
    'System.Title': string;
    'System.WorkItemType': string;
    'System.State': string;
    'System.AssignedTo'?: { displayName: string };
    'System.CreatedBy'?: { displayName: string };
    [key: string]: any;
  };
}

export interface Project {
  id: string;
  name: string;
  description?: string;
  state: string;
}

export interface AreaPath {
  id: string;
  name: string;
  path: string;
  hasChildren: boolean;
}

export interface IterationPath {
  id: string;
  name: string;
  path: string;
  attributes: {
    startDate?: string;
    finishDate?: string;
  };
}