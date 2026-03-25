---
name: vscode-task-button
description: Create compact VS Code task buttons backed by scripts in tools/tasks
---

# VS Code Task Button Skill

## When to use this skill

Use this every time the user asks for a new button to execute something in VS Code.

## What this skill does

1. Create a Bash script in `tools/tasks/`.
2. Register the task in `.vscode/tasks.json`.
3. Add a compact button entry (emoji + short name) in `.vscode/settings.json` under `actionButtons.commands`.
4. Add a tooltip-style description using the task `detail` field in `.vscode/tasks.json`.

## Naming rules

- Script file: lowercase snake case and `.sh` suffix, for example `lint_docs.sh`.
- Task label: match the script filename exactly, for example `lint_docs.sh`.
- Button name: compact label with emoji + short text, for example `🧹 Clean` or `🧪 Test`.

## Required task shape (`.vscode/tasks.json`)

```json
{
  "label": "<script_name>.sh",
  "type": "shell",
  "command": "<script_name>.sh",
  "detail": "<short tooltip description>",
  "options": {
    "cwd": "${workspaceFolder}"
  },
  "presentation": {
    "reveal": "always",
    "panel": "shared",
    "clear": true
  },
  "problemMatcher": []
}
```

## Required button shape (`.vscode/settings.json`)

```json
{
  "name": "<emoji> <short-name>",
  "singleInstance": true,
  "useVsCodeApi": true,
  "command": "workbench.action.tasks.runTask",
  "args": [
    "<script_name>.sh"
  ]
}
```

## Script baseline (`tools/tasks/<script_name>.sh`)

```bash
#!/usr/bin/env bash
set -euo pipefail

# task logic here
```

Make scripts executable after creation.
