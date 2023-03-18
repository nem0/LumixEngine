#include <imgui/imgui.h>

#include "property_grid.h"
#include "asset_browser.h"
#include "editor/prefab_system.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/crt.h"
#include "engine/plugin.h"
#include "engine/math.h"
#include "engine/prefab.h"
#include "engine/reflection.h"
#include "engine/resource.h"
#include "engine/stream.h"
#include "engine/world.h"
#include "engine/math.h"
#include "utils.h"


namespace Lumix
{

static const ComponentType GUI_RECT_TYPE = reflection::getComponentType("gui_rect");
static const ComponentType GUI_CANVAS_TYPE = reflection::getComponentType("gui_canvas");


PropertyGrid::PropertyGrid(StudioApp& app)
	: m_app(app)
	, m_is_open(true)
	, m_plugins(app.getAllocator())
	, m_deferred_select(INVALID_ENTITY)
{
	m_component_filter[0] = '\0';

	m_toggle_ui.init("Inspector", "Toggle Inspector UI", "inspector", "", false);
	m_toggle_ui.func.bind<&PropertyGrid::toggleUI>(this);
	m_toggle_ui.is_selected.bind<&PropertyGrid::isOpen>(this);
	
	m_app.addWindowAction(&m_toggle_ui);
}


PropertyGrid::~PropertyGrid()
{
	m_app.removeAction(&m_toggle_ui);
	ASSERT(m_plugins.empty());
}


struct GridUIVisitor final : reflection::IPropertyVisitor
{
	GridUIVisitor(StudioApp& app, int index, const Array<EntityRef>& entities, ComponentType cmp_type, WorldEditor& editor)
		: m_entities(entities)
		, m_cmp_type(cmp_type)
		, m_editor(editor)
		, m_index(index)
		, m_grid(app.getPropertyGrid())
		, m_app(app)
	{}


	ComponentUID getComponent() const
	{
		ComponentUID first_entity_cmp;
		first_entity_cmp.type = m_cmp_type;
		first_entity_cmp.scene = m_editor.getWorld()->getScene(m_cmp_type);
		first_entity_cmp.entity = m_entities[0];
		return first_entity_cmp;
	}


	struct Attributes {
		float max = FLT_MAX;
		float min = -FLT_MAX;
		bool is_color = false;
		bool is_radians = false;
		bool is_multiline = false;
		bool no_ui = false;
		ResourceType resource_type;
	};

	template <typename T>
	static Attributes getAttributes(const reflection::Property<T>& prop)
	{
		Attributes attrs;
		for (const reflection::IAttribute* attr : prop.attributes) {
			switch (attr->getType()) {
				case reflection::IAttribute::RADIANS:
					attrs.is_radians = true;
					break;
				case reflection::IAttribute::NO_UI:
					attrs.no_ui = true;
					break;
				case reflection::IAttribute::COLOR:
					attrs.is_color = true;
					break;
				case reflection::IAttribute::MULTILINE:
					attrs.is_multiline = true;
					break;
				case reflection::IAttribute::MIN:
					attrs.min = ((reflection::MinAttribute&)*attr).min;
					break;
				case reflection::IAttribute::CLAMP:
					attrs.min = ((reflection::ClampAttribute&)*attr).min;
					attrs.max = ((reflection::ClampAttribute&)*attr).max;
					break;
				case reflection::IAttribute::RESOURCE:
					attrs.resource_type = ((reflection::ResourceAttribute&)*attr).resource_type;
					break;
				default: break;
			}
		}
		return attrs;
	}

	template <typename T>
	void dynamicProperty(const ComponentUID& cmp, const reflection::DynamicProperties& prop, u32 prop_index) {
		struct Prop : reflection::Property<T> {
			Prop(IAllocator& allocator) : reflection::Property<T>(allocator) {}

			T get(ComponentUID cmp, u32 array_index) const override {
				return reflection::get<T>(prop->getValue(cmp, array_index, index));
			}

			void set(ComponentUID cmp, u32 array_index, T value) const override {
				reflection::DynamicProperties::Value v;
				reflection::set<T>(v, value);
				prop->set(cmp, array_index, index, v);
			}

			bool isReadonly() const override { return false; }

			const reflection::DynamicProperties* prop;
			ComponentUID cmp;
			int index;
		} p(m_app.getAllocator());

		p.name = prop.getName(cmp, m_index, prop_index);
		p.prop = &prop;
		p.index =  prop_index;
		visit(p);
	}

