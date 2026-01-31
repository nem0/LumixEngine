# File Paths

In black.h Engine, file paths are **case-sensitive**. This means that `path/file.png` and `path/File.png` are considered different assets. This rule applies even on case-insensitive filesystems. For example, attempting to load `path/file.png` when the actual file is named `path/File.png` will result in an error. This ensures consistent engine behavior across systems. We recommend using lowercase paths.

