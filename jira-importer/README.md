# Jira Importer

A TypeScript ESM tool to import Jira issues via REST API with proper epic and parent linking. This tool reads issue data from JSON files and creates them in Jira with correct hierarchical relationships.

## Features

- **Epic/Parent Resolution**: Automatically resolves epic links and parent relationships by querying Jira
- **Hierarchical Import**: Creates issues in the correct order (Epics → Stories/Tasks → Sub-tasks)
- **Component Management**: Automatically creates missing components
- **Custom Field Support**: Handles Epic Name, Epic Link, and Story Points
- **Status Transitions**: Applies status changes after creation
- **Dry-run Mode**: Preview what will be created without making changes
- **Error Handling**: Comprehensive error handling with helpful messages

## Quick Start

### 1. Install Dependencies

```bash
cd jira-importer
npm install
```

### 2. Set Environment Variables

You can set environment variables in several ways:

**Option A: Direct export (temporary)**
```bash
export JIRA_URL="http://jira:8080"
export JIRA_USER="Anders Westberg"
export JIRA_TOKEN="your-password"
export JIRA_PROJECT="RDP"
```

**Option B: Create a config file (recommended)**
```bash
# Create config directory
mkdir -p ~/.config/rapidreach

# Create environment file
cat > ~/.config/rapidreach/jira.env << EOF
export JIRA_URL="http://jira:8080"
export JIRA_USER="Anders Westberg"
export JIRA_TOKEN="your-password"
export JIRA_PROJECT="RDP"
EOF

# Set secure permissions
chmod 600 ~/.config/rapidreach/jira.env

# Add to your shell profile
echo 'if [ -f ~/.config/rapidreach/jira.env ]; then source ~/.config/rapidreach/jira.env; fi' >> ~/.bashrc

# Reload your shell
source ~/.bashrc
```

### 3. Build and Run

```bash
# Build the TypeScript code
npm run build

# Run with default data files (dry-run first to preview)
npm run dev -- --dry-run

# Run for real
npm run dev

# Run with specific files
npm run dev data/platform-epic.json data/platform-done.json
```

## Environment Variables

| Variable | Description | Example |
|----------|-------------|---------|
| `JIRA_URL` | Jira server URL | `https://jira.company.com` |
| `JIRA_USER` | Jira username | `your-username` |
| `JIRA_TOKEN` | Password or API token | `your-password` |
| `JIRA_PROJECT` | Target project key | `RDP` |

## Data Format

Issues are defined in JSON format with the following structure:

```json
[
  {
    "summary": "Epic Title",
    "issueType": "Epic",
    "description": "Epic description...",
    "priority": "High",
    "labels": ["platform", "firmware"],
    "reporter": "Anders Westberg",
    "assignee": "Anders Westberg",
    "components": ["RapidReach"],
    "epicName": "Short Epic Name",
    "epicKey": "PLATFORM"
  },
  {
    "summary": "Story Title",
    "issueType": "Story",
    "description": "Story description...",
    "priority": "Medium",
    "labels": ["feature"],
    "storyPoints": 8,
    "reporter": "Anders Westberg",
    "assignee": "Anders Westberg",
    "issueId": "4001",
    "epicLink": "PLATFORM",
    "status": "To Do",
    "components": ["RapidReach"]
  },
  {
    "summary": "Sub-task Title",
    "issueType": "Sub-task",
    "description": "Sub-task description...",
    "priority": "Low",
    "labels": ["implementation"],
    "storyPoints": 3,
    "reporter": "Anders Westberg",
    "assignee": "Anders Westberg",
    "issueId": "4101",
    "parent": "4001",
    "status": "Done",
    "components": ["RapidReach"]
  }
]
```

### Field Descriptions

