#include <imgui/imgui.h>

#include "core/crt.h"
#include "core/defer.h"
#include "core/math.h"
#include "core/path.h"
#include "core/stream.h"
#include "core/math.h"

#include "property_grid.h"
#include "asset_browser.h"
#include "editor/prefab_system.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/plugin.h"
#include "engine/prefab.h"
#include "engine/reflection.h"
#include "engine/resource.h"
#include "engine/world.h"
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
	m_app.getSettings().registerOption("property_grid_open", &m_is_open);
}


struct GridUIVisitor final : reflection::IPropertyVisitor
{
	GridUIVisitor(StudioApp& app, int index, Span<const EntityRef> entities, ComponentType cmp_type, const TextFilter& filter, WorldEditor& editor)
		: m_entities(entities)
		, m_cmp_type(cmp_type)
		, m_editor(editor)
		, m_index(index)
		, m_grid(app.getPropertyGrid())
		, m_app(app)
		, m_filter(filter)
	{}


	ComponentUID getComponent() const
	{
		ComponentUID first_entity_cmp;
		first_entity_cmp.type = m_cmp_type;
		first_entity_cmp.module = m_editor.getWorld()->getModule(m_cmp_type);
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

	void visit(const reflection::Property<float>& prop) override
	{
		if (!m_filter.pass(prop.name)) return;
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
		if (!m_filter.pass(prop.name)) return;
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
		if (!m_filter.pass(prop.name)) return;
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
		if (!m_filter.pass(prop.name)) return;
		if (prop.isReadonly()) ImGuiEx::PushReadOnly();
		ComponentUID cmp = getComponent();
		EntityPtr entity = prop.get(cmp, m_index);
		ImGuiEx::Label(prop.name);
		if (m_grid.entityInput(prop.name, &entity)) {
			m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, entity);
		}
		if (prop.isReadonly()) ImGuiEx::PopReadOnly();
	}


