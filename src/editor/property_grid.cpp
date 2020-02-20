#include <imgui/imgui.h>

#include "property_grid.h"
#include "asset_browser.h"
#include "editor/prefab_system.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/crt.h"
#include "engine/iplugin.h"
#include "engine/math.h"
#include "engine/prefab.h"
#include "engine/reflection.h"
#include "engine/resource.h"
#include "engine/stream.h"
#include "engine/universe/universe.h"
#include "engine/math.h"
#include "utils.h"


namespace Lumix
{


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


struct GridUIVisitor final : Reflection::IPropertyVisitor
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


	struct Attributes : Reflection::IAttributeVisitor
	{
		void visit(const Reflection::IAttribute& attr) override
		{
			switch (attr.getType())
			{
				case Reflection::IAttribute::RADIANS:
					is_radians = true;
					break;
				case Reflection::IAttribute::COLOR:
					is_color = true;
					break;
				case Reflection::IAttribute::MIN:
					min = ((Reflection::MinAttribute&)attr).min;
					break;
				case Reflection::IAttribute::CLAMP:
					min = ((Reflection::ClampAttribute&)attr).min;
					max = ((Reflection::ClampAttribute&)attr).max;
					break;
				case Reflection::IAttribute::RESOURCE:
					resource_type = ((Reflection::ResourceAttribute&)attr).type;
					break;
			}
		}

		float max = FLT_MAX;
		float min = -FLT_MAX;
		bool is_color = false;
		bool is_radians = false;
		ResourceType resource_type;
	};


	static Attributes getAttributes(const Reflection::PropertyBase& prop)
	{
		Attributes attrs;
		prop.visit(attrs);
		return attrs;
	}


	bool skipProperty(const Reflection::PropertyBase& prop)
	{
		return equalStrings(prop.name, "Enabled");
	}


