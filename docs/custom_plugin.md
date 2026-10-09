# Custom plugin

1. Run `scripts/plugins.bat`.
2. Choose `Empty plugin template`. This will download the plugin's template.
3. The template is in `plugins\myplugin`. Rename it to whatever you want. As our example, we rename it to `remote`.
    The plugin's name is its directory name. There is no project name to set in `premake5.lua`.
4. Update `LUMIX_PLUGIN_ENTRY` in `plugins\remote\src\myplugin.cpp` to match the plugin's name:
    ```diff
        - LUMIX_PLUGIN_ENTRY(myplugin)
        + LUMIX_PLUGIN_ENTRY(remote)
    ```
    You can rename `myplugin.cpp`, but it does not need to match the plugin's name.
5. Update `LUMIX_STUDIO_ENTRY` in `plugins\remote\src\editor\plugins.cpp` to match the plugin's name:
    ```diff
        - LUMIX_STUDIO_ENTRY(myplugin)
        + LUMIX_STUDIO_ENTRY(remote)
    ```
    You can rename `myplugin.cpp`, but it does not need to match the plugin's name.
6. Recreate the solution, for example run `scripts\premake5.exe vs2022` from the repository root.
7. You can now build the solution with your custom plugin.
8. (Optional, recommended) The template's source files contain other references to `myplugin`; update them as needed.