	void visit(const reflection::Property<Vec2>& prop) override
	{
		if (!m_filter.pass(prop.name)) return;
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
		if (!m_filter.pass(prop.name)) return;
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
		if (!m_filter.pass(prop.name)) return;
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
		if (!m_filter.pass(prop.name)) return;
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
		if (!m_filter.pass(prop.name)) return;
		if (prop.isReadonly()) ImGuiEx::PushReadOnly();
		if (equalIStrings(prop.name, "enabled") && m_index == -1 && m_entities.size() == 1) return;
		ComponentUID cmp = getComponent();
		bool value = prop.get(cmp, m_index);

		ImGuiEx::Label(prop.name);
		ImGui::PushID(prop.name);
		if (ImGui::Checkbox("##v", &value) && !prop.isReadonly())
		{
			m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, value);
		}
		ImGui::PopID();
		if (prop.isReadonly()) ImGuiEx::PopReadOnly();
	}


	void visit(const reflection::Property<Path>& prop) override {
		if (!m_filter.pass(prop.name)) return;
		ComponentUID cmp = getComponent();
		Path path = prop.get(cmp, m_index);

		Attributes attrs = getAttributes(prop);
		if (attrs.no_ui) return;

		if (prop.isReadonly()) ImGuiEx::PushReadOnly();
		ImGuiEx::Label(prop.name);
		ImGui::PushID(prop.name);
		if (attrs.resource_type.isValid()) {
			if (m_app.getAssetBrowser().resourceInput(prop.name, path, attrs.resource_type)) {
				m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, path);
			}
		}
		else {
			if (ImGui::InputText("##v", path.beginUpdate(), path.capacity())) {
				path.endUpdate();
				m_editor.setProperty(m_cmp_type, m_array, m_index, prop.name, m_entities, path);
			}
		}
		ImGui::PopID();
		if (prop.isReadonly()) ImGuiEx::PopReadOnly();
	}


	void visit(const reflection::Property<const char*>& prop) override
	{
		if (!m_filter.pass(prop.name)) return;
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

	void visit(const reflection::BlobProperty& prop) override {
		for (PropertyGrid::IPlugin* plugin : m_grid.m_plugins) {
			plugin->blobGUI(m_grid, m_entities, m_cmp_type, m_index, m_filter, m_editor);
		}
	}

	void visit(const reflection::ArrayProperty& prop) override
	{
		ImGui::Unindent();
		bool is_root_open = ImGui::TreeNodeEx(prop.name, ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_DefaultOpen);
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
				GridUIVisitor v(m_app, i, m_entities, m_cmp_type, m_filter, m_editor);
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

	const TextFilter& m_filter;
	const char* m_array = "";
	StudioApp& m_app;
	WorldEditor& m_editor;
	ComponentType m_cmp_type;
	Span<const EntityRef> m_entities;
	int m_index;
	PropertyGrid& m_grid;
};


bool PropertyGrid::entityInput(const char* name, EntityPtr* entity) {
	ASSERT(entity);
	bool changed = false;
	char buf[128];
	World& world = *m_app.getWorldEditor().getWorld();
	getEntityListDisplayName(m_app, world, Span(buf), *entity);
	ImGui::PushID(name);
	
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
	
	if (!entity->isValid()) {
		copyString(buf, "No entity (click to set)");
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
	}
	
	const float icons_w = ImGui::CalcTextSize(ICON_FA_BULLSEYE ICON_FA_TRASH).x;
	if (ImGui::Button(buf, ImVec2(entity->isValid() ? -icons_w : -1.f, 0))) {
		ImGui::OpenPopup("popup");
	}
	if (!entity->isValid()) {
		ImGui::PopStyleColor();
	}

	if (ImGui::BeginDragDropTarget()) {
		if (auto* payload = ImGui::AcceptDragDropPayload("entity")) {
			EntityRef dropped_entity = *(EntityRef*)payload->Data;
			*entity = dropped_entity;
			changed = true;
		}
		ImGui::EndDragDropTarget();
	}

	if (entity->isValid()) {
		ImGui::SameLine();
		if (ImGuiEx::IconButton(ICON_FA_BULLSEYE, "Go to")) {
			m_deferred_select = *entity;
		}
		ImGui::SameLine();
		if (ImGuiEx::IconButton(ICON_FA_TRASH, "Clear")) {
			*entity = INVALID_ENTITY;
			changed = true;
		}
	}
	ImGui::PopStyleVar();

	if (ImGuiEx::BeginResizablePopup("popup", ImVec2(200, 300), ImGuiWindowFlags_NoNavInputs)) {
		static TextFilter entity_filter;
		static i32 selected_idx = -1;
		entity_filter.gui("Filter", -1, ImGui::IsWindowAppearing());
		const bool insert_enter = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
		bool scroll = false;
		if (ImGui::IsItemFocused()) {
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && selected_idx > 0) {
				--selected_idx;
				scroll = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
				++selected_idx;
				scroll = true;
			}
		}
		
		if (ImGui::BeginChild("list", ImVec2(0, ImGui::GetContentRegionAvail().y))) {
			i32 idx = -1;
			// TODO imgui clipper
			for (EntityPtr i = world.getFirstEntity(); i.isValid(); i = world.getNextEntity(*i)) {
				getEntityListDisplayName(m_app, world, Span(buf), i);
				const bool show = entity_filter.pass(buf);
				if (!show) continue;

				ImGui::PushID(i.index);
				++idx;
				const bool selected = selected_idx == idx;
				if (show && (ImGui::Selectable(buf, selected) || (selected && insert_enter))) {
					*entity = i;
					changed = true;
					ImGui::CloseCurrentPopup();
					ImGui::PopID();
					break;
				}
				if (selected && scroll) {
					ImGui::SetScrollHereY();
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		ImGui::EndPopup();
	}
	ImGui::PopID();
	return changed;
}


static bool componentTreeNode(StudioApp& app, WorldEditor& editor, ComponentType cmp_type, const EntityRef* entities, int entities_count)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap;
	ImGui::Separator();
	const char* cmp_type_name = app.getComponentTypeName(cmp_type);
	const char* icon = app.getComponentIcon(cmp_type);
	ImGui::PushFont(app.getBoldFont());
	bool is_open;
	bool enabled = true;
	IModule* module = editor.getWorld()->getModule(cmp_type);
	if (entities_count == 1 && reflection::getPropertyValue(*module, entities[0], cmp_type, "Enabled", enabled)) {
		is_open = ImGui::TreeNodeEx((void*)(uintptr)cmp_type.index, flags, "%s", "");
		ImGui::SameLine();
		ComponentUID cmp;
		cmp.type = cmp_type;
		cmp.entity = entities[0];
		cmp.module = editor.getWorld()->getModule(cmp_type);
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


void PropertyGrid::showComponentProperties(Span<const EntityRef> entities, ComponentType cmp_type, WorldEditor& editor) {
	ImGui::PushID(cmp_type.index);
	defer { ImGui::PopID(); };

	const reflection::ComponentBase* component = reflection::getComponent(cmp_type);
	bool filter_properties = false;
	if (m_property_filter.isActive() && component) {
		// if all properties are filtered out, don't show component at all
		bool has_blob = false;
		reflection::forEachProperty(cmp_type, [&](auto& prop, const reflection::ArrayProperty* parent) {
			if constexpr (IsSame<decltype(prop), const reflection::BlobProperty&>::Value) {
				// blob is opaque but can have properties passing filter, so let's show UI
				has_blob = true;
			}
			if (m_property_filter.pass(prop.name)) {
				filter_properties = true;
			}
		});

		if (has_blob) filter_properties = true;
		else if (m_property_filter.pass(component->label)) filter_properties = false;
		else if (!filter_properties) return;
	}

	bool is_open = componentTreeNode(m_app, editor, cmp_type, &entities[0], entities.size());
	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(ICON_FA_ELLIPSIS_V).x);
	if (ImGuiEx::IconButton(ICON_FA_ELLIPSIS_V, "Context menu")) {
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

	static const TextFilter empty_filter;
	const TextFilter& filter = filter_properties ? m_property_filter : empty_filter;
	if (component) {
		GridUIVisitor visitor(m_app, -1, entities, cmp_type, filter, editor);
		component->visit(visitor);
	}

	for (IPlugin* i : m_plugins) {
		i->onGUI(*this, entities, cmp_type, filter, editor);
	}
	ImGui::TreePop();
}


void PropertyGrid::showCoreProperties(Span<const EntityRef> entities, WorldEditor& editor) const
{
	ImGui::PushFont(m_app.getBoldFont());
	if (!ImGui::TreeNodeEx("General", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PopFont();
		return;
	}
	ImGui::PopFont();

	char name[World::ENTITY_NAME_MAX_LENGTH];
	World& world = *editor.getWorld();
	const char* entity_name = world.getEntityName(entities[0]);
	copyString(name, entity_name);
	ImGui::SetNextItemWidth(-1);
	if (ImGui::InputTextWithHint("##name", "Name", name, sizeof(name), ImGuiInputTextFlags_AutoSelectAll)) editor.setEntityName(entities[0], name);
	if (entities.size() == 1)
	{
		PrefabSystem& prefab_system = editor.getPrefabSystem();
		PrefabResource* prefab = prefab_system.getPrefabResource(entities[0]);
		if (prefab)
		{
			ImGuiEx::Label("Prefab");
			ImGuiEx::TextUnformatted(prefab->getPath());
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
			ImGui::TextUnformatted(name);

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
				
				ImGuiEx::Label("Local rotation");
				const Vec3 old_euler = tr.rot.toEuler();
				Vec3 euler = old_euler;
				if (ImGuiEx::InputRotation("##lcl_rot", &euler.x)) {
					Array<Quat> rots(m_app.getAllocator());
					for (EntityRef entity : entities) {
						Vec3 tmp = world.getLocalTransform(entity).rot.toEuler();
			
						if (fabs(euler.x - old_euler.x) > 0.0001f) tmp.x = euler.x;
						if (fabs(euler.y - old_euler.y) > 0.0001f) tmp.y = euler.y;
						if (fabs(euler.z - old_euler.z) > 0.0001f) tmp.z = euler.z;
						rots.emplace().fromEuler(tmp);
					}
					editor.setEntitiesLocalRotation(&entities[0], &rots[0], entities.size());
				}
			}
		}
	}
	else
	{
		ImGuiEx::Label("ID");
		ImGui::TextUnformatted("Multiple objects");
		ImGuiEx::Label("Name");
		ImGui::TextUnformatted("Multi-object editing not supported.");
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
			Array<Quat> rots(m_app.getAllocator());
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


static void showAddComponentNode(const StudioApp::AddCmpTreeNode* node, const TextFilter& filter, EntityPtr parent, WorldEditor& editor)
{
	if (!node) return;

	if (filter.isActive()) {
		if (!node->plugin) showAddComponentNode(node->child, filter, parent, editor);
		else if (filter.pass(node->plugin->getLabel())) node->plugin->onGUI(false, true, parent, editor);
		showAddComponentNode(node->next, filter, parent, editor);
		return;
	}

	if (node->plugin) {
		node->plugin->onGUI(false, false, parent, editor);
		showAddComponentNode(node->next, filter, parent, editor);
		return;
	}

	const char* last = reverseFind(node->label, '/');
	if (ImGui::BeginMenu(last ? last + 1 : node->label))
	{
		showAddComponentNode(node->child, filter, parent, editor);
		ImGui::EndMenu();
	}
	showAddComponentNode(node->next, filter, parent, editor);
}

void PropertyGrid::onPathDropped(const char* path) {
	PathInfo info(path);
	for (IPlugin* i : m_plugins) {
		i->onPathDropped(info);
	}
}

void PropertyGrid::onGUI() {
	for (IPlugin* i : m_plugins) {
		i->update();
	}

	if (m_app.checkShortcut(m_toggle_ui, true)) m_is_open = !m_is_open;

	if (m_app.checkShortcut(m_focus_filter_action, true)) {
		m_focus_filter_request = true;
		m_is_open = true;
	}

	if (!m_is_open) return;

	WorldEditor& editor = m_app.getWorldEditor();
	Span<const EntityRef> ents = editor.getSelectedEntities();
	if (m_focus_filter_request) ImGui::SetNextWindowFocus();
	if (ImGui::Begin(ICON_FA_INFO_CIRCLE "Inspector##inspector", &m_is_open)) {
		ImVec2 cp_screen_pos = ImGui::GetCursorScreenPos();
		ImVec2 window_size = ImGui::GetWindowSize();
		ImGui::Dummy(window_size);
		if (ImGui::BeginDragDropTarget()) {
			if (auto* payload = ImGui::AcceptDragDropPayload("path")) {
				const char* dropped_path = (const char*)payload->Data;
				onPathDropped(dropped_path);
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::SetCursorScreenPos(cp_screen_pos);
		
		if (m_focus_filter_request) {
			ImGui::SetKeyboardFocusHere();
			m_focus_filter_request = false;
		}

		if (ents.size() != 0) {
			showCoreProperties(ents, editor);
			m_property_filter.gui("Filter", -1, ImGui::IsWindowAppearing(), &m_focus_filter_action, true);
			World& world = *editor.getWorld();
			for (ComponentType cmp_type : world.getComponents(ents[0])) {
				showComponentProperties(ents, cmp_type, editor);
			}

			ImGui::Separator();
			const float x = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(ICON_FA_PLUS "Add component").x - ImGui::GetStyle().FramePadding.x * 2) * 0.5f;
			ImGui::SetCursorPosX(x);
			if (ImGui::Button(ICON_FA_PLUS "Add component")) ImGui::OpenPopup("AddComponentPopup");

			if (ImGui::BeginPopup("AddComponentPopup", ImGuiWindowFlags_AlwaysAutoResize)) {
				m_component_filter.gui("Filter", 200, ImGui::IsWindowAppearing());
				showAddComponentNode(m_app.getAddComponentTreeRoot().child, m_component_filter, INVALID_ENTITY, editor);
				ImGui::EndPopup();
			}
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
