# ImGui in packaged game

By default, ImGui is not enabled in packaged game. To enable ImGui integration, you have to do following:

1. Uncomment `#define LUMIX_APP_IMGUI_INTEGRATION` in [imgui_integration.h](../src/app/imgui_integration.h).
2. Build the solution including `app` project.
3. In your scene, add following script to an entity:

```lua
local show_imgui = false
function onInputEvent(event : InputEvent)
	if event.type == "button" then
		if event.device.type == "keyboard" then
			if event.key_id == LumixAPI.INPUT_KEYCODE_F11 and event.down then
				show_imgui = not show_imgui
			end
		end
	end
end

function update(time_delta)
	this.world:getModule("gui"):getSystem():enableCursor(show_imgui)
	if show_imgui then
		ImGui.Begin("foo")
		ImGui.Text("Hello World!")
		ImGui.End()
	end
end
```

4. Package your game.
5. Run the packaged game.
6. Press F11 to show ImGui in your packaged game.
