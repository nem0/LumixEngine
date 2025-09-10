#include "engine/reflection.h"
#include "core/allocator.h"
#include "core/hash.h"
#include "core/log.h"
#include "core/stream.h"
#include "core/string.h"
#include "engine/world.h"

namespace Lumix::reflection {

namespace detail {

StringView normalizeTypeName(StringView type_name) {
	StringView res = type_name;
	if (startsWith(res, "struct ")) res.removePrefix(7);
	if (startsWith(res, "Lumix::")) res.removePrefix(7);
	while (res.size() > 0 && res[0] == ' ') res.removePrefix(1);
	while (res.size() > 0 && res.back() == ' ') res.removeSuffix(1);
	return res;
}

} // namespace detail


struct Context {
	Module* first_module = nullptr; 
	RegisteredComponent component_bases[ComponentType::MAX_TYPES_COUNT];
	u32 components_count = 0;
};

static Context& getContext() {
	static Context ctx;
	return ctx;
}

ComponentBase::ComponentBase(IAllocator& allocator)
	: props(allocator)
	, functions(allocator)
{}

void ComponentBase::visit(IPropertyVisitor& visitor) const {
	for (const PropertyBase* prop : props) {
		prop->visit(visitor);
	}
}

const ComponentBase* getComponent(ComponentType cmp_type) {
	return getContext().component_bases[cmp_type.index].cmp;
}

const PropertyBase* getProperty(ComponentType cmp_type, StringView prop_name) {
	const ComponentBase* cmp = getComponent(cmp_type);
	if (!cmp) return nullptr;
	for (PropertyBase* prop : cmp->props) {
		if (equalStrings(prop->name, prop_name)) return prop;
	}
	return nullptr;
}

Module::Module(IAllocator& allocator)
	: cmps(allocator)
	, functions(allocator)
	, events(allocator)
{}

builder::builder(IAllocator& allocator)
	: allocator(allocator)
{
	module = LUMIX_NEW(allocator, Module)(allocator);
}

void builder::registerCmp(ComponentBase* cmp) {
	getContext().component_bases[cmp->component_type.index].cmp = cmp;
	getContext().component_bases[cmp->component_type.index].name_hash = RuntimeHash(cmp->name);
	getContext().component_bases[cmp->component_type.index].module_hash = RuntimeHash(module->name);
	module->cmps.push(cmp);
}

ComponentType getComponentTypeFromHash(RuntimeHash hash)
{
	for (u32 i = 0, c = getContext().components_count; i < c; ++i) {
		if (getContext().component_bases[i].name_hash == hash) {
			return {(i32)i};
		}
	}
	ASSERT(false);
	return {-1};
}

const PropertyBase* getPropertyFromHash(StableHash hash) {
	const Context& ctx = getContext();
	for (u32 i = 0; i < ctx.components_count; ++i) {
		const RegisteredComponent& cmp = ctx.component_bases[i];
		for (PropertyBase* prop : cmp.cmp->props) {
			RollingStableHasher hasher;
			hasher.begin();
			hasher.update(cmp.cmp->name, stringLength(cmp.cmp->name));
			hasher.update(prop->name, stringLength(prop->name));
			if (hasher.end64() == hash) return prop;
		}
	}
	return nullptr;
}

StableHash getPropertyHash(ComponentType cmp_type, const char* property_name) {
	RollingStableHasher hasher;
	hasher.begin();
	const ComponentBase* cmp = getComponent(cmp_type);
	if (!cmp) return StableHash();
	
	hasher.update(cmp->name, stringLength(cmp->name));
	hasher.update(property_name, stringLength(property_name));
	return hasher.end64();
}

bool componentTypeExists(const char* id) {
	Context& ctx = getContext();
	const RuntimeHash name_hash(id);
	for (u32 i = 0, c = ctx.components_count; i < c; ++i) {
		if (ctx.component_bases[i].name_hash == name_hash) {
			return true;
		}
	}
	return false;
}

ComponentType getComponentType(StringView name)
{
	Context& ctx = getContext();
	const RuntimeHash name_hash(name.begin, name.size());
	for (u32 i = 0, c = ctx.components_count; i < c; ++i) {
		if (ctx.component_bases[i].name_hash == name_hash) {
			return {(i32)i};
		}
	}

	if (ctx.components_count == ComponentType::MAX_TYPES_COUNT) {
		logError("Too many component types");
		return INVALID_COMPONENT_TYPE;
	}

	RegisteredComponent& type = ctx.component_bases[getContext().components_count];
	type.name_hash = name_hash;
	++ctx.components_count;
	//logInfo("Component type ", name, ", hash ", name_hash.getHashValue()); 
	return {i32(getContext().components_count - 1)};
}

Module* getFirstModule() {
	return getContext().first_module;
}

Span<const RegisteredComponent> getComponents() {
	return Span(getContext().component_bases, getContext().components_count);
}

struct RadiansAttribute : IAttribute
{
	Type getType() const override { return RADIANS; }
};

struct MultilineAttribute : IAttribute
{
	Type getType() const override { return MULTILINE; }
};

struct NoUIAttribute : IAttribute {
	Type getType() const override { return NO_UI; }
};

builder build_module(const char* name) {
	builder res(getGlobalAllocator());
	Context& ctx = getContext();
	res.module->next = ctx.first_module;
	ctx.first_module = res.module;
	res.module->name = name;
	return res;
}

builder& builder::radiansAttribute() {
	auto* a = LUMIX_NEW(allocator, RadiansAttribute);
	last_prop->attributes.push(a);
	return *this;
}

builder& builder::colorAttribute() {
	auto* a = LUMIX_NEW(allocator, ColorAttribute);
	last_prop->attributes.push(a);
	return *this;
}

builder& builder::noUIAttribute() {
	auto* a = LUMIX_NEW(allocator, NoUIAttribute);
	last_prop->attributes.push(a);
	return *this;
}

builder& builder::multilineAttribute() {
	auto* a = LUMIX_NEW(allocator, MultilineAttribute);
	last_prop->attributes.push(a);
	return *this;
}

builder& builder::minAttribute(float value) {
	auto* a = LUMIX_NEW(allocator, MinAttribute)(value);
	last_prop->attributes.push(a);
	return *this;
}

builder& builder::clampAttribute(float min, float max) {
	auto* a = LUMIX_NEW(allocator, ClampAttribute)(min, max);
	last_prop->attributes.push(a);
	return *this;
}

builder& builder::resourceAttribute(ResourceType type) {
	auto* a = LUMIX_NEW(allocator, ResourceAttribute)(type);
	last_prop->attributes.push(a);
	return *this;
}

builder& builder::end_array() {
	array = nullptr;
	last_prop = nullptr;
	return *this;
}

builder& builder::icon(const char* icon) {
	module->cmps.back()->icon = icon;
	return *this;
}

void builder::addProp(PropertyBase* p) {
	if (array) {
		array->children.push(p);
	}
	else {
		module->cmps.back()->props.push(p);
		p->cmp = module->cmps.back();
	}
	last_prop = p;
}

BlobProperty::BlobProperty(IAllocator& allocator)
	: PropertyBase(allocator)
{}

void BlobProperty::visit(struct IPropertyVisitor& visitor) const {
	visitor.visit(*this);
}

void BlobProperty::getValue(ComponentUID cmp, u32 idx, OutputMemoryStream& stream) const {
	getter(cmp.module, (EntityRef)cmp.entity, idx, stream);
}

void BlobProperty::setValue(ComponentUID cmp, u32 idx, InputMemoryStream& stream) const {
	setter(cmp.module, (EntityRef)cmp.entity, idx, stream);
}

ArrayProperty::ArrayProperty(IAllocator& allocator)
	: PropertyBase(allocator)
	, children(allocator)
{}

u32 ArrayProperty::getCount(ComponentUID cmp) const {
	return counter(cmp.module, (EntityRef)cmp.entity);
}

void ArrayProperty::addItem(ComponentUID cmp, u32 idx) const {
	adder(cmp.module, (EntityRef)cmp.entity, idx);
}

void ArrayProperty::removeItem(ComponentUID cmp, u32 idx) const {
	remover(cmp.module, (EntityRef)cmp.entity, idx);
}


void ArrayProperty::visit(struct IPropertyVisitor& visitor) const {
	visitor.visit(*this);
}

void ArrayProperty::visitChildren(struct IPropertyVisitor& visitor) const {
	for (PropertyBase* prop : children) {
		prop->visit(visitor);
	}
}

} // namespace Lumix::reflection
