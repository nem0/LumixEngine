# File Paths

In Lumix Engine, file paths are **case sensitive**. This means that `path/file.png` and `path/File.png` are considered different assets. This rule applies **even on case-insensitive filesystems**. For instance, attempting to load `path/file.png` when the actual file is named `path/File.png` will result in an error. This is to ensure more consistent engine behavior across different systems. We recommend using lower-case paths.

