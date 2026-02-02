# Lua Plugins for Editor

Lua plugins allow extending the Lumix Editor with custom functionality written in Lua. They are automatically loaded from `data/editor/scripts/plugins/` directory and provide a way to add new windows, menus, and integrate with the editor's UI.

## Plugin Types

There are two types of Lua plugins:

### Simple Scripts

Simple Lua scripts that execute code when loaded. These are useful for adding actions or performing initialization.

```lua
-- Example: Add an action to open the selected entity's Lua script
Editor.addAction {
    name = "open_lua_script", 
    label = "Open Lua Script",
    run = function()
        if Editor.getSelectedEntitiesCount() ~= 1 then return end
        local e = Editor.getSelectedEntity(0)
        if e.lua_script == nil then return end
        if e.lua_script.scripts[1] == nil then return end
        local script_path = e.lua_script.scripts[1].path
        Editor.asset_browser:openEditor(script_path)
    end
}
```

### Plugin Tables

More complex plugins that return a Lua table with hooks for UI integration and settings persistence.

## Plugin Table Structure

A plugin table has the following optional fields:

```lua
local plugin = {
    name = "My Plugin",
    settings = {
        -- settings are automatically saved/loaded
        some_setting = true
    }
}
return plugin -- we must return the plugin table
```

## Available Hooks

### onGUI()

Called every frame to render the plugin's UI. This is where you implement the main functionality.

```lua
function plugin.onGUI()
    if ImGui.Begin("My Window") then
        ImGui.Text("Hello from Lua plugin!")
        if ImGui.Button("Click me") then
            LumixAPI.logInfo("Button clicked!")
        end
    end
    ImGui.End()
end
```

### windowMenuAction()

If this function exists, a menu item is automatically added to the "Window" menu. The function is called when the menu item is activated.

```lua
plugin.windowMenuAction = function()
    plugin.settings.window_open = not plugin.settings.window_open
end
```

### onSettingsLoaded()

Called when the plugin's settings are loaded from disk.

### onBeforeSettingsSaved()

Called before the plugin's settings are saved to disk.

## Accessing Editor APIs

Plugins have access to various editor APIs through global objects:

- `Editor` - Main editor interface
- `LumixAPI` - Engine API
- `ImGui` - Immediate mode GUI

## Settings

Plugin settings are automatically persisted. Define them in the `settings` table:

```lua
local plugin = {
    name = "Example Plugin",
    settings = {
        window_open = true,
        some_value = 42
    }
}
```

## Complete Example

```lua
local plugin = {
    name = "Example Plugin",
    settings = {
        window_open = true,
        counter = 0
    }
}

plugin.windowMenuAction = function()
    plugin.settings.window_open = not plugin.settings.window_open
end

function plugin.onGUI()
    if plugin.settings.window_open then
        if ImGui.Begin("Example Plugin") then
            ImGui.Text("Counter: " .. plugin.settings.counter)
            if ImGui.Button("Increment") then
                plugin.settings.counter = plugin.settings.counter + 1
            end
        end
        ImGui.End()
    end
end

return plugin
```

## Built-in Plugins

The editor comes with some built-in Lua plugins:

- **Lua Console** (`lua_console.lua`) - Interactive Lua console for executing code (plugin table)
- **Open Script Action** (`open_script_action.lua`) - Quick access to open Lua scripts (simple script)

## Notes

- Plugins are loaded at editor startup
- **Plugins are not hot-reloaded** - changes to plugin scripts require restarting the editor
- Simple scripts execute their code once when loaded and don't need to return anything
- Plugin tables must be returned to be registered with the editor
- Errors in plugin scripts are logged but don't prevent other plugins from loading
