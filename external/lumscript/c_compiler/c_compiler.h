#pragma once

#include "compiler.h"

// Internal C-backend output callback. This is intentionally not part of capi.h.
typedef void (*ls_c_output_fn)(void* user_data, ls_string_view text);

// Converts the checked scalar subset of the AST to C. Returns false when the
// unit uses a feature the small C backend cannot represent yet.
bool c_compile(const Unit& unit, ls_c_output_fn output, void* output_user_data);
bool c_compile(const ls_module& module, ls_c_output_fn output, void* output_user_data);
