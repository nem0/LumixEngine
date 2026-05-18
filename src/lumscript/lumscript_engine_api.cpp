#include "lumscript/lumscript_engine_api.h"
#include "core/log.h"
#include "engine/input_system.h"
#include "lumscript/lumscript_ast.h"
#include "lumscript/lumscript_runtime.h"
#include "engine/reflection.h"
#include "engine/world.h"
#include "lumscript/lumscript_capi.gen.h"
#include "imgui/imgui.h"

namespace Lumix::LumScript {

namespace {

struct EngineFunctionContext {
	World* world = nullptr;
	ComponentType component_type = INVALID_COMPONENT_TYPE;
	const reflection::FunctionBase* function = nullptr;
	TypeRef return_type;
};

StringView makeEngineName(Module& module, StringView alias, const char* name) {
	return module.makeQualifiedName(alias, name);
}

i32 addNativeType(Module& module, StringView name, StringView id) {
	for (i32 i = 0; i < module.native_types.size(); ++i) {
		if (equalStrings(module.native_types[i].name, name)) return i;
	}
	NativeTypeDecl& type = module.native_types.emplace();
	type.name = module.copyName(name);
	type.id = module.copyName(id);
	return module.native_types.size() - 1;
}

TypeRef nativeType(Module& module, StringView visible_name, StringView id) {
	const i32 idx = addNativeType(module, visible_name, id);
	return TypeRef(TypeRef::NATIVE, module.native_types[idx].id, idx);
}

bool toLumScriptType(Module& module, reflection::TypeDescriptor type, StringView entity_type_name, TypeRef* out) {
	using V = reflection::Variant;
	switch (type.type) {
		case V::VOID: *out = TypeRef(TypeRef::VOID); return true;
		case V::BOOL: *out = TypeRef(TypeRef::BOOL); return true;
		case V::I32: *out = TypeRef(TypeRef::I32); return true;
		case V::U32: *out = TypeRef(TypeRef::I32); return true;
		case V::FLOAT: *out = TypeRef(TypeRef::F32); return true;
		case V::ENTITY: *out = nativeType(module, entity_type_name, "engine:entity/Entity"); return true;
		default: return false;
	}
}

reflection::Variant toVariant(const Value& value, reflection::Variant::Type type) {
	reflection::Variant v;
	switch (type) {
		case reflection::Variant::BOOL: v = value.b; break;
		case reflection::Variant::I32: v = value.i; break;
		case reflection::Variant::U32: v = (u32)value.i; break;
		case reflection::Variant::FLOAT: v = value.f; break;
		case reflection::Variant::ENTITY: v = EntityRef(value.i); break;
		default: break;
	}
	return v;
}

Value fromVariant(Span<const u8> ret_mem, reflection::Variant::Type type, TypeRef lum_type) {
	Value v;
	v.type = lum_type;
	switch (type) {
		case reflection::Variant::BOOL: v.b = *(const bool*)ret_mem.begin(); break;
		case reflection::Variant::I32: v.i = *(const i32*)ret_mem.begin(); v.f = (float)v.i; break;
		case reflection::Variant::U32: v.u = *(const u32*)ret_mem.begin(); v.i = (i32)v.u; v.f = (float)v.u; break;
		case reflection::Variant::FLOAT: v.f = *(const float*)ret_mem.begin(); v.i = (i32)v.f; break;
		case reflection::Variant::ENTITY: v.i = ((const EntityPtr*)ret_mem.begin())->index; break;
		default: break;
	}
	return v;
}

bool callEngineFunction(Span<const Value> args, Value* result, void* userdata) {
	EngineFunctionContext* ctx = (EngineFunctionContext*)userdata;
	if (!ctx->world) return false;
	IModule* engine_module = ctx->world->getModule(ctx->component_type);
	if (!engine_module) return false;

	Array<reflection::Variant> variants(ctx->world->getAllocator());
	for (u32 i = 0; i < ctx->function->getArgCount(); ++i) {
		variants.push(toVariant(args[i], ctx->function->getArgType(i).type));
	}

	u8 ret_storage[64];
	reflection::TypeDescriptor return_type = ctx->function->getReturnType();
	ASSERT(return_type.size <= sizeof(ret_storage));
	ctx->function->invoke(engine_module, Span(ret_storage, return_type.size), variants);

	if (result && return_type.type != reflection::Variant::VOID) {
		*result = fromVariant(Span((const u8*)ret_storage, return_type.size), return_type.type, ctx->return_type);
	}
	return true;
}

const InputSystem::Event* getInputEvent(const Value& value) {
	InputSystem* input = (InputSystem*)value.ptr;
	if (!input || value.i < 0) return nullptr;
	const Span<const InputSystem::Event> events = input->getEvents();
	if ((u32)value.i >= events.length()) return nullptr;
	return &events[value.i];
}

bool inputGetEventCount(Span<const Value> args, Value* result, void*) {
	InputSystem* input = (InputSystem*)args[0].ptr;
	*result = Runtime::makeI32(input ? input->getEvents().length() : 0);
	return true;
}

bool inputGetEvent(Span<const Value> args, Value* result, void*) {
	InputSystem* input = (InputSystem*)args[0].ptr;
	if (!input) return false;
	const Span<const InputSystem::Event> events = input->getEvents();
	if (args[1].i < 0 || (u32)args[1].i >= events.length()) return false;
	result->type = TypeRef(TypeRef::NATIVE, "engine:input/InputEvent", -1);
	result->ptr = input;
	result->i = args[1].i;
	return true;
}

bool inputGetType(Span<const Value> args, Value* result, void*) {
	const InputSystem::Event* event = getInputEvent(args[0]);
	*result = Runtime::makeI32(event ? (i32)event->type : -1);
	return true;
}

bool inputGetDeviceType(Span<const Value> args, Value* result, void*) {
	const InputSystem::Event* event = getInputEvent(args[0]);
	*result = Runtime::makeI32(event && event->device ? (i32)event->device->type : -1);
	return true;
}

bool inputGetDeviceIndex(Span<const Value> args, Value* result, void*) {
	const InputSystem::Event* event = getInputEvent(args[0]);
	*result = Runtime::makeI32(event && event->device ? (i32)event->device->index : -1);
	return true;
}

bool inputGetKeyId(Span<const Value> args, Value* result, void*) {
	const InputSystem::Event* event = getInputEvent(args[0]);
	*result = Runtime::makeI32(event && event->type == InputEventType::BUTTON ? (i32)event->data.button.key_id : -1);
	return true;
}

bool inputIsDown(Span<const Value> args, Value* result, void*) {
	const InputSystem::Event* event = getInputEvent(args[0]);
	*result = Runtime::makeBool(event && event->type == InputEventType::BUTTON && event->data.button.down);
	return true;
}

bool inputIsRepeat(Span<const Value> args, Value* result, void*) {
	const InputSystem::Event* event = getInputEvent(args[0]);
	*result = Runtime::makeBool(event && event->type == InputEventType::BUTTON && event->data.button.is_repeat);
	return true;
}

bool inputGetX(Span<const Value> args, Value* result, void*) {
	const InputSystem::Event* event = getInputEvent(args[0]);
	float value = 0;
	if (event) {
		switch (event->type) {
			case InputEventType::BUTTON: value = event->data.button.x; break;
			case InputEventType::AXIS: value = event->data.axis.x; break;
			case InputEventType::MOUSE_WHEEL: value = event->data.mouse_wheel.x; break;
			default: break;
		}
	}
	*result = Runtime::makeF32(value);
	return true;
}

bool inputGetY(Span<const Value> args, Value* result, void*) {
	const InputSystem::Event* event = getInputEvent(args[0]);
	float value = 0;
	if (event) {
		switch (event->type) {
			case InputEventType::BUTTON: value = event->data.button.y; break;
			case InputEventType::AXIS: value = event->data.axis.y; break;
			case InputEventType::MOUSE_WHEEL: value = event->data.mouse_wheel.y; break;
			default: break;
		}
	}
	*result = Runtime::makeF32(value);
	return true;
}

bool inputGetValue(Span<const Value> args, Value* result, void*) {
	const InputSystem::Event* event = getInputEvent(args[0]);
	float value = 0;
	if (event) {
		switch (event->type) {
			case InputEventType::AXIS: value = event->data.axis.y; break;
			case InputEventType::MOUSE_WHEEL: value = event->data.mouse_wheel.y; break;
			case InputEventType::BUTTON: value = event->data.button.down ? 1.0f : 0.0f; break;
			default: break;
		}
	}
	*result = Runtime::makeF32(value);
	return true;
}

bool inputGetAxis(Span<const Value> args, Value* result, void*) {
	const InputSystem::Event* event = getInputEvent(args[0]);
	*result = Runtime::makeI32(event && event->type == InputEventType::AXIS ? (i32)event->data.axis.axis : -1);
	return true;
}

bool inputGetText(Span<const Value> args, Value* result, void*) {
	const InputSystem::Event* event = getInputEvent(args[0]);
	*result = Runtime::makeI32(event && event->type == InputEventType::TEXT_INPUT ? (i32)event->data.text.utf8 : 0);
	return true;
}

bool logLogError(Span<const Value> args, Value*, void*) {
	logError(args[0].string);
	return true;
}

bool logLogInfo(Span<const Value> args, Value*, void*) {
	logInfo(args[0].string);
	return true;
}

bool registerInputModule(Module& module, StringView alias) {
	generated::registerGeneratedEngineImport(module, nullptr, "engine:InputEventType", {});
	TypeRef system_type = nativeType(module, makeEngineName(module, alias, "InputSystem"), "engine:input/InputSystem");
	TypeRef event_type = nativeType(module, makeEngineName(module, alias, "InputEvent"), "engine:input/InputEvent");
	TypeRef event_type_enum(TypeRef::ENUM, "InputEventType", -1);
	{
		TypeRef params[] = { system_type };
		addNativeFunction(module, makeEngineName(module, alias, "getEventCount"), TypeRef(TypeRef::I32), Span<const TypeRef>(params), &inputGetEventCount);
	}
	{
		TypeRef params[] = { system_type, TypeRef(TypeRef::I32) };
		addNativeFunction(module, makeEngineName(module, alias, "getEvent"), event_type, Span<const TypeRef>(params), &inputGetEvent);
	}
	{
		TypeRef params[] = { event_type };
		addNativeFunction(module, makeEngineName(module, alias, "getType"), event_type_enum, Span<const TypeRef>(params), &inputGetType);
		addNativeFunction(module, makeEngineName(module, alias, "getDeviceType"), TypeRef(TypeRef::I32), Span<const TypeRef>(params), &inputGetDeviceType);
		addNativeFunction(module, makeEngineName(module, alias, "getDeviceIndex"), TypeRef(TypeRef::I32), Span<const TypeRef>(params), &inputGetDeviceIndex);
		addNativeFunction(module, makeEngineName(module, alias, "getKeyId"), TypeRef(TypeRef::I32), Span<const TypeRef>(params), &inputGetKeyId);
		addNativeFunction(module, makeEngineName(module, alias, "isDown"), TypeRef(TypeRef::BOOL), Span<const TypeRef>(params), &inputIsDown);
		addNativeFunction(module, makeEngineName(module, alias, "isRepeat"), TypeRef(TypeRef::BOOL), Span<const TypeRef>(params), &inputIsRepeat);
		addNativeFunction(module, makeEngineName(module, alias, "getX"), TypeRef(TypeRef::F32), Span<const TypeRef>(params), &inputGetX);
		addNativeFunction(module, makeEngineName(module, alias, "getY"), TypeRef(TypeRef::F32), Span<const TypeRef>(params), &inputGetY);
		addNativeFunction(module, makeEngineName(module, alias, "getValue"), TypeRef(TypeRef::F32), Span<const TypeRef>(params), &inputGetValue);
		addNativeFunction(module, makeEngineName(module, alias, "getAxis"), TypeRef(TypeRef::I32), Span<const TypeRef>(params), &inputGetAxis);
		addNativeFunction(module, makeEngineName(module, alias, "getText"), TypeRef(TypeRef::I32), Span<const TypeRef>(params), &inputGetText);
	}
	return true;
}

bool registerLogModule(Module& module, StringView alias) {
	TypeRef params[] = {TypeRef(TypeRef::STRING)};
	addNativeFunction(module, makeEngineName(module, alias, "logError"), TypeRef(TypeRef::VOID), Span<const TypeRef>(params), &logLogError);
	addNativeFunction(module, makeEngineName(module, alias, "logInfo"), TypeRef(TypeRef::VOID), Span<const TypeRef>(params), &logLogInfo);
	return true;
}

bool imguiBeginWindow(Span<const Value> args, Value* result, void*) {
	bool open = false;
	if (ImGui::GetCurrentContext()) {
		StaticString<256> title(args[0].string);
		open = ImGui::Begin(title);
	}
	*result = Runtime::makeBool(open);
	return true;
}

bool imguiEndWindow(Span<const Value>, Value*, void*) {
	if (ImGui::GetCurrentContext()) ImGui::End();
	return true;
}

bool imguiTextUnformatted(Span<const Value> args, Value*, void*) {
	if (ImGui::GetCurrentContext()) ImGui::TextUnformatted(args[0].string.begin, args[0].string.end);
	return true;
}

bool imguiButton(Span<const Value> args, Value* result, void*) {
	bool clicked = false;
	if (ImGui::GetCurrentContext()) {
		StaticString<256> label(args[0].string);
		clicked = ImGui::Button(label);
	}
	*result = Runtime::makeBool(clicked);
	return true;
}

bool registerImguiModule(Module& module, StringView alias) {
	{
		TypeRef params[] = {TypeRef(TypeRef::STRING)};
		addNativeFunction(module, makeEngineName(module, alias, "beginWindow"), TypeRef(TypeRef::BOOL), Span<const TypeRef>(params), &imguiBeginWindow);
		addNativeFunction(module, makeEngineName(module, alias, "textUnformatted"), TypeRef(TypeRef::VOID), Span<const TypeRef>(params), &imguiTextUnformatted);
		addNativeFunction(module, makeEngineName(module, alias, "button"), TypeRef(TypeRef::BOOL), Span<const TypeRef>(params), &imguiButton);
	}
	addNativeFunction(module, makeEngineName(module, alias, "endWindow"), TypeRef(TypeRef::VOID), Span<const TypeRef>(), &imguiEndWindow);
	return true;
}

bool registerComponentModule(Module& module, World* world, const reflection::ComponentBase& component, StringView alias) {
	const StringView entity_type_name = module.makeQualifiedName("entity", "Entity");
	bool registered_any = false;
	for (const reflection::FunctionBase* function : component.functions) {
		TypeRef return_type;
		if (!toLumScriptType(module, function->getReturnType(), entity_type_name, &return_type)) continue;

		Array<TypeRef> params(module.allocator);
		bool supported = true;
		for (u32 i = 0; i < function->getArgCount(); ++i) {
			TypeRef param_type;
			if (!toLumScriptType(module, function->getArgType(i), entity_type_name, &param_type)) {
				supported = false;
				break;
			}
			params.push(param_type);
		}
		if (!supported) continue;

		void* mem = module.allocator.allocate(sizeof(EngineFunctionContext), alignof(EngineFunctionContext));
		module.allocated_native_data.push(mem);
		EngineFunctionContext* ctx = new (NewPlaceholder(), mem) EngineFunctionContext;
		ctx->world = world;
		ctx->component_type = component.component_type;
		ctx->function = function;
		ctx->return_type = return_type;

		NativeFunctionDecl& fn = addNativeFunction(
			module,
			makeEngineName(module, alias, function->name),
			return_type,
			params,
			&callEngineFunction,
			ctx
		);
		for (i32 i = 0; i < fn.params.size(); ++i) fn.params[i].type = params[i];
		registered_any = true;
	}
	return registered_any;
}

} // namespace

bool resolveEngineImport(Module& module, World* world, StringView path, StringView alias) {
	if (generated::registerGeneratedEngineImport(module, world, path, alias)) return true;
	if (!startsWith(path, "engine:")) return false;
	if (alias.empty()) return false;

	const StringView name = path.withoutLeft(7);
	if (equalStrings(name, "entity")) {
		nativeType(module, makeEngineName(module, alias, "Entity"), "engine:entity/Entity");
		return true;
	}
	if (equalStrings(name, "input")) return registerInputModule(module, alias);
	if (equalStrings(name, "log")) return registerLogModule(module, alias);
	if (equalStrings(name, "imgui")) return registerImguiModule(module, alias);

	for (const reflection::RegisteredComponent& registered : reflection::getComponents()) {
		const reflection::ComponentBase* component = registered.cmp;
		if (!component || !equalStrings(component->name, name)) continue;
		nativeType(module, makeEngineName(module, "entity", "Entity"), "engine:entity/Entity");
		return registerComponentModule(module, world, *component, alias);
	}

	return false;
}

} // namespace Lumix::LumScript
