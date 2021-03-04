#include <imgui/imgui.h>

#include "property_grid.h"
#include "asset_browser.h"
#include "editor/prefab_system.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/crt.h"
#include "engine/plugin.h"
#include "engine/math.h"
#include "engine/prefab.h"
#include "engine/reflection.h"
#include "engine/resource.h"
#include "engine/stream.h"
#include "engine/universe.h"
#include "engine/math.h"
#include "utils.h"


namespace Lumix
{

static const ComponentType GUI_RECT_TYPE = reflection::getComponentType("gui_rect");
static const ComponentType GUI_CANVAS_TYPE = reflection::getComponentType("gui_canvas");


PropertyGrid::PropertyGrid(StudioApp& app)
	: m_app(app)
	, m_is_open(true)
	, m_editor(app.getWorldEditor())
	, m_plugins(app.getAllocator())
	, m_deferred_select(INVALID_ENTITY)
{
	m_component_filter[0] = '\0';
}


PropertyGrid::~PropertyGrid()
{
	ASSERT(m_plugins.empty());
}


struct GridUIVisitor final : reflection::IReflPropertyVisitor
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
		first_entity_cmp.scene = m_editor.getUniverse()->getScene(m_cmp_type);
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
	static Attributes getAttributes(const reflection::refl_typed_prop<T>& prop)
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
			}
		}
		return attrs;
	}

	template <typename T>
	void dynamicProperty(const ComponentUID& cmp, const reflection::IDynamicProperties& prop, u32 prop_index) {
		struct : reflection::Property<T> {
			Span<const reflection::IAttribute* const> getAttributes() const override { return {}; }

			T get(ComponentUID cmp, int array_index) const override {
				return reflection::get<T>(prop->getValue(cmp, array_index, index));
			}

			void set(ComponentUID cmp, int array_index, T value) const override {
				reflection::IDynamicProperties::Value v;
				reflection::set<T>(v, value);
				prop->set(cmp, array_index, index, v);
			}

			const reflection::IDynamicProperties* prop;
			ComponentUID cmp;
			int index;
		} p;
		p.name = prop.getName(cmp, m_index, prop_index);
		p.prop = &prop;
		p.index =  prop_index;
		visit(p);
	}
	// TODO refl
	/*
	void visit(const reflection::IDynamicProperties& prop) override {
		ComponentUID cmp = getComponent();;
		for (u32 i = 0, c = prop.getCount(cmp, m_index); i < c; ++i) {
			const reflection::IDynamicProperties::Type type = prop.getType(cmp, m_index, i);
			switch(type) {
				case reflection::IDynamicProperties::FLOAT: dynamicProperty<float>(cmp, prop, i); break;
				case reflection::IDynamicProperties::BOOLEAN: dynamicProperty<bool>(cmp, prop, i); break;
				case reflection::IDynamicProperties::ENTITY: dynamicProperty<EntityPtr>(cmp, prop, i); break;
				case reflection::IDynamicProperties::I32: dynamicProperty<i32>(cmp, prop, i); break;
				case reflection::IDynamicProperties::STRING: dynamicProperty<const char*>(cmp, prop, i); break;
				case reflection::IDynamicProperties::COLOR: {
					struct : reflection::Property<Vec3> {
						Span<const reflection::IAttribute* const> getAttributes() const override {
							return Span((const reflection::IAttribute*const*)attrs, 1);
						}
						
						Vec3 get(ComponentUID cmp, int array_index) const override {
							return reflection::get<Vec3>(prop->getValue(cmp, array_index, index));
						}
						void set(ComponentUID cmp, int array_index, Vec3 value) const override {
							reflection::IDynamicProperties::Value v;
							reflection::set(v, value);
							prop->set(cmp, array_index, index, v);
						}
						const reflection::IDynamicProperties* prop;
						ComponentUID cmp;
						int index;
						reflection::ColorAttribute attr;
						reflection::IAttribute* attrs[1] = { &attr };
					} p;
					p.name = prop.getName(cmp, m_index, i);
					p.prop = &prop;
					p.index =  i;
					visit(p);
					break;
				}
				case reflection::IDynamicProperties::RESOURCE: {
					struct : reflection::Property<Path> {
						Span<const reflection::IAttribute* const> getAttributes() const override {
							return Span((const reflection::IAttribute*const*)attrs, 1);
						}
						
						Path get(ComponentUID cmp, int array_index) const override {
							return Path(reflection::get<const char*>(prop->getValue(cmp, array_index, index)));
						}
						void set(ComponentUID cmp, int array_index, Path value) const override {
							reflection::IDynamicProperties::Value v;
							reflection::set(v, value.c_str());
							prop->set(cmp, array_index, index, v);
						}
						const reflection::IDynamicProperties* prop;
						ComponentUID cmp;
						int index;
						reflection::ResourceAttribute attr;
						reflection::IAttribute* attrs[1] = { &attr };
					} p;
					p.attr = prop.getResourceAttribute(cmp, m_index, i);
					p.name = prop.getName(cmp, m_index, i);
					p.prop = &prop;
					p.index =  i;
					visit(p);
					break;
				}
				default: ASSERT(false); break;
			}
		}
	}
	*/

	void visit(const reflection::refl_typed_prop<float>& prop) override
	{
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
	}

	void visit(const reflection::refl_typed_prop<int>& prop) override
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
			return;
		}

		ImGui::PushID(prop.name);
		ImGuiEx::Label(prop.name);
		if (ImGui::InputInt("##v", &value))
		{
			m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, value);
		}
		ImGui::PopID();
	}


	void visit(const reflection::refl_typed_prop<u32>& prop) override
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
			return;
		}

		ImGuiEx::Label(prop.name);
		ImGui::PushID(prop.name);
		if (ImGui::InputScalar("##v", ImGuiDataType_U32, &value))
		{
			m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, value);
		}
		ImGui::PopID();
	}


	void visit(const reflection::refl_typed_prop<EntityPtr>& prop) override
	{
		ComponentUID cmp = getComponent();
		EntityPtr entity = prop.get(cmp, m_index);

		char buf[128];
		getEntityListDisplayName(m_app, m_editor, Span(buf), entity);
		ImGui::PushID(prop.name);
		
		ImGuiEx::Label(prop.name);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
		
		if (!entity.isValid()) {
			copyString(buf, "No entity (click to set)");
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
		}
		
		if (ImGui::Button(buf, ImVec2(entity.isValid() ? -32.f : -1.f, 0))) {
			ImGui::OpenPopup("popup");
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


		Universe& universe = *m_editor.getUniverse();
		if (ImGuiEx::BeginResizablePopup("popup", ImVec2(200, 300)))
		{
			static char entity_filter[32] = {};
			const float w = ImGui::CalcTextSize(ICON_FA_TIMES).x + ImGui::GetStyle().ItemSpacing.x * 2;
			ImGui::SetNextItemWidth(-w);
			ImGui::InputTextWithHint("##filter", "Filter", entity_filter, sizeof(entity_filter));
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
				entity_filter[0] = '\0';
			}
			
			if (ImGui::BeginChild("list", ImVec2(0, ImGui::GetContentRegionAvail().y))) {
				for (EntityPtr i = universe.getFirstEntity(); i.isValid(); i = universe.getNextEntity((EntityRef)i))
				{
					getEntityListDisplayName(m_app, m_editor, Span(buf), i);
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
	}


	void visit(const reflection::refl_typed_prop<Vec2>& prop) override
	{
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
	}


	void visit(const reflection::refl_typed_prop<Vec3>& prop) override
	{
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
	}


	void visit(const reflection::refl_typed_prop<IVec3>& prop) override
	{
		ComponentUID cmp = getComponent();
		IVec3 value = prop.get(cmp, m_index);
		
		ImGuiEx::Label(prop.name);
		ImGui::PushID(prop.name);
		if (ImGui::DragInt3("##v", &value.x)) {
			m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, value);
		}
		ImGui::PopID();
	}


	void visit(const reflection::refl_typed_prop<Vec4>& prop) override
	{
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
	}


	void visit(const reflection::refl_typed_prop<bool>& prop) override
	{
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
	}


	void visit(const reflection::refl_typed_prop<Path>& prop) override
	{
		ComponentUID cmp = getComponent();
		const Path p = prop.get(cmp, m_index);
		char tmp[LUMIX_MAX_PATH];
		copyString(tmp, p.c_str());

		Attributes attrs = getAttributes(prop);
		if (attrs.no_ui) return;

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
	}


	void visit(const reflection::refl_typed_prop<const char*>& prop) override
	{
		ComponentUID cmp = getComponent();
		const Attributes attrs = getAttributes(prop);
		
		char tmp[1024];
		copyString(tmp, prop.get(cmp, m_index));

		ImGuiEx::Label(prop.name);
		ImGui::PushID(prop.name);
		
		auto* enum_attr = (reflection::StringEnumAttribute*)reflection::getAttribute(prop, reflection::IAttribute::STRING_ENUM);
		if (enum_attr) {
			if (m_entities.size() > 1) {
				ImGui::TextUnformatted("Multi-object editing not supported.");
				ImGui::PopID();
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
	}

	// TODO refl
	//void visit(const reflection::IBlobProperty& prop) override {}


	void visit(const reflection::reflarrayprop& prop) override
	{
		ImGui::Unindent();
		bool is_open = ImGui::TreeNodeEx(prop.name, ImGuiTreeNodeFlags_AllowItemOverlap);
		if (m_entities.size() > 1)
		{
			ImGui::Text("Multi-object editing not supported.");
			if (is_open) ImGui::TreePop();
			ImGui::Indent();
			return;
		}

		ImGui::PushID(prop.name);
		ComponentUID cmp = getComponent();
		int count = prop.getCount(cmp);
		const ImGuiStyle& style = ImGui::GetStyle();
		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(ICON_FA_PLUS).x);
		if (ImGuiEx::IconButton(ICON_FA_PLUS, "Add item"))
		{
			m_editor.addArrayPropertyItem(cmp, prop.name);
			count = prop.getCount(cmp);
		}
		if (!is_open)
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
	IScene* scene = editor.getUniverse()->getScene(cmp_type);
	if (entities_count == 1 && reflection::getPropertyValue(*scene, entities[0], cmp_type, "Enabled", Ref(enabled))) {
		is_open = ImGui::TreeNodeEx((void*)(uintptr)cmp_type.index, flags, "%s", "");
		ImGui::SameLine();
		ComponentUID cmp;
		cmp.type = cmp_type;
		cmp.entity = entities[0];
		cmp.scene = editor.getUniverse()->getScene(cmp_type);
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


void PropertyGrid::showComponentProperties(const Array<EntityRef>& entities, ComponentType cmp_type)
{
	bool is_open = componentTreeNode(m_app, m_editor, cmp_type, &entities[0], entities.size());
	ImGuiStyle& style = ImGui::GetStyle();
	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(ICON_FA_ELLIPSIS_V).x);
	if (ImGuiEx::IconButton(ICON_FA_ELLIPSIS_V, "Context menu"))
	{
		ImGui::OpenPopup("ctx");
	}
	if (ImGui::BeginPopup("ctx")) {
		if (ImGui::Selectable("Remove component")) {
			m_editor.destroyComponent(entities, cmp_type);
			ImGui::EndPopup();
			if (is_open) ImGui::TreePop();
			return;
		}
		ImGui::EndPopup();
	}

	if (!is_open) return;

	const reflection::reflcmp* component = reflection::getReflComponent(cmp_type);
	GridUIVisitor visitor(m_app, -1, entities, cmp_type, m_editor);
	if (component) component->visit(visitor);

	if (entities.size() == 1)
	{
		ComponentUID cmp;
		cmp.type = cmp_type;
		cmp.scene = m_editor.getUniverse()->getScene(cmp.type);
		cmp.entity = entities[0];
		for (auto* i : m_plugins)
		{
			i->onGUI(*this, cmp);
		}
	}
	ImGui::TreePop();
}


void PropertyGrid::showCoreProperties(const Array<EntityRef>& entities) const
{
	char name[Universe::ENTITY_NAME_MAX_LENGTH];
	Universe& universe = *m_editor.getUniverse();
	const char* tmp = universe.getEntityName(entities[0]);
	copyString(name, tmp);
	ImGui::SetNextItemWidth(-1);
	if (ImGui::InputTextWithHint("##name", "Name", name, sizeof(name))) m_editor.setEntityName(entities[0], name);
	ImGui::PushFont(m_app.getBoldFont());
	if (!ImGui::TreeNodeEx("General", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PopFont();
		return;
	}
	ImGui::PopFont();
	if (entities.size() == 1)
	{
		PrefabSystem& prefab_system = m_editor.getPrefabSystem();
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
		EntityPtr parent = universe.getParent(entities[0]);
		if (parent.isValid())
		{
			getEntityListDisplayName(m_app, m_editor, Span(name), parent);
			ImGuiEx::Label("Parent");
			ImGui::Text("%s", name);

			if (!universe.hasComponent(entities[0], GUI_RECT_TYPE) || universe.hasComponent(entities[0], GUI_CANVAS_TYPE)) {
				Transform tr = universe.getLocalTransform(entities[0]);
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
						m_editor.setEntitiesLocalCoordinate(&entities[0], entities.size(), (&tr.pos.x)[(int)coord], coord);
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


	if (!universe.hasComponent(entities[0], GUI_RECT_TYPE) || universe.hasComponent(entities[0], GUI_CANVAS_TYPE)) {
		DVec3 pos = universe.getPosition(entities[0]);
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
				m_editor.setEntitiesCoordinate(&entities[0], entities.size(), (&pos.x)[(int)coord], coord);
			}
		}

		Quat rot = universe.getRotation(entities[0]);
		Vec3 old_euler = rot.toEuler();
		Vec3 euler = radiansToDegrees(old_euler);
		ImGuiEx::Label("Rotation");
		const float rot_change_speed = ImGui::GetIO().KeyAlt ? 10.f : 1.f; // we won't have precision without this
		if (ImGui::DragFloat3("##rot", &euler.x, rot_change_speed, 0, 0, "%.2f"))
		{
			if (euler.x <= -90.0f || euler.x >= 90.0f) euler.y = 0;
			euler.x = degreesToRadians(clamp(euler.x, -90.0f, 90.0f));
			euler.y = degreesToRadians(fmodf(euler.y + 180, 360.0f) - 180);
			euler.z = degreesToRadians(fmodf(euler.z + 180, 360.0f) - 180);
			rot.fromEuler(euler);
		
			Array<Quat> rots(m_editor.getAllocator());
			for (EntityRef entity : entities)
			{
				Vec3 tmp = universe.getRotation(entity).toEuler();
			
				if (fabs(euler.x - old_euler.x) > 0.0001f) tmp.x = euler.x;
				if (fabs(euler.y - old_euler.y) > 0.0001f) tmp.y = euler.y;
				if (fabs(euler.z - old_euler.z) > 0.0001f) tmp.z = euler.z;
				rots.emplace().fromEuler(tmp);
			}
			m_editor.setEntitiesRotations(&entities[0], &rots[0], entities.size());
		}

		float scale = universe.getScale(entities[0]);
		ImGuiEx::Label("Scale");
		if (ImGui::DragFloat("##scale", &scale, 0.1f, 0, FLT_MAX))
		{
			m_editor.setEntitiesScale(&entities[0], entities.size(), scale);
		}
	}
	ImGui::TreePop();
}


static void showAddComponentNode(const StudioApp::AddCmpTreeNode* node, const char* filter)
{
	if (!node) return;

	if (filter[0])
	{
		if (!node->plugin) showAddComponentNode(node->child, filter);
		else if (stristr(node->plugin->getLabel(), filter)) node->plugin->onGUI(false, true);
		showAddComponentNode(node->next, filter);
		return;
	}

	if (node->plugin)
	{
		node->plugin->onGUI(false, false);
		showAddComponentNode(node->next, filter);
		return;
	}

	const char* last = reverseFind(node->label, nullptr, '/');
	if (ImGui::BeginMenu(last ? last + 1 : node->label))
	{
		showAddComponentNode(node->child, filter);
		ImGui::EndMenu();
	}
	showAddComponentNode(node->next, filter);
}


void PropertyGrid::onGUI()
{
	for (auto* i : m_plugins) {
		i->update();
	}

	if (!m_is_open) return;

	const Array<EntityRef>& ents = m_editor.getSelectedEntities();
	if (ImGui::Begin(ICON_FA_INFO_CIRCLE "Inspector##inspector", &m_is_open) && !ents.empty())
	{
		showCoreProperties(ents);

		Universe& universe = *m_editor.getUniverse();
		for (ComponentUID cmp = universe.getFirstComponent(ents[0]); cmp.isValid();
			 cmp = universe.getNextComponent(cmp))
		{
			showComponentProperties(ents, cmp.type);
		}

		ImGui::Separator();
		const float x = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(ICON_FA_PLUS "Add component").x - ImGui::GetStyle().FramePadding.x * 2) * 0.5f;
		ImGui::SetCursorPosX(x);
		if (ImGui::Button(ICON_FA_PLUS "Add component")) {
			ImGui::OpenPopup("AddComponentPopup");
		}
		
		if (ImGuiEx::BeginResizablePopup("AddComponentPopup", ImVec2(300, 300))) {
			const float w = ImGui::CalcTextSize(ICON_FA_TIMES).x + ImGui::GetStyle().ItemSpacing.x * 2;
			ImGui::SetNextItemWidth(-w);
			ImGui::InputTextWithHint("##filter", "Filter", m_component_filter, sizeof(m_component_filter));
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
				m_component_filter[0] = '\0';
			}

			showAddComponentNode(m_app.getAddComponentTreeRoot().child, m_component_filter);
			ImGui::EndPopup();
		}
	}
	ImGui::End();

	if (m_deferred_select.isValid())
	{
		const EntityRef e = (EntityRef)m_deferred_select;
		m_editor.selectEntities(Span(&e, 1u), false);
		m_deferred_select = INVALID_ENTITY;
	}
}


} // namespace Lumix