	void visit(const Reflection::Property<float>& prop) override
	{
		if (skipProperty(prop)) return;
		Attributes attrs = getAttributes(prop);
		ComponentUID cmp = getComponent();
		float f;
		OutputMemoryStream blob(&f, sizeof(f));
		prop.getValue(cmp, m_index, blob);

		if (attrs.is_radians) f = radiansToDegrees(f);
		if (ImGui::DragFloat(prop.name, &f, 1, attrs.min, attrs.max))
		{
			f = clamp(f, attrs.min, attrs.max);
			if (attrs.is_radians) f = degreesToRadians(f);
			m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &f, sizeof(f));
		}
	}


	void visit(const Reflection::Property<int>& prop) override
	{
		if (skipProperty(prop)) return;
		ComponentUID cmp = getComponent();
		int value;
		OutputMemoryStream blob(&value, sizeof(value));
		prop.getValue(cmp, m_index, blob);

		if (ImGui::InputInt(prop.name, &value))
		{
			m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
		}
	}


	void visit(const Reflection::Property<u32>& prop) override
	{
		if (skipProperty(prop)) return;
		ComponentUID cmp = getComponent();
		u32 value;
		OutputMemoryStream blob(&value, sizeof(value));
		prop.getValue(cmp, m_index, blob);

		if (ImGui::InputScalar(prop.name, ImGuiDataType_U32, &value))
		{
			m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
		}
	}


	void visit(const Reflection::Property<EntityPtr>& prop) override
	{
		ComponentUID cmp = getComponent();
		EntityPtr entity;
		OutputMemoryStream blob(&entity, sizeof(entity));
		prop.getValue(cmp, m_index, blob);

		char buf[128];
		getEntityListDisplayName(m_editor, Span(buf), entity);
		ImGui::PushID(prop.name);
		
		float item_w = ImGui::CalcItemWidth();
		auto& style = ImGui::GetStyle();
		float text_width = maximum(50.0f, item_w - ImGui::CalcTextSize("...").x - style.FramePadding.x * 2);

		auto pos = ImGui::GetCursorPos();
		pos.x += text_width;
		ImGui::BeginGroup();
		ImGui::AlignTextToFramePadding();
		ImGui::PushTextWrapPos(pos.x);
		ImGui::Text("%s", buf);
		ImGui::PopTextWrapPos();
		ImGui::SameLine();
		ImGui::SetCursorPos(pos);
		if (ImGui::Button("..."))
		{
			ImGui::OpenPopup(prop.name);
		}
		ImGui::EndGroup();
		ImGui::SameLine();
		ImGui::Text("%s", prop.name);

		Universe& universe = *m_editor.getUniverse();
		if (ImGui::BeginPopup(prop.name))
		{
			if (entity.isValid() && ImGui::Button("Select")) m_grid.m_deferred_select = entity;

			static char entity_filter[32] = {};
			ImGui::InputTextWithHint("##filter", "Filter", entity_filter, sizeof(entity_filter));
			for (EntityPtr i = universe.getFirstEntity(); i.isValid(); i = universe.getNextEntity((EntityRef)i))
			{
				getEntityListDisplayName(m_editor, Span(buf), i);
				bool show = entity_filter[0] == '\0' || stristr(buf, entity_filter) != 0;
				if (show && ImGui::Selectable(buf))
				{
					m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &i, sizeof(i));
				}
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}


	void visit(const Reflection::Property<Vec2>& prop) override
	{
		if (skipProperty(prop)) return;
		ComponentUID cmp = getComponent();
		Vec2 value;
		OutputMemoryStream blob(&value, sizeof(value));
		prop.getValue(cmp, m_index, blob);
		if (ImGui::DragFloat2(prop.name, &value.x))
		{
			m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
		}
	}


	void visit(const Reflection::Property<Vec3>& prop) override
	{
		if (skipProperty(prop)) return;
		Attributes attrs = getAttributes(prop);
		ComponentUID cmp = getComponent();
		Vec3 value;
		OutputMemoryStream blob(&value, sizeof(value));
		prop.getValue(cmp, m_index, blob);

		if (attrs.is_color)
		{
			if (ImGui::ColorEdit3(prop.name, &value.x))
			{
				m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
			}
		}
		else
		{
			if (attrs.is_radians) value = radiansToDegrees(value);
			if (ImGui::DragFloat3(prop.name, &value.x, 1, attrs.min, attrs.max))
			{
				if (attrs.is_radians) value = degreesToRadians(value);
				m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
			}
		}
	}


	void visit(const Reflection::Property<IVec3>& prop) override
	{
		if (skipProperty(prop)) return;
		ComponentUID cmp = getComponent();
		IVec3 value;
		OutputMemoryStream blob(&value, sizeof(value));
		prop.getValue(cmp, m_index, blob);
		
		if (ImGui::DragInt3(prop.name, &value.x)) {
			m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
		}
	}


	void visit(const Reflection::Property<Vec4>& prop) override
	{
		if (skipProperty(prop)) return;
		Attributes attrs = getAttributes(prop);
		ComponentUID cmp = getComponent();
		Vec4 value;
		OutputMemoryStream blob(&value, sizeof(value));
		prop.getValue(cmp, m_index, blob);

		if (attrs.is_color)
		{
			if (ImGui::ColorEdit4(prop.name, &value.x))
			{
				m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
			}
		}
		else
		{
			if (ImGui::DragFloat4(prop.name, &value.x))
			{
				m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
			}
		}
	}


	void visit(const Reflection::Property<bool>& prop) override
	{
		if (skipProperty(prop)) return;
		ComponentUID cmp = getComponent();
		bool value;
		OutputMemoryStream blob(&value, sizeof(value));
		prop.getValue(cmp, m_index, blob);

		if (ImGui::CheckboxEx(prop.name, &value))
		{
			m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
		}
	}


	void visit(const Reflection::Property<Path>& prop) override
	{
		if (skipProperty(prop)) return;
		ComponentUID cmp = getComponent();
		char tmp[1024];
		OutputMemoryStream blob(&tmp, sizeof(tmp));
		prop.getValue(cmp, m_index, blob);

		Attributes attrs = getAttributes(prop);

		if (attrs.resource_type != INVALID_RESOURCE_TYPE)
		{
			if (m_app.getAssetBrowser().resourceInput(prop.name, StaticString<20>("", (u64)&prop), Span(tmp), attrs.resource_type))
			{
				m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), tmp, stringLength(tmp) + 1);
			}
		}
		else
		{
			if (ImGui::InputText(prop.name, tmp, sizeof(tmp)))
			{
				m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), tmp, stringLength(tmp) + 1);
			}
		}
	}


	void visit(const Reflection::Property<const char*>& prop) override
	{
		if (skipProperty(prop)) return;
		ComponentUID cmp = getComponent();
		char tmp[1024];
		OutputMemoryStream blob(&tmp, sizeof(tmp));
		prop.getValue(cmp, m_index, blob);

		if (ImGui::InputText(prop.name, tmp, sizeof(tmp)))
		{
			m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), tmp, stringLength(tmp) + 1);
		}
	}


	void visit(const Reflection::IBlobProperty& prop) override {}


	void visit(const Reflection::IArrayProperty& prop) override
	{
		if (skipProperty(prop)) return;
		ImGui::Unindent();
		bool is_open = ImGui::TreeNodeEx(prop.name, ImGuiTreeNodeFlags_AllowItemOverlap);
		if (m_entities.size() > 1)
		{
			ImGui::Text("Multi-object editing not supported.");
			if (is_open) ImGui::TreePop();
			ImGui::Indent();
			return;
		}

		ComponentUID cmp = getComponent();
		int count = prop.getCount(cmp);
		const ImGuiStyle& style = ImGui::GetStyle();
		if (prop.canAddRemove())
		{
			ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("Add").x - style.FramePadding.x * 2 - style.WindowPadding.x - 15);
			if (ImGui::SmallButton("Add"))
			{
				m_editor.addArrayPropertyItem(cmp, prop);
				count = prop.getCount(cmp);
			}
		}
		if (!is_open)
		{
			ImGui::Indent();
			return;
		}

		for (int i = 0; i < count; ++i)
		{
			char tmp[10];
			toCString(i, Span(tmp));
			ImGui::PushID(i);
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap;
			bool is_open = !prop.canAddRemove() || ImGui::TreeNodeEx(tmp, flags);
			if (prop.canAddRemove())
			{
				ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("Remove").x - style.FramePadding.x * 2 - style.WindowPadding.x - 15);
				if (ImGui::SmallButton("Remove"))
				{
					m_editor.removeArrayPropertyItem(cmp, i, prop);
					--i;
					count = prop.getCount(cmp);
					if(is_open) ImGui::TreePop();
					ImGui::PopID();
					continue;
				}
			}

			if (is_open)
			{
				GridUIVisitor v(m_app, i, m_entities, m_cmp_type, m_editor);
				prop.visit(v);
				if (prop.canAddRemove()) ImGui::TreePop();
			}

			ImGui::PopID();
		}
		ImGui::TreePop();
		ImGui::Indent();
	}


	void visit(const Reflection::IEnumProperty& prop) override
	{
		if (skipProperty(prop)) return;
		if (m_entities.size() > 1)
		{
			ImGui::LabelText(prop.name, "Multi-object editing not supported.");
			return;
		}

		ComponentUID cmp = getComponent();
		int value;
		OutputMemoryStream blob(&value, sizeof(value));
		prop.getValue(cmp, m_index, blob);
		int count = prop.getEnumCount(cmp);

		struct Data
		{
			const Reflection::IEnumProperty* prop;
			ComponentUID cmp;
		};

		auto getter = [](void* data, int index, const char** out) -> bool {
			Data* combo_data = (Data*)data;
			*out = combo_data->prop->getEnumName(combo_data->cmp, index);
			return true;
		};

		Data data;
		data.cmp = cmp;
		data.prop = &prop;

		int idx = prop.getEnumValueIndex(cmp, value);
		if (ImGui::Combo(prop.name, &idx, getter, &data, count))
		{
			value = prop.getEnumValue(cmp, idx);
			ASSERT(cmp.isValid());
			const EntityRef e = (EntityRef)cmp.entity;
			m_editor.setProperty(cmp.type, m_index, prop, &e, 1, &value, sizeof(value));
		}
	}


	StudioApp& m_app;
	WorldEditor& m_editor;
	ComponentType m_cmp_type;
	const Array<EntityRef>& m_entities;
	int m_index;
	PropertyGrid& m_grid;
};


