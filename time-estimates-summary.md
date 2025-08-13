# Time Estimates for RapidReach Jira Tasks

## Summary

We attempted to add time estimates to the Jira tasks but encountered a configuration issue:
- **Error**: "Field 'timeoriginalestimate' cannot be set. It is not on the appropriate screen"

## Time Distribution Plan

### Implementation Tasks (54 To Do sub-tasks) - Total: 120 hours

Based on complexity analysis:

| Complexity | Count | Hours Each | Total Hours | Examples |
|------------|-------|------------|-------------|----------|
| Simple | 2 | 1.1h | 2.2h | Status Data Collection, Status JSON Builder |
| Medium | 29 | 1.8h | 52.2h | HTTP requests, storage, basic handlers |
| Complex | 8 | 2.5h | 20.0h | Parsers, system integration, handlers |
| Very Complex | 15 | 3.2h | 48.0h | Registration, TLS/SSL, Audio streaming |
| **Total** | **54** | - | **122.4h** | |

### Acceptance Test Tasks (54 sub-tasks) - Total: 20 hours
- Each test task: 0.4 hours (approximately 25 minutes)

## Required Jira Configuration

To enable time tracking, a Jira administrator needs to:

1. Go to **Project Settings** → **Screens**
2. Find the screens used for Sub-tasks in the RDP project
3. Add ONE of the following fields to the screens:
   - **Story Points** (customfield_10002) - Currently shown as "Estimate" field
   - **Time Tracking** (includes Original Estimate, Remaining Estimate, Time Spent)
   - **Original Estimate** field (timeoriginalestimate)

**Current Status**: None of these fields are editable on sub-tasks. The available editable fields are:
- Assignee, Attachment, Comment, Component/s, Sprint, Epic Link, Description, Fix Version/s, Linked Issues, Issue Type, Labels, Priority, Reporter, Summary

## Manual Entry Instructions

Until the configuration is fixed, you can manually add estimates by:

1. Opening each issue
2. Click **More** → **Log Work** or **Time Tracking**
3. Enter the Original Estimate based on the complexity:
   - Simple tasks: 1.1h
   - Medium tasks: 1.8h
   - Complex tasks: 2.5h
   - Very complex tasks: 3.2h
   - Test tasks: 0.4h

## Task Breakdown by Category

### Very Complex Tasks (3.2h each):
- RDP-110: Registration HTTP Client
- RDP-111: Build Registration JSON Payload
- RDP-113: Parse Registration Response
- RDP-122: Audio Streaming Protocol
- RDP-123: Opus Decoder Integration
- RDP-124: Audio Playback Handler
- RDP-125: Audio File Management
- RDP-143: Implement TLS/SSL Security for MQTT
- RDP-145: Emergency Alert Subscription System
- RDP-146: Device Registration Protocol
- RDP-148: Production Security Hardening
- RDP-149: Performance Optimization
- RDP-154: WiFi SSID/Password Command Handler
- RDP-162: Microphone Feedback Detection
- RDP-163: Self-Test Audio Validation

### Complex Tasks (2.5h each):
- RDP-119: MQTT Message Parser
- RDP-132: Status Request Handler
- RDP-150: Restart Command Handler
- RDP-152: Connectivity Priority System
- RDP-156: Log Retrieval System
- RDP-157: Statistics System Core
- RDP-158: Message Count Tracking
- RDP-159: Power-On Statistics

### Medium Tasks (1.8h each):
Most standard implementation tasks including:
- HTTP requests
- Storage management
- Basic command handlers
- Configuration systems
- Data formatting

### Simple Tasks (1.1h each):
- RDP-133: Status Data Collection
- RDP-134: Status JSON Builder

## Next Steps

1. Contact Jira administrator to add time tracking fields to Sub-task screens
2. Once configured, run the `update-time-estimates.py` script again
3. Or manually enter the estimates using the hours listed above