	void visit(const reflection::DynamicProperties& prop) override {
		ComponentUID cmp = getComponent();;
		for (u32 i = 0, c = prop.getCount(cmp, m_index); i < c; ++i) {
			const reflection::DynamicProperties::Type type = prop.getType(cmp, m_index, i);
			switch(type) {
				case reflection::DynamicProperties::NONE: break;
				case reflection::DynamicProperties::FLOAT: dynamicProperty<float>(cmp, prop, i); break;
				case reflection::DynamicProperties::BOOLEAN: dynamicProperty<bool>(cmp, prop, i); break;
				case reflection::DynamicProperties::ENTITY: dynamicProperty<EntityPtr>(cmp, prop, i); break;
				case reflection::DynamicProperties::I32: dynamicProperty<i32>(cmp, prop, i); break;
				case reflection::DynamicProperties::STRING: dynamicProperty<const char*>(cmp, prop, i); break;
				case reflection::DynamicProperties::COLOR: {
					struct Prop : reflection::Property<Vec3> {
						Prop(IAllocator& allocator) : Property<Vec3>(allocator) {}

						Vec3 get(ComponentUID cmp, u32 array_index) const override {
							return reflection::get<Vec3>(prop->getValue(cmp, array_index, index));
						}
						void set(ComponentUID cmp, u32 array_index, Vec3 value) const override {
							reflection::DynamicProperties::Value v;
							reflection::set(v, value);
							prop->set(cmp, array_index, index, v);
						}

						bool isReadonly() const override { return false; }

						const reflection::DynamicProperties* prop;
						ComponentUID cmp;
						int index;
						reflection::ColorAttribute attr;
					} p(m_app.getAllocator());

					p.name = prop.getName(cmp, m_index, i);
					p.prop = &prop;
					p.index =  i;
					p.attributes.push(&p.attr);
					visit(p);
					break;
				}
				case reflection::DynamicProperties::RESOURCE: {
					struct Prop : reflection::Property<Path> {
						Prop(IAllocator& allocator) : Property<Path>(allocator) {}

						Path get(ComponentUID cmp, u32 array_index) const override {
							return Path(reflection::get<const char*>(prop->getValue(cmp, array_index, index)));
						}

						void set(ComponentUID cmp, u32 array_index, Path value) const override {
							reflection::DynamicProperties::Value v;
							reflection::set(v, value.c_str());
							prop->set(cmp, array_index, index, v);
						}

						bool isReadonly() const override { return false; }

						const reflection::DynamicProperties* prop;
						ComponentUID cmp;
						int index;
						reflection::ResourceAttribute attr;
					} p(m_app.getAllocator());

					p.attr = prop.getResourceAttribute(cmp, m_index, i);
					p.name = prop.getName(cmp, m_index, i);
					p.prop = &prop;
					p.index =  i;
					p.attributes.push(&p.attr);
					visit(p);
					break;
				}
			}
		}
	}

	void visit(const reflection::Property<float>& prop) override
	{
		if (prop.isReadonly()) ImGuiEx::PushReadOnly();
		Attributes attrs = getAttributes(prop);
		ComponentUID cmp = getComponent();
		float f = prop.get(cmp, m_index);

		if (attrs.is_radians) f = radiansToDegrees(f);
		ImGuiEx::Label(prop.name);
		ImGui::PushID(prop.name);
		if (ImGui::DragFloat("##v", &f, 1, attrs.min, attrs.max))
		{
			f = clamp(f, attrs.min, attrs.max);
			if (attrs.is_radians) f = degreesToRadians(f);
			m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, f);
		}
		ImGui::PopID();
		if (prop.isReadonly()) ImGuiEx::PopReadOnly();
	}