static bool componentTreeNode(StudioApp& app, ComponentType cmp_type, const EntityRef* entities, int entities_count)
{
	static const u32 ENABLED_HASH = crc32("Enabled");
	const Reflection::PropertyBase* enabled_prop = Reflection::getProperty(cmp_type, ENABLED_HASH);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap;
	ImGui::Separator();
	const char* cmp_type_name = app.getComponentTypeName(cmp_type);
	ImGui::PushFont(app.getBoldFont());
	bool is_open;
	if (enabled_prop)
	{
		is_open = ImGui::TreeNodeEx((void*)(uintptr)cmp_type.index, flags, "%s", "");
		ImGui::SameLine();
		bool b;
		ComponentUID cmp;
		cmp.type = cmp_type;
		cmp.entity = entities[0];
		cmp.scene = app.getWorldEditor().getUniverse()->getScene(cmp_type);
		OutputMemoryStream blob(&b, sizeof(b));
		enabled_prop->getValue(cmp, -1, blob);
		if(ImGui::Checkbox(cmp_type_name, &b))
		{
			app.getWorldEditor().setProperty(cmp_type, -1, *enabled_prop, entities, entities_count, &b, sizeof(b));
		}
	}
	else
	{ 
		is_open = ImGui::TreeNodeEx((void*)(uintptr)cmp_type.index, flags, "%s", cmp_type_name);
	}
	ImGui::PopFont();
	return is_open;
}


