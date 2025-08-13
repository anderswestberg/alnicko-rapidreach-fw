### Jira import guide (Server/Data Center 9.8.1)

This folder contains CSVs and instructions to import an Epi### Troubleshooting

- Error: "Cannot add value ... to CustomField Parent Link"
  - You mapped Parent to Parent Link. Re‑import and map Parent → Parent id.

- Sub‑tasks not visible under stories
  - Parent values don't resolve to parent Stories. Ensure Parent field contains valid Story keys (e.g., RDF-3, RDF-4) and map Parent → Parent id.

- Users auto‑created or invalid email warnings
  - Unmap Reporter/Assignee or provide valid accounts. Jira Server may create placeholders if values don't match.

- Components not found
  - Ensure the "RapidReach" component exists in your project or allow creation during import.

- Epic Link not working
  - Ensure Epic Link field contains the actual Epic key (e.g., RDF-1) from the first import step.nd Sub‑tasks for the RapidReach device work. There are two audiences for this document:

- AI/automation: how to prepare and validate CSVs and which mappings to enforce
- Human users: the exact import steps in Jira with field mappings and known pitfalls

---

### Files in this folder

- `01-epic-import.csv`: Single Epic. Import first to create the Epic and get its key (e.g., `RDF-1`).
- `02-stories-import.csv`: Stories that link to the Epic via Epic Link field.
- `03-all-subtasks.csv`: All subtasks that link to their parent Stories via Parent field.
- `README.md`: This import guide with step-by-step instructions and troubleshooting.

---

### CSV schemas (what each file contains)

- Epic (01-epic-import.csv)
  - Fields: Summary, Issue Type, Description, Priority, Labels, Story Points, Reporter, Assignee, Component
  - Notes: Issue Type must be "Epic". Contains a single Epic record.

- Stories (02-stories-import.csv)
  - Fields: Summary, Issue Type, Description, Priority, Labels, Story Points, Reporter, Assignee, Epic Link, Component
  - Rules: Issue Type must be "Story"; Epic Link should reference the Epic key (e.g., RDF-1).

- Sub‑tasks (03-all-subtasks.csv)
  - Fields: Summary, Issue Type, Description, Priority, Labels, Story Points, Reporter, Assignee, Parent, Status, Component/s
  - Rules: Issue Type must be exactly "Sub-task"; Parent field contains the parent Story key (e.g., RDF-3, RDF-4, etc.).

All CSVs quote values that contain commas. Component/s is used for subtasks, Component for Epic and Stories. Status values assume the default workflow states (To Do, In Progress, Done).

---

### Human import steps (Jira Server/DC 9.8.1)

1) Import the Epic
   - Go to: Jira Administration → System → External System Import → CSV
   - Upload `01-epic-import.csv`
   - On the mapping screen:
     - Map: Summary, Issue Type, Description, Priority, Labels, Story Points
     - Map: Reporter, Assignee (to valid usernames/emails or unmap)
     - Map: Component → Component/s
   - Start import → After completion, copy the created Epic key (e.g., `RDF-1`).

2) Import Stories
   - Upload `02-stories-import.csv`
   - Mapping:
     - Map: Summary, Issue Type, Description, Priority, Labels, Story Points
     - Map: Epic Link → Epic Link (ensure this references the Epic key from step 1)
     - Map: Component → Component/s
     - Reporter/Assignee: map to valid users or unmap
   - Start import → Note the created Story keys (e.g., RDF-3, RDF-4, etc.)

3) Import Sub-tasks
   - Upload `03-all-subtasks.csv`
   - Mapping:
     - Map: Summary, Issue Type, Description, Priority, Labels, Story Points
     - Map: Parent → Parent id (system field). Do NOT map to "Parent Link"
     - Map: Status → Status, Component/s → Component/s
     - Reporter/Assignee: map to valid users or unmap
   - Start import

---

### Known pitfalls and how to avoid them

- Parent vs Parent Link
  - Use Parent (Parent id) for Sub‑tasks. Do not map to Parent Link (Advanced Roadmaps), which expects higher‑level hierarchy and causes errors like "Issue '1001' could not be found".

- Duplicate "External issue ID" custom fields
  - If Jira has multiple custom fields named "External issue ID", the importer may show a warning and duplicate records. Do not map CSV "Issue ID" to any custom field; map it to the built‑in "Issue ID" only.

- Reporter/Assignee resolution
  - If values don’t match valid users, Jira Server may create placeholder accounts or warn. Use valid usernames/emails or unmap these fields during import.

- Components and Versions
  - Component/s (and Fix Version/s) must exist or be creatable by the importing user. Otherwise, pre‑create them or allow creation during import.

- Project alignment
  - Import everything into the same project. Sub‑tasks must be created in the same project as their parents; cross‑project Sub‑tasks aren’t allowed.

---

### AI/automation checklist

Validation before import:
- Ensure all CSV headers match Jira Server conventions: Component/s (for subtasks), Component (for Epic/Stories), Parent
- Ensure Issue Types are exact: Epic, Story, Sub-task
- Confirm Sub‑tasks reference parent Stories by Issue key (e.g., RDF-3, RDF-4) in Parent column
- Ensure Epic Link on Stories uses the Epic key (RDF-1)
- Quote fields that contain commas
- Optional: normalize Reporter/Assignee to known usernames/emails or strip columns

Import orchestration:
- Step 1: Import `01-epic-import.csv`; capture the created Epic key
- Step 2: Verify Epic Link values in `02-stories-import.csv` match the captured Epic key
- Step 3: Import `02-stories-import.csv`; capture the created Story keys  
- Step 4: Verify Parent values in `03-all-subtasks.csv` match the captured Story keys
- Step 5: Import `03-all-subtasks.csv` with Parent mapped to Parent id, not Parent Link

Post‑checks:
- Verify hierarchy in the project: Epic → Stories → Sub‑tasks
- Spot check status and components mapping; adjust workflow/status mapping if needed

---

### Troubleshooting

- Error: "Cannot add value ... to CustomField Parent Link"
  - You mapped Parent to Parent Link. Re‑import and map Parent → Parent id.

- Sub‑tasks not visible under stories
  - Parent values don’t resolve to parents created in the same import or project. Use Issue ID linkages in the same CSV and map Parent → Parent id.

- Users auto‑created or invalid email warnings
  - Unmap Reporter/Assignee or provide valid accounts. Jira Server may create placeholders if values don’t match.

- Import duplicates
  - Ensure "Issue ID" is mapped to the built‑in field only, and leave any "External issue ID" custom fields unmapped.


