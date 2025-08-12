export interface JiraIssue {
  summary: string;
  issueType: string;
  description: string;
  priority: string;
  labels: string[];
  storyPoints?: number;
  reporter: string;
  assignee: string;
  issueId?: string;
  parent?: string;
  epicLink?: string;
  epicName?: string;
  epicKey?: string;
  status?: string;
  components: string[];
}

export interface JiraField {
  id: string;
  name: string;
  custom: boolean;
  schema?: {
    type: string;
    system?: string;
  };
}

export interface JiraProject {
  id: string;
  key: string;
  name: string;
  components: JiraComponent[];
}

export interface JiraComponent {
  id: string;
  name: string;
}

export interface JiraIssueResponse {
  id: string;
  key: string;
  self: string;
  fields: Record<string, any>;
}

export interface JiraTransition {
  id: string;
  name: string;
  to: {
    id: string;
    name: string;
  };
}

export interface CreateIssuePayload {
  fields: Record<string, any>;
}

export interface IssueKeyMapping {
  [issueId: string]: string;
}

export interface JiraConfig {
  url: string;
  username: string;
  token: string;
  projectKey: string;
}