void PropertyGrid::showComponentProperties(const Array<EntityRef>& entities, ComponentType cmp_type)
{
	bool is_open = componentTreeNode(m_app, cmp_type, &entities[0], entities.size());
	ImGuiStyle& style = ImGui::GetStyle();
	ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("Remove").x - style.FramePadding.x * 2 - style.WindowPadding.x - 15);
	if (ImGui::SmallButton("Remove"))
	{
		m_editor.destroyComponent(entities, cmp_type);
		if (is_open) ImGui::TreePop();
		return;
	}

	if (!is_open) return;

	const Reflection::ComponentBase* component = Reflection::getComponent(cmp_type);
	GridUIVisitor visitor(m_app, -1, entities, cmp_type, m_editor);
	if (component) component->visit(visitor);

	if (m_deferred_select.isValid())
	{
		const EntityRef e = (EntityRef)m_deferred_select;
		m_editor.selectEntities(&e, 1, false);
		m_deferred_select = INVALID_ENTITY;
	}

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


bool PropertyGrid::entityInput(const char* label, const char* str_id, EntityPtr& entity)
{
	const auto& style = ImGui::GetStyle();
	float item_w = ImGui::CalcItemWidth();
	ImGui::PushItemWidth(
		item_w - ImGui::CalcTextSize("...").x - style.FramePadding.x * 2 - style.ItemSpacing.x);
	char buf[50];
	getEntityListDisplayName(m_editor, Span(buf), entity);
	ImGui::LabelText("", "%s", buf);
	ImGui::SameLine();
	StaticString<30> popup_name("pu", str_id);
	if (ImGui::Button(StaticString<30>("...###br", str_id)))
	{
		ImGui::OpenPopup(popup_name);
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (auto* payload = ImGui::AcceptDragDropPayload("entity"))
		{
			entity = *(EntityRef*)payload->Data;
			ImGui::EndDragDropTarget();
			return true;
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::SameLine();
	ImGui::Text("%s", label);
	ImGui::PopItemWidth();

	if (ImGui::BeginPopup(popup_name))
	{
		if (entity.isValid())
		{
			if (ImGui::Button("Select current")) m_deferred_select = entity;
			ImGui::SameLine();
			if (ImGui::Button("Empty"))
			{
				entity = INVALID_ENTITY;
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return true;
			}
		}
		Universe* universe = m_editor.getUniverse();
		static char entity_filter[32] = {};
		ImGui::InputTextWithHint("##filter", "Filter", entity_filter, sizeof(entity_filter));
		if (ImGui::ListBoxHeader("Entities"))
		{
			if (entity_filter[0])
			{
				for (EntityPtr i = universe->getFirstEntity(); i.isValid(); i = universe->getNextEntity((EntityRef)i))
				{
					getEntityListDisplayName(m_editor, Span(buf), i);
					if (stristr(buf, entity_filter) == nullptr) continue;
					if (ImGui::Selectable(buf))
					{
						ImGui::ListBoxFooter();
						entity = i;
						ImGui::CloseCurrentPopup();
						ImGui::EndPopup();
						return true;
					}
				}
			}
			else
			{
				for (EntityPtr i = universe->getFirstEntity(); i.isValid(); i = universe->getNextEntity((EntityRef)i))
				{
					getEntityListDisplayName(m_editor, Span(buf), i);
					if (ImGui::Selectable(buf))
					{
						ImGui::ListBoxFooter();
						entity = i;
						ImGui::CloseCurrentPopup();
						ImGui::EndPopup();
						return true;
					}
				}
			}
			ImGui::ListBoxFooter();
		}

		ImGui::EndPopup();
	}
	return false;
}


void PropertyGrid::showCoreProperties(const Array<EntityRef>& entities) const
{
	char name[256];
	const char* tmp = m_editor.getUniverse()->getEntityName(entities[0]);
	copyString(name, tmp);
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
			ImGui::SameLine();
			if (ImGui::Button("Save prefab"))
			{
				prefab_system.savePrefab(prefab->getPath());
			}
		}

		EntityPtr parent = m_editor.getUniverse()->getParent(entities[0]);
		if (parent.isValid())
		{
			getEntityListDisplayName(m_editor, Span(name), parent);
			ImGui::LabelText("Parent", "%s", name);

			Transform tr = m_editor.getUniverse()->getLocalTransform(entities[0]);
			DVec3 old_pos = tr.pos;
			if (ImGui::DragScalarN("Local position", ImGuiDataType_Double, &tr.pos.x, 3, 1.f))
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
	else
	{
		ImGui::LabelText("ID", "%s", "Multiple objects");
		ImGui::LabelText("Name", "%s", "Multi-object editing not supported.");
	}


	DVec3 pos = m_editor.getUniverse()->getPosition(entities[0]);
	DVec3 old_pos = pos;
	if (ImGui::DragScalarN("Position", ImGuiDataType_Double, &pos.x, 3, 1.f))
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

	Universe* universe = m_editor.getUniverse();
	Quat rot = universe->getRotation(entities[0]);
	Vec3 old_euler = rot.toEuler();
	Vec3 euler = radiansToDegrees(old_euler);
	if (ImGui::DragFloat3("Rotation", &euler.x))
	{
		if (euler.x <= -90.0f || euler.x >= 90.0f) euler.y = 0;
		euler.x = degreesToRadians(clamp(euler.x, -90.0f, 90.0f));
		euler.y = degreesToRadians(fmodf(euler.y + 180, 360.0f) - 180);
		euler.z = degreesToRadians(fmodf(euler.z + 180, 360.0f) - 180);
		rot.fromEuler(euler);
		
		Array<Quat> rots(m_editor.getAllocator());
		for (EntityRef entity : entities)
		{
			Vec3 tmp = universe->getRotation(entity).toEuler();
			
			if (fabs(euler.x - old_euler.x) > 0.01f) tmp.x = euler.x;
			if (fabs(euler.y - old_euler.y) > 0.01f) tmp.y = euler.y;
			if (fabs(euler.z - old_euler.z) > 0.01f) tmp.z = euler.z;
			rots.emplace().fromEuler(tmp);
		}
		m_editor.setEntitiesRotations(&entities[0], &rots[0], entities.size());
	}

	float scale = m_editor.getUniverse()->getScale(entities[0]);
	if (ImGui::DragFloat("Scale", &scale, 0.1f))
	{
		m_editor.setEntitiesScale(&entities[0], entities.size(), scale);
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

	auto& ents = m_editor.getSelectedEntities();
	if (ImGui::Begin("Properties", &m_is_open) && !ents.empty())
	{
		if (ImGui::Button("Add component"))
		{
			ImGui::OpenPopup("AddComponentPopup");
		}
		if (ImGui::BeginPopup("AddComponentPopup"))
		{
			ImGui::InputTextWithHint("##filter", "Filter", m_component_filter, sizeof(m_component_filter));
			showAddComponentNode(m_app.getAddComponentTreeRoot().child, m_component_filter);
			ImGui::EndPopup();
		}

		showCoreProperties(ents);

		Universe& universe = *m_editor.getUniverse();
		for (ComponentUID cmp = universe.getFirstComponent(ents[0]); cmp.isValid();
			 cmp = universe.getNextComponent(cmp))
		{
			showComponentProperties(ents, cmp.type);
		}
	}
	ImGui::End();
}


} // namespace Lumix
