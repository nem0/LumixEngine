# UI Example

Real-world example: a compact game main menu with a settings overlay. This file
contains a `style` block and the markup for a playable menu UI demonstrating
layout, sizing, classes, and commonly used attributes.

```c++
style {
	panel.menu {
		width: 420;
		padding: 16;
		bg-color: #1e1f25;
	}

	text.title {
		font-size: 22;
		color: #ffffff;
		padding: 8;
		margin: 12;
	}

	button.primary {
		bg-color: #2ea44f;
		color: #ffffff;
		padding: 10;
		width: 12em;
		height: 2.6em;
		margin: 6;
	}

	button.secondary {
		bg-color: #3a3f4a;
		color: #ffffff;
		padding: 8;
		width: 10em;
		height: 2.6em;
		margin: 4;
	}

	panel.settings_row {
		direction: row;
		justify-content: space-between;
		padding: 6;
	}
}

panel id="ui_root" width=100% height=100% {
	/* Centered overlay panel */
	panel id="overlay" {
		panel id="menu" class=menu {
			text class=title value="My Game"

			/* Main actions */
			panel {
				button id="start" class="primary" { "Start Game" }
				button id="continue" class="primary" { "Continue" }
				button id="options" class="secondary" { "Options" }
				button id="quit" class="secondary" { "Quit" }
			}
		}

		/* Settings overlay (hidden by default, shown when Options pressed) */
		panel id="settings_panel" width=360 class=menu visible=false {
			text class=title value="Settings"

			/* Volume row: label + numeric input */
			panel class=settings_row {
				text value="Volume" align=left
				input id="volume" value="75" width=6em
			}

			/* Resolution row: simple selectable options */
			panel class=settings_row {
				text value="Resolution" align=left
				panel {
					button id="res_1920" class="secondary" width=10em { "1920x1080" }
					button id="res_1280" class="secondary" width=10em { "1280x720" }
				}
			}

			/* Apply / Back */
			panel {
				button id="apply" class="primary" width=10em { "Apply" }
				button id="back" class="secondary" width=10em { "Back" }
			}
		}
	}
}
```
