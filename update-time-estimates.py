#!/usr/bin/env python3
import requests
from requests.auth import HTTPBasicAuth
import json
import os

# Load environment variables
JIRA_URL = os.environ['JIRA_URL']
JIRA_USER = os.environ['JIRA_USER']
JIRA_TOKEN = os.environ['JIRA_TOKEN']
JIRA_PROJECT = os.environ['JIRA_PROJECT']

auth = HTTPBasicAuth(JIRA_USER, JIRA_TOKEN)

# Get all To Do implementation tasks
print("Fetching To Do implementation tasks...")
jql_todo = 'project=RDP AND status="To Do" AND type=Sub-task ORDER BY key'
response = requests.get(
    f'{JIRA_URL}/rest/api/2/search',
    auth=auth,
    params={
        'jql': jql_todo,
        'fields': 'key,summary,parent',
        'maxResults': 100
    }
)

if response.status_code != 200:
    print(f"Error fetching tasks: {response.status_code}")
    exit(1)

todo_tasks = response.json()['issues']
print(f"\nFound {len(todo_tasks)} To Do implementation sub-tasks")

# Categorize tasks by complexity based on their summary
simple_tasks = []      # 1-2 hours: Simple configuration, basic implementation
medium_tasks = []      # 2-3 hours: Standard features, moderate complexity
complex_tasks = []     # 3-4 hours: Complex logic, integration, protocols
very_complex_tasks = [] # 4-5 hours: Full subsystems, security, major features

# Keywords for categorization
simple_keywords = ['configuration', 'basic', 'simple', 'flag', 'status', 'getter', 'setter', 'validation']
complex_keywords = ['protocol', 'security', 'encryption', 'parser', 'handler', 'manager', 'system', 'integration']
very_complex_keywords = ['tls', 'ssl', 'authentication', 'registration', 'audio', 'codec', 'streaming']

for task in todo_tasks:
    summary = task['fields']['summary'].lower()
    key = task['key']
    
    # Check for complexity indicators
    if any(keyword in summary for keyword in very_complex_keywords):
        very_complex_tasks.append(task)
    elif any(keyword in summary for keyword in complex_keywords):
        complex_tasks.append(task)
    elif any(keyword in summary for keyword in simple_keywords):
        simple_tasks.append(task)
    else:
        # Default to medium complexity
        medium_tasks.append(task)

print(f"\nTask complexity breakdown:")
print(f"Simple tasks (1-2h): {len(simple_tasks)}")
print(f"Medium tasks (2-3h): {len(medium_tasks)}")
print(f"Complex tasks (3-4h): {len(complex_tasks)}")
print(f"Very complex tasks (4-5h): {len(very_complex_tasks)}")

# Calculate total hours needed with the current distribution
estimated_total = (len(simple_tasks) * 1.5 + 
                  len(medium_tasks) * 2.5 + 
                  len(complex_tasks) * 3.5 + 
                  len(very_complex_tasks) * 4.5)

print(f"\nEstimated total with initial distribution: {estimated_total}h")

# Adjust to fit within 120 hours
scaling_factor = 120 / estimated_total if estimated_total > 0 else 1
print(f"Scaling factor to fit 120h: {scaling_factor:.2f}")

# Final hour allocations
simple_hours = round(1.5 * scaling_factor, 1)
medium_hours = round(2.5 * scaling_factor, 1)
complex_hours = round(3.5 * scaling_factor, 1)
very_complex_hours = round(4.5 * scaling_factor, 1)

print(f"\nAdjusted hour allocations:")
print(f"Simple tasks: {simple_hours}h each")
print(f"Medium tasks: {medium_hours}h each")
print(f"Complex tasks: {complex_hours}h each")
print(f"Very complex tasks: {very_complex_hours}h each")

# Show examples from each category
print("\n=== Task Examples by Category ===")
print("\nSimple tasks:")
for task in simple_tasks[:3]:
    print(f"  {task['key']}: {task['fields']['summary']}")

print("\nMedium tasks:")
for task in medium_tasks[:3]:
    print(f"  {task['key']}: {task['fields']['summary']}")

print("\nComplex tasks:")
for task in complex_tasks[:3]:
    print(f"  {task['key']}: {task['fields']['summary']}")

print("\nVery complex tasks:")
for task in very_complex_tasks[:3]:
    print(f"  {task['key']}: {task['fields']['summary']}")

# Ask for confirmation before updating
print(f"\n{'='*60}")
print(f"Ready to update {len(todo_tasks)} implementation tasks with time estimates")
total_hours = (len(simple_tasks) * simple_hours + 
               len(medium_tasks) * medium_hours +
               len(complex_tasks) * complex_hours +
               len(very_complex_tasks) * very_complex_hours)
print(f"Total hours: {total_hours:.1f}h")
print("\nProceeding with updates...")

# Update implementation tasks
print("\nUpdating implementation tasks...")
success_count = 0
for task in todo_tasks:
    key = task['key']
    summary = task['fields']['summary'].lower()
    
    # Determine hours based on complexity
    if any(keyword in summary for keyword in very_complex_keywords):
        hours = very_complex_hours
    elif any(keyword in summary for keyword in complex_keywords):
        hours = complex_hours
    elif any(keyword in summary for keyword in simple_keywords):
        hours = simple_hours
    else:
        hours = medium_hours
    
    # Update the task with time estimate
    update_data = {
        'fields': {
            'timetracking': {
                'originalEstimate': f'{hours}h'
            }
        }
    }
    
    update_response = requests.put(
        f'{JIRA_URL}/rest/api/2/issue/{key}',
        auth=auth,
        json=update_data
    )
    
    if update_response.status_code == 204:
        success_count += 1
        print(f"✓ {key}: {hours}h")
    else:
        print(f"✗ {key}: Failed - {update_response.status_code}")

print(f"\nUpdated {success_count}/{len(todo_tasks)} implementation tasks")

# Now handle acceptance test tasks
print("\n" + "="*60)
print("Fetching Acceptance Test tasks...")
jql_test = 'project=RDP AND status="Acceptance Test" AND type=Sub-task ORDER BY key'
response = requests.get(
    f'{JIRA_URL}/rest/api/2/search',
    auth=auth,
    params={
        'jql': jql_test,
        'fields': 'key,summary',
        'maxResults': 100
    }
)

if response.status_code == 200:
    test_tasks = response.json()['issues']
    print(f"Found {len(test_tasks)} Acceptance Test sub-tasks")
    
    # Distribute 20 hours across test tasks
    test_hours_per_task = round(20 / len(test_tasks), 1) if len(test_tasks) > 0 else 0
    print(f"Each test task will get: {test_hours_per_task}h")
    print("\nUpdating test tasks...")
    
    test_success_count = 0
    for task in test_tasks:
        key = task['key']
        update_data = {
            'fields': {
                'timetracking': {
                    'originalEstimate': f'{test_hours_per_task}h'
                }
            }
        }
        
        update_response = requests.put(
            f'{JIRA_URL}/rest/api/2/issue/{key}',
            auth=auth,
            json=update_data
        )
        
        if update_response.status_code == 204:
            test_success_count += 1
            print(f"✓ {key}: {test_hours_per_task}h")
        else:
            print(f"✗ {key}: Failed - {update_response.status_code}")
    
    print(f"\nUpdated {test_success_count}/{len(test_tasks)} test tasks")

print("\n✅ Time estimation update complete!")
