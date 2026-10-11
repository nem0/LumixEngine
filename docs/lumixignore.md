# Ignoring folders with `.lumixignore`

A folder containing a file named `.lumixignore` is excluded from the asset pipeline. The engine does not scan, compile, load or export anything in it.

## What it does

A folder with `.lumixignore` is treated as if it does not contain assets:

- **Not compiled:** the asset compiler skips it, so no compiled files are created for its resources.
- **Not loaded:** resources in it cannot be loaded. If one is requested, the asset compiler logs an error saying it is in an ignored folder.
- **Not exported:** the [Export game](packaged_game.md) step does not include it.

The marker applies to the whole subtree. Subfolders of an ignored folder are ignored too, even if they have no marker of their own.

## Creating it

The easiest way is from the asset browser:

1. Right-click the folder.
2. Choose **Ignore directory**. The menu item is checked while the folder is ignored.

This creates an empty `.lumixignore` file in the folder. Choosing the item again deletes the file and un-ignores the folder.

You can also create or delete the file by hand in your file manager. The editor watches the project folder, so the change is picked up while the editor is running.

## What happens when it changes

- **Marker added:** every resource in the folder is forgotten, and its compiled file in `.lumix/resources/` is deleted. Nothing in the folder can load stale data afterwards.
- **Marker removed:** the folder is scanned again as if it had just been created, and its assets are compiled.

## Limitations

- The marker must be in a folder, not in the project root. A `.lumixignore` in the root is not supported, because it would ignore the whole project.
- The file itself is empty. Its content is not read.

## Example

```text
project/
├── .lumixignore          ← not supported here, the project root is never ignored
├── models/
│   ├── hero.fbx          ← compiled, loaded, exported
│   └── wip/
│       ├── .lumixignore  ← whole wip/ folder is ignored
│       └── test_rig.fbx  ← not compiled, not loaded, not exported
```

Use this for work-in-progress assets, scratch files, or large source material that should not go into the game build.
