# Azure DevOps Work Item Importer

Import work items to Azure DevOps from JSON files with proper hierarchy and epic linking.

## Features

- ✅ Imports Epics, Features, User Stories, and Tasks
- ✅ Maintains proper hierarchy (Epic → Feature/Story → Task)
- ✅ Maps fields appropriately (priority, tags, effort/story points)
- ✅ Handles status transitions
- ✅ Dry-run mode for testing
- ✅ Phased import for correct dependency order

## Prerequisites

- Node.js 18+
- Azure DevOps account with a project
- Personal Access Token (PAT) with Work Items read/write permissions

## Setup

### 1. Install Dependencies

```bash
cd azure-devops-importer
npm install
```

### 2. Set Environment Variables

You can set environment variables in several ways:

**Option A: Direct export (temporary)**
```bash
export AZDO_ORG_URL="https://dev.azure.com/your-organization"
export AZDO_PROJECT="YourProjectName"
export AZDO_PAT="your-personal-access-token"
export AZDO_API_VERSION="7.1"  # Optional, defaults to 7.1
```

**Option B: Create a config file (recommended)**
```bash
# Create config directory
mkdir -p ~/.config/rapidreach

# Create environment file
cat > ~/.config/rapidreach/azdo.env << EOF
export AZDO_ORG_URL="https://dev.azure.com/your-organization"
export AZDO_PROJECT="YourProjectName"
export AZDO_PAT="your-personal-access-token"
export AZDO_API_VERSION="7.1"
EOF

# Set secure permissions
chmod 600 ~/.config/rapidreach/azdo.env

# Add to your .bashrc or .profile
echo "[ -f ~/.config/rapidreach/azdo.env ] && source ~/.config/rapidreach/azdo.env" >> ~/.bashrc

# Load immediately
source ~/.config/rapidreach/azdo.env
```

**Option C: Use .env file (for development)**
```bash
cp env.example .env
# Edit .env with your values
```

### 3. Generate a Personal Access Token

1. Go to Azure DevOps → User Settings → Personal Access Tokens
2. Click "New Token"
3. Give it a name (e.g., "Work Item Importer")
4. Set expiration as needed
5. Select scopes:
   - Work Items: Read & Write
   - Project and Team: Read (optional)
