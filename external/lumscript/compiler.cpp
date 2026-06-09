#include "compiler.h"
#include "utils.h"

ls_result ls_module_typecheck(ls_module* module) {
	return LS_RESULT_OK;
}

ls_result ls_module_compile(
	ls_module* module,
	ls_string_view source,
	ls_string_view source_name,
	ls_import_resolver_fn import_resolver,
	void* import_resolver_userdata
){
	ls_result parse_result = ls_module_parse(module, source, source_name);
	if (parse_result == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	if (ls_module_typecheck(module) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	return LS_RESULT_OK;
}
