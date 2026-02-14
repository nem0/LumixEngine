# How to download plugins
1. Execute [scripts/plugins.bat](../scripts/plugins.bat).
2. Select the desired plugin.
3. Regenerate the solution (e.g., run [scripts/create_vs22_sln.bat](../scripts/create_vs22_sln.bat)).
4. Build and run the solution.

Plugins downloaded this way are placed in the `plugins` directory. *GENie* automatically detects all plugins in this directory and includes them in the solution.

# Up-to-date plugins

Note: Plugins in this section are compilable and runnable; some are still in the prototype stage.

* [Real-world terrains, roads and buildings](https://github.com/nem0/LumixEngine_maps)
* [Rml UI](https://github.com/nem0/lumixengine_rml)
* [JavaScript](https://github.com/nem0/LumixEngine_js)
* [Procedural geometry](https://github.com/nem0/lumixengine_procedural_geom)
* [Network](https://github.com/nem0/lumixengine_net)
* [Marketplace](https://github.com/nem0/lumixengine_market)
* [C#](https://github.com/nem0/lumixengine_csharp)
* [LiveCode](https://github.com/nem0/lumixengine_livecode)
* [GLTF importer](https://github.com/nem0/lumixengine_gltf)

# Deprecated

Note: These plugins are not supported and most likely won't build with the latest engine, but can be resurrected.

* [Node-based shader editor](https://github.com/nem0/lumixengine_shader_editor)
* [Visual scripting](https://github.com/nem0/lumixengine_visualscript)
* [DX11 & DX12](https://github.com/nem0/lumixengine_dx11) - DX12 is now integrated into the engine; older APIs (DX11) are not supported.
* [FBX importer using Autodesk's SDK](https://github.com/nem0/LumixEngine_fbx)
* [HTML/CSS rendering](https://github.com/nem0/lumixengine_html)
* [Turbo Badger](https://github.com/nem0/lumixengine_turbobadger)