6. Copy the token immediately (you won't see it again)

## Usage

### Build the Project

```bash
npm run build
```

### Run Import

```bash
# Import all JSON files from data/ directory
npm run dev

# Dry run (preview without creating)
npm run dev -- --dry-run

# Import specific files
npm run dev -- data/platform-epic.json data/platform-done.json

# Get help
npm run dev -- --help

# Test your Azure DevOps credentials
curl -u ":$AZDO_PAT" "$AZDO_ORG_URL/_apis/projects/$AZDO_PROJECT?api-version=7.1"
```

## JSON Data Format

### Epic Example
```json
[
  {
    "summary": "RapidReach Device Platform",
    "issueType": "Epic",
    "description": "Core platform functionality for RapidReach devices",
    "priority": "High",
    "labels": ["platform", "core"],
    "reporter": "Anders Westberg",
    "assignee": "Anders Westberg",
    "issueId": "PLATFORM",
    "epicName": "RapidReach Device Platform",
    "status": "Active",
    "components": ["RapidReach"]
  }
]
```

### User Story Example
```json
[
  {
    "summary": "Audio Playback System",
    "issueType": "User Story",
    "description": "As a user, I want the device to play audio files",
    "priority": "High",
    "labels": ["audio", "core"],
    "storyPoints": 8,
    "reporter": "Anders Westberg",
    "assignee": "Anders Westberg",
    "issueId": "4001",
    "epicLink": "PLATFORM",
    "status": "Done",
    "components": ["RapidReach"]
  }
]
```

### Task Example
```json
[
  {
    "summary": "Implement Opus Decoder",
    "issueType": "Task",
    "description": "Integrate Opus audio decoder library",
    "priority": "High",
    "labels": ["audio", "codec"],
    "storyPoints": 5,
    "reporter": "Anders Westberg",
    "assignee": "Anders Westberg",
    "issueId": "4011",
    "parent": "4001",
    "status": "Done"
  }
]
```

## Field Mappings

| JSON Field | Azure DevOps Field | Notes |
|------------|-------------------|-------|
| summary | System.Title | Required |
| issueType | System.WorkItemType | Epic, Feature, User Story, Task |
| description | System.Description | Optional |
| priority | Microsoft.VSTS.Common.Priority | 1-4 (Critical=1, High=2, Medium=3, Low=4) |
| labels | System.Tags | Semicolon-separated in Azure DevOps |
| storyPoints | Microsoft.VSTS.Scheduling.Effort | Story points/effort |
| assignee | System.AssignedTo | Must be valid Azure DevOps user |
| parent | Parent Link | Hierarchy relationship |
| epicLink | Parent Link (to Epic) | For stories under epics |
| status | System.State | New, Active, Resolved, Closed |

## Work Item Type Mapping

| JSON issueType | Azure DevOps Type |
|----------------|------------------|
| Epic | Epic |
| Feature | Feature |
| Story | User Story |
| User Story | User Story |
| Task | Task |
| Sub-task | Task |

## Import Process

The importer works in three phases:

1. **Phase 1: Epics** - Creates all epics first
2. **Phase 2: Features/Stories** - Creates features and user stories, linking to epics
3. **Phase 3: Tasks** - Creates tasks and links them to parent stories

## Key Resolution

The importer resolves references in this order:

### Epic Links
1. Internal reference lookup (e.g., "PLATFORM", "SMART-SPEAKER")
2. Query by title in Azure DevOps
3. Use internal issue ID mapping from current import

### Parent Links
1. Internal issue ID mapping (e.g., "4001" → Work Item #123)
2. Direct work item ID if already known

## Azure DevOps Specific Features

### Area and Iteration Paths
```json
{
  "areaPath": "Web\\Frontend",
  "iterationPath": "Sprint 1"
}
```

### Acceptance Criteria
```json
{
  "acceptanceCriteria": "- Audio plays without distortion\n- Supports Opus format\n- Volume control works"
}
```

## Troubleshooting

### Authentication Errors
- Verify PAT has correct permissions
- Check PAT hasn't expired
- Ensure organization URL is correct

### Work Item Creation Errors
- Check required fields are present (title, type)
- Verify assignee exists in Azure DevOps
- Ensure work item types are enabled in project

### Parent Linking Issues
- Parents must be created before children
- Use correct issueId references
- Check hierarchy is valid (Epic → Story → Task)

### Status Transition Errors
- Some states may require specific transitions
- Check project's workflow configuration
- User must have permissions for state changes

## Limitations

- Requires Azure DevOps Services or Server 2019+
- Custom fields must be configured in the code
- Attachments are not supported in this version
- Board column mapping not included

## Testing Azure DevOps Connection

```bash
# Test authentication
curl -u ":$AZDO_PAT" "$AZDO_ORG_URL/_apis/projects?api-version=7.1" | jq .

# Get project details
curl -u ":$AZDO_PAT" "$AZDO_ORG_URL/$AZDO_PROJECT/_apis/wit/workitemtypes?api-version=7.1" | jq .

# List work items
curl -u ":$AZDO_PAT" "$AZDO_ORG_URL/$AZDO_PROJECT/_apis/wit/wiql?api-version=7.1" \
  -H "Content-Type: application/json" \
  -d '{"query": "SELECT [System.Id], [System.Title] FROM WorkItems WHERE [System.TeamProject] = '\"$AZDO_PROJECT\"'"}' | jq .
```

## Recent Import Success

✅ **Successfully imports to Azure DevOps:**
- **Epics**: With proper hierarchy setup
- **User Stories**: Linked to epics  
- **Tasks**: Linked to parent stories
- **Field Mapping**: Priority, tags, effort, assignments
- **Status**: Proper state transitions

The tool successfully creates a complete project structure with proper hierarchy and relationships in Azure DevOps.