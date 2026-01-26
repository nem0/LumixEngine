# Editor action

In this context, an action refers to any command in the editor that can be triggered by a shortcut. In the code, it is represented by `struct Action` in [src/editor/action.h](../src/editor/action.h). Every instance of `Action` is automatically saved in the settings and is available in the shortcut editor. Some `Action`s are available in the **Tools** or **View** menu. To check if the user actually pressed the assigned shortcut or triggered the action from the menu, use `StudioApp::checkShortcut`.

## Shortcuts

Every action can be assigned a shortcut. A shortcut consist of a set of modifiers (Alt, Ctrl, Shift) and a key, so for example `Ctrl A`, `F11` or `Alt` (modifier only shortcuts are valid). It's valid for different actions to have the same shortcut assigned as long as they are in different Windows/parts of UI, since focus can be taken into account. 

Shorcuts can be changed in editor in **Settings** window under **Shortcuts** tab. Shortcuts are saved **studio.ini** in user settings. You can see the location of **studio.ini** in editor in **Settings** under **General** tab. On Windows it should be in `%APPDATA%/lumixengine/studio/studio.ini`

## `Action` Usage

### Window action

A window action is an action that's supposed to open or close a window. It's automatically visible in **Main Menu** -> **View** -> `Action::label_short`.

```cpp
struct Plugin ... {
    ...
    Action m_toggle_ui{"My plugin", "Toggle UI", "Toggle UI", "myplugin_toggle_ui", "", Action::WINDOW}; // Action::WINDOW -> the action is visible in Main Menu -> View -> Action::label_short
    bool m_is_window_open = false;
    StudioApp& m_app;
    ...

    // this is called even when window is not open
    void gui() {
        // m_app.checkShortcut returns true if user pressed 
        // the shortcut assigned to the Action
        // or triggered the Action from menu
        if (m_app.checkShortcut(m_toggle_ui, true/*global action*/)) {
            // global action means the action can be triggered
            // even if the window is not focused
            m_is_window_open = !m_is_window_open;
        }

        if (!m_is_window_open) return;
        if (ImGui::Begin("My plugin window", &m_is_window_open)) {
            ...
        }
    }
}
```

### Tool action

A tool action is an action that's visible in **Main Menu** -> **Tools**

```cpp
struct Plugin ... {
    ...
    Action m_wireframe{"View", "Wireframe", "Toggle wireframe", "Toggle wireframe", "view_toggle_wireframe", "", Action::TOOL}; // Action::TOOL -> the action is visible in Main Menu -> Tools -> Action::label_short
    StudioApp& m_app;
    ...

    void update(float time_delta) {
        if (m_app.checkShortcut(m_wireframe, true/*global action*/)) {
            toggleWireframe();
        }
    }
}
```

### Normal action

An action, which is not a tool or window action, is just a normal action. It means it's not automatically visible in any menu.

```cpp
struct Plugin ... {
    ...
    Action m_create_action{"My plugin", "Create object", "Create object with some stuff", "myplugin_create_object", ""}; // Action::NORMAL (default) -> the action is not automatically visible in any menu
    StudioApp& m_app;
    ...

    void gui() {
        ...
        if (ImGui::Begin("My plugin window")) {
            ...
            if (m_app.checkShortcut(m_create_action, false/*local action*/)) {
                // Local action means focus is taken into account.
                // In this case, since we are inside ImGui::Begin/ImGui::End call,
                // `My plugin window` must be focused for action to be triggered.
                createObject();
            }
            ...
        }
        ImGui::End();
    }
}
```

### `Action::isActive`

`checkShortcut` returns true only once when the shortcut is pressed. User must release the keys and press them again to trigger the action again. On the other hand, `Action::isActive` returns true for the whole duration a shortcut is pressed. 

* 1st frame: user pressed a shortcut, `checkShortcut` returns **true**, `Action::isActive` returns **true**
* 2nd frame: shortcut is still pressed, `checkShortcut` return **false**, but `Action::isActive` still returns **true**
* ...
* N-th frame: user released the keys, `checkShortcut` returns **false**, `Action::isActive` returns **false**

This is useful for stuff like camera movement:

```cpp
struct CameraController ... {
    ...
    Action m_forward_action{"Forward", "Forward", "Camera - move forward", "camera_move_forward", ""}; 
    ...

    void update(float time_delta) {
        ...
        if (m_forward_action.isActive()) moveCameraForward();
        ...
    }
};
```