	void visit(const reflection::Property<int>& prop) override
	{
		ComponentUID cmp = getComponent();
		int value = prop.get(cmp, m_index);
		auto* enum_attr = (reflection::EnumAttribute*)reflection::getAttribute(prop, reflection::IAttribute::ENUM);

		if (enum_attr) {
			if (m_entities.size() > 1) {
				ImGuiEx::Label(prop.name);
				ImGui::TextUnformatted("Multi-object editing not supported.");
				return;
			}

			if (prop.isReadonly()) ImGuiEx::PushReadOnly();
			const int count = enum_attr->count(cmp);

			const char* preview = count ? enum_attr->name(cmp, value) : "";
			ImGuiEx::Label(prop.name);
			ImGui::PushID(prop.name);
			if (ImGui::BeginCombo("##v", preview)) {
				for (int i = 0; i < count; ++i) {
					const char* val_name = enum_attr->name(cmp, i);
					if (ImGui::Selectable(val_name)) {
						value = i;
						const EntityRef e = (EntityRef)cmp.entity;
						m_editor.setProperty(cmp.type, m_array, m_index, prop.name, Span(&e, 1), value);
					}
				}
				ImGui::EndCombo();
			}
			ImGui::PopID();
			if (prop.isReadonly()) ImGuiEx::PopReadOnly();
			return;
		}

		if (prop.isReadonly()) ImGuiEx::PushReadOnly();
		ImGui::PushID(prop.name);
		ImGuiEx::Label(prop.name);
		if (ImGui::InputInt("##v", &value))
		{
			m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, value);
		}
		ImGui::PopID();
		if (prop.isReadonly()) ImGuiEx::PopReadOnly();
	}


	void visit(const reflection::Property<u32>& prop) override
	{
		ComponentUID cmp = getComponent();
		u32 value = prop.get(cmp, m_index);
		
		auto* enum_attr = (reflection::EnumAttribute*)reflection::getAttribute(prop, reflection::IAttribute::ENUM);
		if (enum_attr) {
			if (m_entities.size() > 1) {
				ImGuiEx::Label(prop.name);
				ImGui::TextUnformatted("Multi-object editing not supported.");
				return;
			}

			const int count = enum_attr->count(cmp);

			if (prop.isReadonly()) ImGuiEx::PushReadOnly();
			const char* preview = enum_attr->name(cmp, value);
			ImGuiEx::Label(prop.name);
			ImGui::PushID(prop.name);
			if (ImGui::BeginCombo("##v", preview)) {
				for (int i = 0; i < count; ++i) {
					const char* val_name = enum_attr->name(cmp, i);
					if (ImGui::Selectable(val_name)) {
						value = i;
						const EntityRef e = (EntityRef)cmp.entity;
						m_editor.setProperty(cmp.type, m_array, m_index, prop.name, Span(&e, 1), value);
					}
				}
				ImGui::EndCombo();
			}
			ImGui::PopID();
			if (prop.isReadonly()) ImGuiEx::PopReadOnly();
			return;
		}

		if (prop.isReadonly()) ImGuiEx::PushReadOnly();
		ImGuiEx::Label(prop.name);
		ImGui::PushID(prop.name);
		if (ImGui::InputScalar("##v", ImGuiDataType_U32, &value))
		{
			m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, value);
		}
		ImGui::PopID();
		if (prop.isReadonly()) ImGuiEx::PopReadOnly();
	}


	void visit(const reflection::Property<EntityPtr>& prop) override
	{
		if (prop.isReadonly()) ImGuiEx::PushReadOnly();
		ComponentUID cmp = getComponent();
		EntityPtr entity = prop.get(cmp, m_index);

		char buf[128];
		getEntityListDisplayName(m_app, *m_editor.getWorld(), Span(buf), entity);
		ImGui::PushID(prop.name);
		
		ImGuiEx::Label(prop.name);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
		
		if (!entity.isValid()) {
			copyString(buf, "No entity (click to set)");
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
		}
		
		const float icons_w = ImGui::CalcTextSize(ICON_FA_BULLSEYE ICON_FA_TRASH).x;
		bool recently_opened_popup = false;
		if (ImGui::Button(buf, ImVec2(entity.isValid() ? -icons_w : -1.f, 0))) {
			ImGui::OpenPopup("popup");
			recently_opened_popup = true;
		}
		if (!entity.isValid()) {
			ImGui::PopStyleColor();
		}

		if (ImGui::BeginDragDropTarget()) {
			if (auto* payload = ImGui::AcceptDragDropPayload("entity")) {
				EntityRef dropped_entity = *(EntityRef*)payload->Data;
				m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, dropped_entity);
			}
		}

		if (entity.isValid()) {
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_BULLSEYE, "Go to")) {
				m_grid.m_deferred_select = entity;
			}
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_TRASH, "Clear")) {
				m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, INVALID_ENTITY);
			}
		}
		ImGui::PopStyleVar();


		World& world = *m_editor.getWorld();
		if (ImGuiEx::BeginResizablePopup("popup", ImVec2(200, 300)))
		{
			static char entity_filter[32] = {};
			const float w = ImGui::CalcTextSize(ICON_FA_TIMES).x + ImGui::GetStyle().ItemSpacing.x * 2;
			ImGui::SetNextItemWidth(-w);
			if (recently_opened_popup) ImGui::SetKeyboardFocusHere();
			ImGui::InputTextWithHint("##filter", "Filter", entity_filter, sizeof(entity_filter), ImGuiInputTextFlags_AutoSelectAll);
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
				entity_filter[0] = '\0';
			}
			
			if (ImGui::BeginChild("list", ImVec2(0, ImGui::GetContentRegionAvail().y))) {
				for (EntityPtr i = world.getFirstEntity(); i.isValid(); i = world.getNextEntity((EntityRef)i))
				{
					getEntityListDisplayName(m_app, world, Span(buf), i);
					bool show = entity_filter[0] == '\0' || stristr(buf, entity_filter) != 0;
					if (show && ImGui::Selectable(buf))
					{
						m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, i);
					}
				}
			}
			ImGui::EndChild();
			ImGui::EndPopup();
		}
		ImGui::PopID();
		if (prop.isReadonly()) ImGuiEx::PopReadOnly();
	}


	void visit(const reflection::Property<Vec2>& prop) override
	{
		if (prop.isReadonly()) ImGuiEx::PushReadOnly();
		ComponentUID cmp = getComponent();
		Vec2 value = prop.get(cmp, m_index);
		Attributes attrs = getAttributes(prop);
		if (attrs.no_ui) return;

		if (attrs.is_radians) value = radiansToDegrees(value);
		ImGuiEx::Label(prop.name);
		ImGui::PushID(prop.name);
		if (ImGui::DragFloat2("##v", &value.x))
		{
			if (attrs.is_radians) value = degreesToRadians(value);
			m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, value);
		}
		ImGui::PopID();
		if (prop.isReadonly()) ImGuiEx::PopReadOnly();
	}


	void visit(const reflection::Property<Vec3>& prop) override
	{
		if (prop.isReadonly()) ImGuiEx::PushReadOnly();
		Attributes attrs = getAttributes(prop);
		ComponentUID cmp = getComponent();
		Vec3 value = prop.get(cmp, m_index);

		ImGuiEx::Label(prop.name);
		ImGui::PushID(prop.name);
		if (attrs.is_color)
		{
			if (ImGui::ColorEdit3("##v", &value.x))
			{
				m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, value);
			}
		}
		else
		{
			if (attrs.is_radians) value = radiansToDegrees(value);
			if (ImGui::DragFloat3("##v", &value.x, 1, attrs.min, attrs.max))
			{
				if (attrs.is_radians) value = degreesToRadians(value);
				m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, value);
			}
		}
		ImGui::PopID();
		if (prop.isReadonly()) ImGuiEx::PopReadOnly();
	}


	void visit(const reflection::Property<IVec3>& prop) override
	{
		if (prop.isReadonly()) ImGuiEx::PushReadOnly();
		ComponentUID cmp = getComponent();
		IVec3 value = prop.get(cmp, m_index);
		
		ImGuiEx::Label(prop.name);
		ImGui::PushID(prop.name);
		if (ImGui::DragInt3("##v", &value.x)) {
			m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, value);
		}
		ImGui::PopID();
		if (prop.isReadonly()) ImGuiEx::PopReadOnly();
	}


	void visit(const reflection::Property<Vec4>& prop) override
	{
		if (prop.isReadonly()) ImGuiEx::PushReadOnly();
		Attributes attrs = getAttributes(prop);
		ComponentUID cmp = getComponent();
		Vec4 value = prop.get(cmp, m_index);

		ImGuiEx::Label(prop.name);
		ImGui::PushID(prop.name);
		if (attrs.is_color)
		{
			if (ImGui::ColorEdit4("##v", &value.x))
			{
				m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, value);
			}
		}
		else
		{
			if (ImGui::DragFloat4("##v", &value.x))
			{
				m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, value);
			}
		}
		ImGui::PopID();
		if (prop.isReadonly()) ImGuiEx::PopReadOnly();
	}


	void visit(const reflection::Property<bool>& prop) override
	{
		if (prop.isReadonly()) ImGuiEx::PushReadOnly();
		if (equalIStrings(prop.name, "enabled") && m_index == -1 && m_entities.size() == 1) return;
		ComponentUID cmp = getComponent();
		bool value = prop.get(cmp, m_index);

		ImGuiEx::Label(prop.name);
		ImGui::PushID(prop.name);
		if (ImGui::Checkbox("##v", &value))
		{
			m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, value);
		}
		ImGui::PopID();
		if (prop.isReadonly()) ImGuiEx::PopReadOnly();
	}


	void visit(const reflection::Property<Path>& prop) override
	{
		ComponentUID cmp = getComponent();
		const Path p = prop.get(cmp, m_index);
		char tmp[LUMIX_MAX_PATH];
		copyString(tmp, p.c_str());

		Attributes attrs = getAttributes(prop);
		if (attrs.no_ui) return;

		if (prop.isReadonly()) ImGuiEx::PushReadOnly();
		ImGuiEx::Label(prop.name);
		ImGui::PushID(prop.name);
		if (attrs.resource_type.isValid())
		{
			if (m_app.getAssetBrowser().resourceInput(prop.name, Span(tmp), attrs.resource_type))
			{
				m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, Path(tmp));
			}
		}
		else
		{
			if (ImGui::InputText("##v", tmp, sizeof(tmp)))
			{
				m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, Path(tmp));
			}
		}
		ImGui::PopID();
		if (prop.isReadonly()) ImGuiEx::PopReadOnly();
	}


	void visit(const reflection::Property<const char*>& prop) override
	{
		ComponentUID cmp = getComponent();
		const Attributes attrs = getAttributes(prop);
		
		char tmp[1024];
		copyString(tmp, prop.get(cmp, m_index));

		if (prop.isReadonly()) ImGuiEx::PushReadOnly();
		ImGuiEx::Label(prop.name);
		ImGui::PushID(prop.name);
		
		auto* enum_attr = (reflection::StringEnumAttribute*)reflection::getAttribute(prop, reflection::IAttribute::STRING_ENUM);
		if (enum_attr) {
			if (m_entities.size() > 1) {
				ImGui::TextUnformatted("Multi-object editing not supported.");
				ImGui::PopID();
				if (prop.isReadonly()) ImGuiEx::PopReadOnly();
				return;
			}

			const int count = enum_attr->count(cmp);

			if (ImGui::BeginCombo("##v", tmp)) {
				for (int i = 0; i < count; ++i) {
					const char* val_name = enum_attr->name(cmp, i);
					if (ImGui::Selectable(val_name)) {
						m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, val_name);
					}
				}
				ImGui::EndCombo();
			}
		}
		else if(attrs.is_multiline) {
			if (ImGui::InputTextMultiline("##v", tmp, sizeof(tmp))) {
				m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, tmp);
			}
		}
		else {
			if (ImGui::InputText("##v", tmp, sizeof(tmp))) {
				m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, tmp);
			}
		}
		ImGui::PopID();
		if (prop.isReadonly()) ImGuiEx::PopReadOnly();
	}

	void visit(const reflection::BlobProperty& prop) override {}

	void visit(const reflection::ArrayProperty& prop) override
	{
		ImGui::Unindent();
		bool is_root_open = ImGui::TreeNodeEx(prop.name, ImGuiTreeNodeFlags_AllowItemOverlap);
		if (m_entities.size() > 1)
		{
			ImGui::Text("Multi-object editing not supported.");
			if (is_root_open) ImGui::TreePop();
			ImGui::Indent();
			return;
		}

		ImGui::PushID(prop.name);
		ComponentUID cmp = getComponent();
		int count = prop.getCount(cmp);
		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(ICON_FA_PLUS).x);
		if (ImGuiEx::IconButton(ICON_FA_PLUS, "Add item"))
		{
			m_editor.addArrayPropertyItem(cmp, prop.name);
			count = prop.getCount(cmp);
		}
		if (!is_root_open)
		{
			ImGui::PopID();
			ImGui::Indent();
			return;
		}

		for (int i = 0; i < count; ++i)
		{
			char tmp[10];
			toCString(i, Span(tmp));
			ImGui::PushID(i);
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap;
			bool is_open = ImGui::TreeNodeEx(tmp, flags);
			ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(ICON_FA_TRASH).x);
			if (ImGuiEx::IconButton(ICON_FA_TRASH, "Remove"))
			{
				m_editor.removeArrayPropertyItem(cmp, i, prop.name);
				--i;
				count = prop.getCount(cmp);
				if(is_open) ImGui::TreePop();
				ImGui::PopID();
				continue;
			}

			if (is_open)
			{
				GridUIVisitor v(m_app, i, m_entities, m_cmp_type, m_editor);
				v.m_array = prop.name;
				prop.visitChildren(v);
				ImGui::TreePop();
			}

			ImGui::PopID();
		}
		ImGui::PopID();
		ImGui::TreePop();
		ImGui::Indent();
	}


	const char* m_array = "";
	StudioApp& m_app;
	WorldEditor& m_editor;
	ComponentType m_cmp_type;
	const Array<EntityRef>& m_entities;
	int m_index;
	PropertyGrid& m_grid;
};


