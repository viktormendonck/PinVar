# PinVar – Blueprint Variable Pinning for Unreal Engine

A UE5.6 editor plugin that lets you quickly view and edit **Blueprint Class Default Variables** in one place, grouped by a custom `PinnedGroup` metadata tag.

## Features
- **Custom grouping** of variables using `PinnedGroup` metadata.
- Expandable/collapsible groups.
- Multiple groups per variable, separate with commas `,`.


## Installation
1. Download or clone this repository.
2. Place the `PinVar` folder into your Unreal project's: <YourProject>/Plugins/
3. Launch Unreal Engine.
4. In the **Plugins** window, search for “PinVar” and enable it.
5. Restart the editor.

## Usage

### 1. Add PinnedGroup Metadata to a Variable

Example (C++):
When working in C++, PinVar detects variables by reading their `UPROPERTY` metadata.  
If a variable is **not** marked with `UPROPERTY`, it will not be scanned and therefore won’t appear in the PinVar panel.

To make a C++ variable appear in PinVar:

1. Declare it as a `UPROPERTY` in your header file.
2. Add the `PinnedGroup` metadata with the group name you want.
3. Make sure the property is visible to the editor (e.g., `EditDefaultsOnly`, `BlueprintReadOnly`, etc.).
```cpp
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (PinnedGroup = "Movement,Combat"))
float SprintSpeed;
```
Notes:
- Use commas , to assign the variable to multiple groups.
- The variable must be visible in Class Defaults for editing.



Example (Blueprint):
**local variables** in blueprints are **not exposed as `UPROPERTY`**.  
Because PinVar scans for variables using `UPROPERTY` metadata, these won’t appear automatically, even if you want them in a pinned group.

To work around this, you can manually add them from the PinVar panel:

1. Open the main PinVar window:  
   **Window → Pinned Variables Tool**.
2. Click **Add BP Variable**.
3. In the dialog:
   - **Select the Blueprint** that contains the variable.
   - **Choose the variable** from the list.
   - **Enter the group name** you want it to appear under (comma-separated if adding to multiple groups).
4. Click **Add** — the variable will now appear in your pinned list and can be edited like any other.

### 2. Editing the variables
In the PinVar Window (**Window → Pinned Variables Tool**) you can now open the different dropdown menus to edit the variables inside them

## Background
I originally built this tool while working on a game with a tightly connected combat system.  
Balancing values was tedious — I had to jump from Blueprint to Blueprint, editing one variable at a time.  

With PinVar, I can simply create a group called **"Combat"** and adjust all related values in one place, without constant Blueprint switching.


