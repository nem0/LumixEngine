#include "lumscript/lumscript_engine_api.h"
#include "core/log.h"
#include "engine/input_system.h"
#include "engine/reflection.h"
#include "engine/world.h"
#include "lumscript/value.h"
#include "lumscript/lumscript_capi.gen.h"
#include "lumscript/capi.h"
#include "imgui/imgui.h"
#include <string.h>

namespace Lumix::LumScript {

namespace {

static int logLogError(const ls_value* args, size_t arg_count, ls_value*, void*) {
	if (arg_count < 1) return 0;
	logError(StringView(args[0].string.begin, args[0].string.end));
	return 1;
}

static int logLogInfo(const ls_value* args, size_t arg_count, ls_value*, void*) {
	if (arg_count < 1) return 0;
	logInfo(StringView(args[0].string.begin, args[0].string.end));
	return 1;
}

static ls_string_view toC(StringView value) {
	return {value.begin, value.end};
}

static ls_string_view lsStringView(const char* value) {
	return {value, value + strlen(value)};
}

static StringView fromC(ls_string_view value) {
	return {value.begin, value.end};
}

static Value fromC(ls_value value) {
	Value result;
	result.type = TypeRef((TypeRef::Kind)value.type.kind, fromC(value.type.name), value.type.struct_index);
	result.type.element_kind = (TypeRef::Kind)value.type.element_kind;
	result.type.element_name = fromC(value.type.element_name);
	result.type.array_size = value.type.array_size;
	result.type.nullable = value.type.nullable != 0;
	result.b = value.b != 0;
	result.i = value.i;
	result.u = value.u;
	result.i64 = value.i64;
	result.u64 = value.u64;
	result.f = value.f;
	result.d = value.d;
	result.string = fromC(value.string);
	for (i32 i = 0; i < 4; ++i) result.composite[i] = value.composite[i];
	result.ptr = value.ptr;
	return result;
}

static ls_string_view makeEngineName(ls_module* module, StringView alias, const char* name) {
	return ls_make_qualified_name(module, toC(alias), ls_string_view{name, name + strlen(name)});
}

static ls_type nativeType(ls_module* module, ls_string_view visible_name, ls_string_view id) {
	const int idx = ls_module_add_native_type(module, visible_name, id);
	return ls_type_make_native(visible_name, idx, 0);
}

static const InputSystem::Event* getInputEvent(const Value& value) {
	InputSystem* input = (InputSystem*)value.ptr;
	if (!input || value.i < 0) return nullptr;
	// TODO this should not go through input->, `value` should point directly to the event
	const Span<const InputSystem::Event> events = input->getEvents();
	if ((u32)value.i >= events.length()) return nullptr;
	return &events[value.i];
}

static int inputGetEventCount(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 1) return 0;
	InputSystem* input = (InputSystem*)fromC(args[0]).ptr;
	if (result) *result = ls_value_make_i32(input ? input->getEvents().length() : 0);
	return 1;
}

static int inputGetEvent(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 2) return 0;
	InputSystem* input = (InputSystem*)fromC(args[0]).ptr;
	const int idx = fromC(args[1]).i;
	if (!input || idx < 0) return 0;
	const Span<const InputSystem::Event> events = input->getEvents();
	if ((u32)idx >= events.length()) return 0;
	if (!result) return 1;
	result->type = ls_type_make_native(lsStringView("engine:input/InputEvent"), -1, 0);
	result->ptr = input;
	result->i = idx;
	return 1;
}

static int inputGetType(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 1) return 0;
	const InputSystem::Event* event = getInputEvent(fromC(args[0]));
	if (result) *result = ls_value_make_i32(event ? (i32)event->type : -1);
	return 1;
}

static int inputGetDeviceType(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 1) return 0;
	const InputSystem::Event* event = getInputEvent(fromC(args[0]));
	if (result) *result = ls_value_make_i32(event && event->device ? (i32)event->device->type : -1);
	return 1;
}

static int inputGetDeviceIndex(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 1) return 0;
	const InputSystem::Event* event = getInputEvent(fromC(args[0]));
	if (result) *result = ls_value_make_i32(event && event->device ? (i32)event->device->index : -1);
	return 1;
}

static int inputGetKeyId(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 1) return 0;
	const InputSystem::Event* event = getInputEvent(fromC(args[0]));
	if (result) *result = ls_value_make_i32(event && event->type == InputEventType::BUTTON ? (i32)event->data.button.key_id : -1);
	return 1;
}

static int inputIsDown(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 1) return 0;
	const InputSystem::Event* event = getInputEvent(fromC(args[0]));
	if (result) *result = ls_value_make_bool(event && event->type == InputEventType::BUTTON && event->data.button.down);
	return 1;
}

