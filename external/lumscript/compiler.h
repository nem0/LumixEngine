#pragma once

#include "capi.h"

struct Module;
using ImportResolver = bool (*)(Module& module, ls_string_view path, ls_string_view alias, ls_string_view* source, void* userdata);

bool typecheck(Module& module);
bool parse(Module& module, ls_string_view source, ls_string_view declaration_prefix = {}, ls_string_view source_name = {});
bool compile(Module& module, ls_string_view source, ImportResolver import_resolver = nullptr, void* import_resolver_userdata = nullptr, ls_string_view source_name = {});