static bool componentTreeNode(StudioApp& app, WorldEditor& editor, ComponentType cmp_type, const EntityRef* entities, int entities_count)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap;
	ImGui::Separator();
	const char* cmp_type_name = app.getComponentTypeName(cmp_type);
	const char* icon = app.getComponentIcon(cmp_type);
	ImGui::PushFont(app.getBoldFont());
	bool is_open;
	bool enabled = true;
	IScene* scene = editor.getWorld()->getScene(cmp_type);
	if (entities_count == 1 && reflection::getPropertyValue(*scene, entities[0], cmp_type, "Enabled", enabled)) {
		is_open = ImGui::TreeNodeEx((void*)(uintptr)cmp_type.index, flags, "%s", "");
		ImGui::SameLine();
		ComponentUID cmp;
		cmp.type = cmp_type;
		cmp.entity = entities[0];
		cmp.scene = editor.getWorld()->getScene(cmp_type);
		if(ImGui::Checkbox(StaticString<256>(icon, cmp_type_name), &enabled))
		{
			editor.setProperty(cmp_type, "", -1, "Enabled", Span(entities, entities_count), enabled);
		}
	}
	else
	{ 
		is_open = ImGui::TreeNodeEx((void*)(uintptr)cmp_type.index, flags, "%s%s", icon, cmp_type_name);
	}
	ImGui::PopFont();
	return is_open;
}