- **summary**: Issue title (required)
- **issueType**: Issue type (Epic, Story, Task, Sub-task)
- **description**: Issue description
- **priority**: Priority level (Critical, High, Medium, Low)
- **labels**: Array of labels/tags
- **storyPoints**: Story points (number)
- **reporter/assignee**: Jira usernames
- **issueId**: Internal ID for linking within the import
- **parent**: Reference to parent issue (for sub-tasks)
- **epicLink**: Reference to epic issue
- **epicName**: Epic name (for Epic issues only)
- **epicKey**: Known epic key (optional)
- **status**: Target status (To Do, In Progress, Done)
- **components**: Array of component names

## Import Process

The tool imports issues in this order to ensure dependencies are satisfied:

1. **Epics**: Created first since they have no dependencies
2. **Stories/Tasks**: Created next, can link to epics
3. **Sub-tasks**: Created last, link to their parent stories/tasks

## Reference Resolution

The tool resolves epic and parent references in multiple ways:

### Epic Links
1. Internal reference lookup (e.g., "PLATFORM", "SMART-SPEAKER")
2. Direct key lookup (e.g., "RDP-93")
3. Search by epic name or summary
4. Use internal issue ID mapping from current import

### Parent References
1. Direct key lookup (e.g., "RDP-42")
2. Search by summary in the same project
3. Use internal issue ID mapping from current import

## Data Files

The project includes the following data files:

- `data/platform-epic.json`: RapidReach Device Platform epic
- `data/platform-done.json`: Completed platform work
- `data/platform-remaining.json`: Remaining platform work
- `data/smart-speaker-epic.json`: Smart Speaker product epic
- `data/smart-speaker-done.json`: Completed smart speaker work
- `data/smart-speaker-remaining.json`: Remaining smart speaker work

## Usage Examples

```bash
# Dry-run with all default files
npm run dev -- --dry-run

# Import platform only
npm run dev data/platform-epic.json data/platform-done.json data/platform-remaining.json

# Import smart speaker only
npm run dev data/smart-speaker-epic.json data/smart-speaker-done.json data/smart-speaker-remaining.json

# Import everything
npm run dev

# Get help
npm run dev -- --help

# Test your Jira credentials
curl -s -u "$JIRA_USER:$JIRA_TOKEN" "$JIRA_URL/rest/api/2/myself" | python3 -m json.tool
```

## Development

```bash
# Install dependencies
npm install

# Build TypeScript
npm run build

# Run in development mode (builds and runs)
npm run dev

# Clean build artifacts
npm run clean
```

## Error Handling

The tool includes comprehensive error handling:

- **Missing Environment Variables**: Clear error messages for setup issues
- **File Not Found**: Validates all input files before starting
- **Jira API Errors**: Detailed error messages for API failures
- **Missing Dependencies**: Clear errors when parent/epic references can't be resolved
- **Invalid Data**: Validation of required fields

## Limitations

- Requires Jira Server/Data Center (tested with 9.8.1)
- Custom fields must exist (Epic Name, Epic Link)
- Story Points field currently disabled (screen configuration issue)
- User accounts must exist in Jira with exact usernames
- Project must exist and be accessible
- Components field is required for this project

## Troubleshooting

### Authentication Issues
- Verify JIRA_URL, JIRA_USER, and JIRA_TOKEN
- Check network connectivity to Jira server
- Ensure user has project permissions

### Custom Field Errors
- Ensure Jira Software is installed (for Epic fields)
- Check that custom fields exist and are accessible

### Parent/Epic Resolution Issues
- Use dry-run mode to see what references will be resolved
- Check that parent issues exist or are included in the import
- Verify epic keys are correct

### Status Transition Errors
- Check that target statuses exist in your workflow
- Ensure user has permission to transition issues
- Some statuses may require additional fields

## Recent Import Success

✅ **Last successful import (December 2024):**
- **2 Epics**: RDP-93 (Platform), RDP-94 (Smart Speaker)
- **15 Stories/Tasks**: 9 platform (done), 6 smart speaker (to do)
- **55 Sub-tasks**: All properly linked with correct parent relationships
- **Epic Links**: Working correctly with internal reference system
- **Hierarchy**: Story → Sub-task (2-level, Jira-compliant)

The tool successfully creates a complete project structure with proper epic linking and hierarchical relationships suitable for AI-assisted development.
