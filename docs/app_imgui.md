# ImGui in packaged game

By default, ImGui is not enabled in packaged game. To enable ImGui integration, you have to do following:

1. Uncomment `#define LUMIX_APP_IMGUI_INTEGRATION` in [imgui_integration.h](../src/app/imgui_integration.h).
2. Build the solution including `app` project.
3. Set `main.evox` in the project root to the following:

	```evox
	import "core:imgui" as ImGui
	import "core:input"
	import "core:keycode"
	import "core:ui"
	import "core:world"

	fn main(input : InputSystem, world : World) : void {
		var show_imgui : bool = false;
		while true {
			const time_delta : f32 = yield;
			for event in input.getEvents() {
				match event {
					case ButtonEvent:
						if event.device_type == .KEYBOARD and event.key_id as Keycode == .F11 and event.down and not event.is_repeat {
							show_imgui = not show_imgui;
						}
					case:
				}
			}

			if const ui = world.ui() {
				ui.getSystem().enableCursor(show_imgui);
				if show_imgui {
					ImGui.begin("foo");
					ImGui.textUnformatted("Hello World!");
					ImGui.end();
				}
			}
		}
	}
	```

4. Package your game.
5. Run the packaged game.
6. Press F11 to show ImGui in your packaged game.