void PropertyGrid::showComponentProperties(const Array<EntityRef>& entities, ComponentType cmp_type, WorldEditor& editor)
{
	bool is_open = componentTreeNode(m_app, editor, cmp_type, &entities[0], entities.size());
	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(ICON_FA_ELLIPSIS_V).x);
	if (ImGuiEx::IconButton(ICON_FA_ELLIPSIS_V, "Context menu"))
	{
		ImGui::OpenPopup("ctx");
	}
	if (ImGui::BeginPopup("ctx")) {
		if (ImGui::Selectable("Remove component")) {
			editor.destroyComponent(entities, cmp_type);
			ImGui::EndPopup();
			if (is_open) ImGui::TreePop();
			return;
		}
		ImGui::EndPopup();
	}

	if (!is_open) return;

	const reflection::ComponentBase* component = reflection::getComponent(cmp_type);
	GridUIVisitor visitor(m_app, -1, entities, cmp_type, editor);
	if (component) component->visit(visitor);

	for (IPlugin* i : m_plugins) {
		i->onGUI(*this, entities, cmp_type, editor);
	}
	ImGui::TreePop();
}


void PropertyGrid::showCoreProperties(const Array<EntityRef>& entities, WorldEditor& editor) const
{
	char name[World::ENTITY_NAME_MAX_LENGTH];
	World& world = *editor.getWorld();
	const char* entity_name = world.getEntityName(entities[0]);
	copyString(name, entity_name);
	ImGui::SetNextItemWidth(-1);
	if (ImGui::InputTextWithHint("##name", "Name", name, sizeof(name), ImGuiInputTextFlags_AutoSelectAll)) editor.setEntityName(entities[0], name);
	ImGui::PushFont(m_app.getBoldFont());
	if (!ImGui::TreeNodeEx("General", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PopFont();
		return;
	}
	ImGui::PopFont();
	if (entities.size() == 1)
	{
		PrefabSystem& prefab_system = editor.getPrefabSystem();
		PrefabResource* prefab = prefab_system.getPrefabResource(entities[0]);
		if (prefab)
		{
			ImGuiEx::Label("Prefab");
			ImGui::TextUnformatted(prefab->getPath().c_str());
			if (ImGui::Button(ICON_FA_SAVE "Save prefab"))
			{
				prefab_system.savePrefab(entities[0], prefab->getPath());
			}
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_UNLINK "Break prefab"))
			{
				prefab_system.breakPrefab(entities[0]);
			}
		}

		ImGuiEx::Label("ID");
		ImGui::Text("%d", entities[0].index);
		EntityPtr parent = world.getParent(entities[0]);
		if (parent.isValid())
		{
			getEntityListDisplayName(m_app, world, Span(name), parent);
			ImGuiEx::Label("Parent");
			ImGui::Text("%s", name);

			if (!world.hasComponent(entities[0], GUI_RECT_TYPE) || world.hasComponent(entities[0], GUI_CANVAS_TYPE)) {
				Transform tr = world.getLocalTransform(entities[0]);
				DVec3 old_pos = tr.pos;
				ImGuiEx::Label("Local position");
				if (ImGui::DragScalarN("##lcl_pos", ImGuiDataType_Double, &tr.pos.x, 3, 1.f))
				{
					WorldEditor::Coordinate coord = WorldEditor::Coordinate::NONE;
					if (tr.pos.x != old_pos.x) coord = WorldEditor::Coordinate::X;
					if (tr.pos.y != old_pos.y) coord = WorldEditor::Coordinate::Y;
					if (tr.pos.z != old_pos.z) coord = WorldEditor::Coordinate::Z;
					if (coord != WorldEditor::Coordinate::NONE)
					{
						editor.setEntitiesLocalCoordinate(&entities[0], entities.size(), (&tr.pos.x)[(int)coord], coord);
					}
				}
			}
		}
	}
	else
	{
		ImGuiEx::Label("ID");
		ImGui::Text("%s", "Multiple objects");
		ImGuiEx::Label("Name");
		ImGui::Text("%s", "Multi-object editing not supported.");
	}


	if (!world.hasComponent(entities[0], GUI_RECT_TYPE) || world.hasComponent(entities[0], GUI_CANVAS_TYPE)) {
		DVec3 pos = world.getPosition(entities[0]);
		DVec3 old_pos = pos;
		ImGuiEx::Label("Position");
		if (ImGui::DragScalarN("##pos", ImGuiDataType_Double, &pos.x, 3, 1.f, 0, 0, "%.3f"))
		{
			WorldEditor::Coordinate coord = WorldEditor::Coordinate::NONE;
			if (pos.x != old_pos.x) coord = WorldEditor::Coordinate::X;
			if (pos.y != old_pos.y) coord = WorldEditor::Coordinate::Y;
			if (pos.z != old_pos.z) coord = WorldEditor::Coordinate::Z;
			if (coord != WorldEditor::Coordinate::NONE)
			{
				editor.setEntitiesCoordinate(&entities[0], entities.size(), (&pos.x)[(int)coord], coord);
			}
		}

		ImGuiEx::Label("Rotation");
		
		Quat rot = world.getRotation(entities[0]);
		const Vec3 old_euler = rot.toEuler();
		Vec3 euler = old_euler;
		if (ImGuiEx::InputRotation("##rot", &euler.x)) {
			Array<Quat> rots(editor.getAllocator());
			for (EntityRef entity : entities) {
				Vec3 tmp = world.getRotation(entity).toEuler();
			
				if (fabs(euler.x - old_euler.x) > 0.0001f) tmp.x = euler.x;
				if (fabs(euler.y - old_euler.y) > 0.0001f) tmp.y = euler.y;
				if (fabs(euler.z - old_euler.z) > 0.0001f) tmp.z = euler.z;
				rots.emplace().fromEuler(tmp);
			}
			editor.setEntitiesRotations(&entities[0], &rots[0], entities.size());
		}

		Vec3 scale = world.getScale(entities[0]);
		ImGuiEx::Label("Scale");
		if (ImGui::DragFloat3("##scale", &scale.x, 0.1f, 0, FLT_MAX))
		{
			editor.setEntitiesScale(&entities[0], entities.size(), scale);
		}
	}
	ImGui::TreePop();
}


