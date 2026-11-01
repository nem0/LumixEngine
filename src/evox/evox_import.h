#pragma once

#include "core/path.h"
#include "core/string.h"

namespace Lumix {

// Index of the ':' of a "<name>:" prefix, e.g. "core:vec3" or "steam:steam", or -1 for other imports, which are paths in the project.
// A single letter before ':' is a drive ("C:/...").
inline i32 getEvoxImportPrefixLength(StringView import) {
	for (u64 i = 0; i < import.size(); ++i) {
		const char c = import[i];
		if (c == '/' || c == '\\') return -1;
		if (c == ':') return i > 1 ? (i32)i : -1;
	}
	return -1;
}

inline bool isEvoxPrefixedImport(StringView import) {
	return getEvoxImportPrefixLength(import) >= 0;
}

// Path of the script behind an import:
//   "core:vec3"       -> engine/scripts/core/vec3.evox
//   "steam:steam"     -> plugins/steam/steam.evox, a plugin mounts the directory with its scripts as plugins/<name>
//   "scripts/game"    -> scripts/game.evox
inline Path getEvoxImportPath(StringView import) {
	const i32 prefix_length = getEvoxImportPrefixLength(import);
	if (prefix_length < 0) {
		return endsWith(import, ".evox") ? Path(import) : Path(import, ".evox");
	}
	const StringView prefix(import.data, (u64)prefix_length);
	const StringView name = import.withoutLeft((u64)prefix_length + 1);
	const bool has_extension = endsWith(name, ".evox");
	if (prefix == StringView("core")) {
		return has_extension ? Path("engine/scripts/core/", name) : Path("engine/scripts/core/", name, ".evox");
	}
	return has_extension ? Path("plugins/", prefix, "/", name) : Path("plugins/", prefix, "/", name, ".evox");
}

} // namespace Lumix
