# PinVar – Pinned Variables Tool for Unreal Engine

## Overview

**PinVar** is an Unreal Engine 5.6 editor plugin that provides a dedicated editor tab for managing **Blueprint** and **C++** class default variables in one place.
It allows you to group and edit variables from multiple sources — including component defaults — without manually opening multiple Blueprints.


## Features

* **Custom “Pinned Variables Tool” Tab** – Access from *Window → Custom Tools → Pinned Variables Tool*.
* **Group-Based Organization** – Create and manage variable groups for easy editing.
* **Subcategories** - use "*A|B*" to add a subcategory for organization of variables.
* **Multiple Source Types**:
  * Blueprint-local variables
  * Parent C++ class properties
  * Component template variables
* **Searchable & Filterable Lists** – Quickly find variables and groups.
* **Persistent Storage** – Groups and variable selections are saved in JSON inside the plugin folder.

## Installation

1. Copy the `PinVar` plugin folder into your project’s `Plugins/` directory.
2. Rebuild your project to compile the plugin.
3. Enable **PinVar** in *Edit → Plugins*.(might be automatically activated)

## Usage

### Opening the Tool

* Navigate to **Windows → Pinned Variables Tool**.
* A new tab will appear with toolbar actions and the variable list.
  
<img width="794" height="672" alt="Screenshot 2025-08-11 233443" src="https://github.com/user-attachments/assets/8aec0857-3fee-43ad-89e6-3d0f8e5490cc" />

### Adding Variables

Click **Add Variable** to open the selection dialog.
Here you can select the blueprint from which you will take your variables.

<img width="609" height="443" alt="image" src="https://github.com/user-attachments/assets/da086ab0-13b2-4459-aff2-dd1a7819d421" />

You can add variables from three different sources:

<img width="531" height="425" alt="Screenshot 2025-08-11 234047" src="https://github.com/user-attachments/assets/c299a157-bcc8-4865-b420-0e71fc0e81fc" />

#### 1. **Blueprint Local Variable**

* Select a Blueprint from your project.
* Choose a variable defined directly in that Blueprint.

#### 2. **Parent C++ Property**

* Select a Blueprint that inherits from a native C++ class.
* Choose a C++ property marked with an editable UPROPERTY.

#### 3. **Component Variable**

* Select a Blueprint.
* Choose a component from the list (template or SCS-added).
* Pick a variable from that component’s defaults.


### Group Management

* **New Group:** Enter a name in the **Group Name** field.
  Multiple groups can be entered as comma-separated names (`Combat,AI`).
* **Existing Group:** Select from the dropdown and click **Add to existing group**.
* You can also add to an existing group by putting its name in the new group field

Groups are **per variable set**, not hard-linked to Blueprints, meaning you can gather related settings across multiple assets into one group.

### Editing Variables

* Groups show **Blueprint variables first**, then **C++ variables**, then **component variables**.
* Components are shown under a `Component: <Name>` heading.

### Removing Variables

* Click the **X** button next to a variable to remove it from the group.
* Removal is immediate and saved to disk.

## Data Storage

Pinned variables are saved as JSON in:

```
<PluginDir>/PinVar/Pinned.json
```

This file contains an array of pinned variable definitions including:
* Class name
* Variable name
* Group name(s)
* Component template name (if applicable)
* Pretty component variable name (if applicable)

## Example Workflow

1. **Goal:** Balance combat stats across multiple Blueprints and components.
2. Open **Pinned Variables Tool**.
3. Create a group called `Combat`.
4. Add relevant health, damage, cooldown variables from:

   * Player Blueprint (BP local)
   * Parent C++ `ACharacter` properties
   * Weapon component variables
5. Edit all in one place without jumping between assets.

## Why This Exists

While balancing gameplay systems, you often need to tweak the same category of variables scattered across many assets.
**PinVar** centralizes these edits, reducing context switching and making iteration faster.
