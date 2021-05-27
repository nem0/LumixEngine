#include "engine/reflection.h"
#include "engine/allocator.h"
#include "engine/allocators.h"
#include "engine/crc32.h"
#include "engine/log.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "engine/universe.h"

namespace Lumix
{


namespace reflection
{
	
struct Context {
	Scene* first_scene = nullptr; 
	RegisteredComponent component_bases[ComponentType::MAX_TYPES_COUNT];
	u32 components_count = 0;
};

static Context& getContext() {
	static Context ctx;
	return ctx;
}

static IAllocator& getAllocator() {
	static DefaultAllocator alloc;
	return alloc;
}

Array<FunctionBase*>& allFunctions() {
	static Array<FunctionBase*> fncs(getAllocator());
	return fncs;
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

const PropertyBase* getProperty(ComponentType cmp_type, const char* prop_name) {
	const ComponentBase* cmp = getComponent(cmp_type);
	for (PropertyBase* prop : cmp->props) {
		if (equalStrings(prop->name, prop_name)) return prop;
	}
	return nullptr;
}

Scene::Scene(IAllocator& allocator)
	: cmps(allocator)
	, functions(allocator)
{}

builder::builder(IAllocator& allocator)
	: allocator(allocator)
{
	scene = LUMIX_NEW(allocator, Scene)(allocator);
}

void builder::registerCmp(ComponentBase* cmp) {
	cmp->scene = crc32(scene->name);
	getContext().component_bases[cmp->component_type.index].cmp = cmp;
	getContext().component_bases[cmp->component_type.index].name_hash = crc32(cmp->name);
	getContext().component_bases[cmp->component_type.index].scene = crc32(scene->name);
	scene->cmps.push(cmp);
}

ComponentType getComponentTypeFromHash(u32 hash)
{
	for (u32 i = 0, c = getContext().components_count; i < c; ++i) {
		if (getContext().component_bases[i].name_hash == hash) {
			return {(i32)i};
		}
	}
	ASSERT(false);
	return {-1};
}


u32 getComponentTypeHash(ComponentType type)
{
	return getContext().component_bases[type.index].name_hash;
}


ComponentType getComponentType(const char* name)
{
	Context& ctx = getContext();
	u32 name_hash = crc32(name);
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
	return {i32(getContext().components_count - 1)};
}

Scene* getFirstScene() {
	return getContext().first_scene;
}

void DynamicProperties::visit(IPropertyVisitor& visitor) const { visitor.visit(*this); }

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

builder build_scene(const char* name) {
	builder res(getAllocator());
	Context& ctx = getContext();
	res.scene->next = ctx.first_scene;
	ctx.first_scene = res.scene;
	res.scene->name = name;
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
	scene->cmps.back()->icon = icon;
	return *this;
}

void builder::addProp(PropertyBase* p) {
	if (array) {
		array->children.push(p);
	}
	else {
		scene->cmps.back()->props.push(p);
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
	getter(cmp.scene, (EntityRef)cmp.entity, idx, stream);
}

void BlobProperty::setValue(ComponentUID cmp, u32 idx, InputMemoryStream& stream) const {
	setter(cmp.scene, (EntityRef)cmp.entity, idx, stream);
}

ArrayProperty::ArrayProperty(IAllocator& allocator)
	: PropertyBase(allocator)
	, children(allocator)
{}

u32 ArrayProperty::getCount(ComponentUID cmp) const {
	return counter(cmp.scene, (EntityRef)cmp.entity);
}

void ArrayProperty::addItem(ComponentUID cmp, u32 idx) const {
	adder(cmp.scene, (EntityRef)cmp.entity, idx);
}

void ArrayProperty::removeItem(ComponentUID cmp, u32 idx) const {
	remover(cmp.scene, (EntityRef)cmp.entity, idx);
}


void ArrayProperty::visit(struct IPropertyVisitor& visitor) const {
	visitor.visit(*this);
}

void ArrayProperty::visitChildren(struct IPropertyVisitor& visitor) const {
	for (PropertyBase* prop : children) {
		prop->visit(visitor);
	}
}

} // namespace Reflection


} // namespace Lumix
