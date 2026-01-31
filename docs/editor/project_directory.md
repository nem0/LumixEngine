Different directories can be mounted in different virtual paths. There are two mounts by default: the engine data directory and the project directory.

# Engine data directory

We can access any files in the `data/*` directory using the `engine/*` mount point.

For example, let's assume we downloaded the engine repository in `c:/projects/black Engine`. We can access `c:/projects/black Engine/data/models/cube.fbx` using `engine/models/cube.fbx` in the engine.
This directory is used for files that are not project specific (e.g., shaders, editor fonts, some basic models like cube and sphere, etc.). These files can be "shared" across multiple projects.

# Project directory

The project directory is used for project-specific files. The user selects the location of the project directory when the editor starts. The project directory is mounted as the root of virtual paths. 

For example, let's assume we downloaded the engine repository in `c:/projects/black Engine`. We can access `c:/projects/black Engine/demo/models/ybot/ybot.fbx` using `models/ybot/ybot.fbx` in the engine.

As its name suggests, the project directory is used for project-specific files. Most files should be in this mount point.

# Mounting other directories

By default, only engine data and project directories are mounted in the editor. You can mount other directories using 

```cpp
void FileSystem::mount(StringView path, StringView virtual_path)
```

# Virtual path collision

Virtual paths must uniquely identify files. For example, if the project directory contains `engine/` subdirectory, files there are not accessible, because we already have engine data directory mounted to `engine/`.