static int inputIsRepeat(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 1) return 0;
	const InputSystem::Event* event = getInputEvent(fromC(args[0]));
	if (result) *result = ls_value_make_bool(event && event->type == InputEventType::BUTTON && event->data.button.is_repeat);
	return 1;
}

static int inputGetX(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 1) return 0;
	const InputSystem::Event* event = getInputEvent(fromC(args[0]));
	float value = 0;
	if (event) {
		switch (event->type) {
			case InputEventType::BUTTON: value = event->data.button.x; break;
			case InputEventType::AXIS: value = event->data.axis.x; break;
			case InputEventType::MOUSE_WHEEL: value = event->data.mouse_wheel.x; break;
			default: break;
		}
	}
	if (result) *result = ls_value_make_f32(value);
	return 1;
}

static int inputGetY(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 1) return 0;
	const InputSystem::Event* event = getInputEvent(fromC(args[0]));
	float value = 0;
	if (event) {
		switch (event->type) {
			case InputEventType::BUTTON: value = event->data.button.y; break;
			case InputEventType::AXIS: value = event->data.axis.y; break;
			case InputEventType::MOUSE_WHEEL: value = event->data.mouse_wheel.y; break;
			default: break;
		}
	}
	if (result) *result = ls_value_make_f32(value);
	return 1;
}

static int inputGetValue(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 1) return 0;
	const InputSystem::Event* event = getInputEvent(fromC(args[0]));
	float value = 0;
	if (event) {
		switch (event->type) {
			case InputEventType::AXIS: value = event->data.axis.y; break;
			case InputEventType::MOUSE_WHEEL: value = event->data.mouse_wheel.y; break;
			case InputEventType::BUTTON: value = event->data.button.down ? 1.0f : 0.0f; break;
			default: break;
		}
	}
	if (result) *result = ls_value_make_f32(value);
	return 1;
}

static int inputGetAxis(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 1) return 0;
	const InputSystem::Event* event = getInputEvent(fromC(args[0]));
	if (result) *result = ls_value_make_i32(event && event->type == InputEventType::AXIS ? (i32)event->data.axis.axis : -1);
	return 1;
}

static int inputGetText(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 1) return 0;
	const InputSystem::Event* event = getInputEvent(fromC(args[0]));
	if (result) *result = ls_value_make_i32(event && event->type == InputEventType::TEXT_INPUT ? (i32)event->data.text.utf8 : 0);
	return 1;
}

bool registerInputModule(ls_module* module, StringView alias) {
	if (!module) return false;

	const ls_type system_type = nativeType(module, makeEngineName(module, alias, "InputSystem"), lsStringView("engine:input/InputSystem"));
	const ls_type event_type = nativeType(module, makeEngineName(module, alias, "InputEvent"), lsStringView("engine:input/InputEvent"));
	// TODO this is generated by meta in lumscript_capi.gen.h
	const ls_enum_member event_type_enum[] = {
		{lsStringView("BUTTON"), 0},
		{lsStringView("AXIS"), 1},
		{lsStringView("MOUSE_WHEEL"), 2},
		{lsStringView("TEXT_INPUT"), 3},
		{lsStringView("DEVICE_ADDED"), 4},
		{lsStringView("DEVICE_REMOVED"), 5},
	};
	const int event_type_enum_idx = ls_module_add_enum(module, lsStringView("InputEventType"), event_type_enum, lengthOf(event_type_enum));
	const ls_type event_type_enum_type = ls_type_make_enum(lsStringView("InputEventType"), event_type_enum_idx, 0);

	{
		const ls_type params[] = { system_type };
		ls_module_add_native_function(module, makeEngineName(module, alias, "getEventCount"), ls_type_make(LS_TYPE_I32), params, lengthOf(params), &inputGetEventCount, nullptr);
	}
	{
		const ls_type params[] = { system_type, ls_type_make(LS_TYPE_I32) };
		ls_module_add_native_function(module, makeEngineName(module, alias, "getEvent"), event_type, params, lengthOf(params), &inputGetEvent, nullptr);
	}
	{
		const ls_type params[] = { event_type };
		ls_module_add_native_function(module, makeEngineName(module, alias, "getType"), event_type_enum_type, params, lengthOf(params), &inputGetType, nullptr);
		ls_module_add_native_function(module, makeEngineName(module, alias, "getDeviceType"), ls_type_make(LS_TYPE_I32), params, lengthOf(params), &inputGetDeviceType, nullptr);
		ls_module_add_native_function(module, makeEngineName(module, alias, "getDeviceIndex"), ls_type_make(LS_TYPE_I32), params, lengthOf(params), &inputGetDeviceIndex, nullptr);
		ls_module_add_native_function(module, makeEngineName(module, alias, "getKeyId"), ls_type_make(LS_TYPE_I32), params, lengthOf(params), &inputGetKeyId, nullptr);
		ls_module_add_native_function(module, makeEngineName(module, alias, "isDown"), ls_type_make(LS_TYPE_BOOL), params, lengthOf(params), &inputIsDown, nullptr);
		ls_module_add_native_function(module, makeEngineName(module, alias, "isRepeat"), ls_type_make(LS_TYPE_BOOL), params, lengthOf(params), &inputIsRepeat, nullptr);
		ls_module_add_native_function(module, makeEngineName(module, alias, "getX"), ls_type_make(LS_TYPE_F32), params, lengthOf(params), &inputGetX, nullptr);
		ls_module_add_native_function(module, makeEngineName(module, alias, "getY"), ls_type_make(LS_TYPE_F32), params, lengthOf(params), &inputGetY, nullptr);
		ls_module_add_native_function(module, makeEngineName(module, alias, "getValue"), ls_type_make(LS_TYPE_F32), params, lengthOf(params), &inputGetValue, nullptr);
		ls_module_add_native_function(module, makeEngineName(module, alias, "getAxis"), ls_type_make(LS_TYPE_I32), params, lengthOf(params), &inputGetAxis, nullptr);
		ls_module_add_native_function(module, makeEngineName(module, alias, "getText"), ls_type_make(LS_TYPE_I32), params, lengthOf(params), &inputGetText, nullptr);
	}
	return true;
}