static void showAddComponentNode(const StudioApp::AddCmpTreeNode* node, const char* filter, EntityPtr parent, WorldEditor& editor)
{
	if (!node) return;

	if (filter[0])
	{
		if (!node->plugin) showAddComponentNode(node->child, filter, parent, editor);
		else if (stristr(node->plugin->getLabel(), filter)) node->plugin->onGUI(false, true, parent, editor);
		showAddComponentNode(node->next, filter, parent, editor);
		return;
	}

	if (node->plugin)
	{
		node->plugin->onGUI(false, false, parent, editor);
		showAddComponentNode(node->next, filter, parent, editor);
		return;
	}

	const char* last = reverseFind(node->label, nullptr, '/');
	if (ImGui::BeginMenu(last ? last + 1 : node->label))
	{
		showAddComponentNode(node->child, filter, parent, editor);
		ImGui::EndMenu();
	}
	showAddComponentNode(node->next, filter, parent, editor);
}

void PropertyGrid::onSettingsLoaded() { m_is_open = m_app.getSettings().m_is_properties_open; }
void PropertyGrid::onBeforeSettingsSaved() { m_app.getSettings().m_is_properties_open  = m_is_open; }

void PropertyGrid::onWindowGUI()
{
	for (IPlugin* i : m_plugins) {
		i->update();
	}

	if (!m_is_open) return;

	WorldEditor& editor = m_app.getWorldEditor();
	const Array<EntityRef>& ents = editor.getSelectedEntities();
	if (ImGui::Begin(ICON_FA_INFO_CIRCLE "Inspector##inspector", &m_is_open) && !ents.empty()) {
		showCoreProperties(ents, editor);

		World& world = *editor.getWorld();
		for (ComponentUID cmp = world.getFirstComponent(ents[0]); cmp.isValid(); cmp = world.getNextComponent(cmp)) {
			showComponentProperties(ents, cmp.type, editor);
		}

		ImGui::Separator();
		const float x = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(ICON_FA_PLUS "Add component").x - ImGui::GetStyle().FramePadding.x * 2) * 0.5f;
		ImGui::SetCursorPosX(x);
		if (ImGui::Button(ICON_FA_PLUS "Add component")) ImGui::OpenPopup("AddComponentPopup");

		if (ImGui::BeginPopup("AddComponentPopup", ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGuiEx::filter("Filter", m_component_filter, sizeof(m_component_filter), 200, ImGui::IsWindowAppearing());
			showAddComponentNode(m_app.getAddComponentTreeRoot().child, m_component_filter, INVALID_ENTITY, editor);
			ImGui::EndPopup();
		}
	}
	ImGui::End();

	if (m_deferred_select.isValid()) {
		const EntityRef e = (EntityRef)m_deferred_select;
		editor.selectEntities(Span(&e, 1u), false);
		m_deferred_select = INVALID_ENTITY;
	}
}


} // namespace Lumix
