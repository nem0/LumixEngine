# Custom plugin

1. Run `scripts/plugins.bat`.
2. Choose `Empty plugin template`. This will download the plugin's template.
3. The template is in `plugins\myplugin`. Rename it to whatever you want. As our example, we rename it to `remote_control`
4. Rename the plugin's project name in `plugins\remote_control\genie.lua`. The name must match the directory name from step 3:

    ```diff
        - if plugin "myplugin" then
        + if plugin "remote_control" then
    ```
4. Update `BLACK_PLUGIN_ENTRY` in `plugins\remote_control\src\myplugin.cpp` to match the plugin's name:
    ```diff
        - BLACK_PLUGIN_ENTRY(myplugin)
        + BLACK_PLUGIN_ENTRY(remote_control)
    ```
    You can rename `myplugin.cpp`, but it does not need to match the plugin's name.

5. Update `BLACK_STUDIO_ENTRY` in `plugins\remote_control\src\editor\plugins.cpp` to match the plugin's name:
    ```diff
        - BLACK_STUDIO_ENTRY(myplugin)
        + BLACK_STUDIO_ENTRY(remote_control)
    ```
    You can rename `myplugin.cpp`, but it does not need to match the plugin's name.
6. Recreate the solution, for example run `scripts\genie.exe vs2022` from the repository root.
7. You can now build the solution with your custom plugin.
8. (Optional, recommended) The template's source files contain other references to `myplugin`; update them as needed.