bool registerLogModule(ls_module* module, StringView alias) {
	const ls_type params[] = {ls_type_make(LS_TYPE_STRING)};
	ls_module_add_native_function(module, makeEngineName(module, alias, "logError"), ls_type_make(LS_TYPE_VOID), params, lengthOf(params), &logLogError, nullptr);
	ls_module_add_native_function(module, makeEngineName(module, alias, "logInfo"), ls_type_make(LS_TYPE_VOID), params, lengthOf(params), &logLogInfo, nullptr);
	return true;
}

static int imguiBeginWindow(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 1) return 0;
	bool open = false;
	if (ImGui::GetCurrentContext()) {
		StaticString<256> title(fromC(args[0]).string);
		open = ImGui::Begin(title);
	}
	if (result) *result = ls_value_make_bool(open);
	return 1;
}

static int imguiEndWindow(const ls_value*, size_t, ls_value*, void*) {
	if (ImGui::GetCurrentContext()) ImGui::End();
	return 1;
}

static int imguiTextUnformatted(const ls_value* args, size_t arg_count, ls_value*, void*) {
	if (arg_count < 1) return 0;
	if (ImGui::GetCurrentContext()) {
		const StringView text = fromC(args[0]).string;
		ImGui::TextUnformatted(text.begin, text.end);
	}
	return 1;
}

static int imguiButton(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 1) return 0;
	bool clicked = false;
	if (ImGui::GetCurrentContext()) {
		StaticString<256> label(fromC(args[0]).string);
		clicked = ImGui::Button(label);
	}
	if (result) *result = ls_value_make_bool(clicked);
	return 1;
}

bool registerImguiModule(ls_module* module, StringView alias) {
	if (!module) return false;
	const ls_type params[] = {ls_type_make(LS_TYPE_STRING)};
	ls_module_add_native_function(module, makeEngineName(module, alias, "beginWindow"), ls_type_make(LS_TYPE_BOOL), params, lengthOf(params), &imguiBeginWindow, nullptr);
	ls_module_add_native_function(module, makeEngineName(module, alias, "textUnformatted"), ls_type_make(LS_TYPE_VOID), params, lengthOf(params), &imguiTextUnformatted, nullptr);
	ls_module_add_native_function(module, makeEngineName(module, alias, "button"), ls_type_make(LS_TYPE_BOOL), params, lengthOf(params), &imguiButton, nullptr);
	ls_module_add_native_function(module, makeEngineName(module, alias, "endWindow"), ls_type_make(LS_TYPE_VOID), nullptr, 0, &imguiEndWindow, nullptr);
	return true;
}

} // namespace

bool resolveEngineImport(ls_module& c_module, World* world, StringView path, StringView alias) {
	if (generated::registerGeneratedEngineImport(&c_module, world, toC(path), toC(alias))) return true;
	if (!startsWith(path, "engine:")) return false;
	if (alias.empty()) return false;

	const StringView name = path.withoutLeft(7);
	if (equalStrings(name, "entity")) {
		nativeType(&c_module, makeEngineName(&c_module, alias, "Entity"), lsStringView("engine:entity/Entity"));
		return true;
	}
	if (equalStrings(name, "input")) return registerInputModule(&c_module, alias);
	if (equalStrings(name, "log")) return registerLogModule(&c_module, alias);
	if (equalStrings(name, "imgui")) return registerImguiModule(&c_module, alias);

	return false;
}

} // namespace Lumix::LumScript
