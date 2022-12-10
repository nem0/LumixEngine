#include <imgui/imgui.h>

#include "animation/animation.h"
#include "animation/animation_scene.h"
#include "animation/controller.h"
#include "animation/property_animation.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/hash_map.h"
#include "engine/log.h"
#include "engine/os.h"
#include "controller_editor.h"
#include "engine/reflection.h"
#include "engine/universe.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "renderer/render_scene.h"


using namespace Lumix;


static const ComponentType ANIMABLE_TYPE = reflection::getComponentType("animable");
static const ComponentType PROPERTY_ANIMATOR_TYPE = reflection::getComponentType("property_animator");
static const ComponentType ANIMATOR_TYPE = reflection::getComponentType("animator");
static const ComponentType RENDERABLE_TYPE = reflection::getComponentType("model_instance");


namespace
{

	
struct AnimationAssetBrowserPlugin : AssetBrowser::Plugin
{
	explicit AnimationAssetBrowserPlugin(StudioApp& app)
		: m_app(app)
		, AssetBrowser::Plugin(app.getAllocator())
	{
		app.getAssetCompiler().registerExtension("ani", Animation::TYPE);
	}

	void deserialize(InputMemoryStream& blob) override { ASSERT(false); }
	void serialize(OutputMemoryStream& blob) override {}

	bool onGUI(Span<Resource*> resources) override
	{
		if (resources.length() > 1) return false;

		auto* animation = static_cast<Animation*>(resources[0]);
		ImGuiEx::Label("Length");
		ImGui::Text("%.3fs", animation->getLength().seconds());
		return false;
	}


	void onResourceUnloaded(Resource* resource) override {}


	const char* getName() const override { return "Animation"; }


	ResourceType getResourceType() const override { return Animation::TYPE; }


	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		if (type == Animation::TYPE) return m_app.getAssetBrowser().copyTile("editor/textures/tile_animation.tga", out_path);
		return false;
	}

	StudioApp& m_app;
};


struct PropertyAnimationPlugin : AssetBrowser::Plugin, AssetCompiler::IPlugin
{
	explicit PropertyAnimationPlugin(StudioApp& app)
		: m_app(app)
		, AssetBrowser::Plugin(app.getAllocator())
	{
		app.getAssetCompiler().registerExtension("anp", PropertyAnimation::TYPE);
	}

	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return "anp"; }

	bool compile(const Path& src) override {
		return m_app.getAssetCompiler().copyCompile(src);
	}

	bool createResource(const char* path) override
	{
		os::OutputFile file;
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.open(path, file)) {
			logError("Failed to create ", path);
			return false;
		}

		file.close();
		return true;
	}

	static bool hasFloatProperty(const reflection::ComponentBase* cmp) {
		struct : reflection::IEmptyPropertyVisitor {
			void visit(const reflection::Property<float>& prop) override { result = true; }
			bool result = false;
		} visitor;
		cmp->visit(visitor);
		return visitor.result;
	}

	void ShowAddCurveMenu(PropertyAnimation* animation) {
		if (!ImGui::BeginMenu("Add curve")) return;
		
		for (const reflection::RegisteredComponent& cmp_type : reflection::getComponents()) {
			const char* cmp_type_name = cmp_type.cmp->name;
			if (!hasFloatProperty(cmp_type.cmp)) continue;
			if (!ImGui::BeginMenu(cmp_type_name)) continue;

			const reflection::ComponentBase* component = cmp_type.cmp;
			struct : reflection::IEmptyPropertyVisitor
			{
				void visit(const reflection::Property<float>& prop) override
				{
					int idx = animation->curves.find([&](PropertyAnimation::Curve& rhs) {
						return rhs.cmp_type == cmp_type && rhs.property == &prop;
					});
					if (idx < 0 &&ImGui::MenuItem(prop.name))
					{
						PropertyAnimation::Curve& curve = animation->addCurve();
						curve.cmp_type = cmp_type;
						curve.property = &prop;
						curve.frames.push(0);
						curve.frames.push(animation->curves.size() > 1 ? animation->curves[0].frames.back() : 1);
						curve.values.push(0);
						curve.values.push(0);
					}
				}
				PropertyAnimation* animation;
				ComponentType cmp_type;
			} visitor;

			visitor.animation = animation;
			visitor.cmp_type = cmp_type.cmp->component_type;
			component->visit(visitor);

			ImGui::EndMenu();
		}

		ImGui::EndMenu();
	}


	void savePropertyAnimation(PropertyAnimation& anim)
	{
		ASSERT(anim.isReady());
		OutputMemoryStream blob(m_app.getAllocator());
		anim.serialize(blob);
		m_app.getAssetBrowser().saveResource(anim, blob);
	}
	
	void deserialize(InputMemoryStream& blob) override {
		((PropertyAnimation*)m_current_resources[0])->deserialize(blob);
	}
	
	void serialize(OutputMemoryStream& blob) override {
		((PropertyAnimation*)m_current_resources[0])->serialize(blob);
	}


	bool onGUI(Span<Resource*> resources) override
	{
		m_current_resources = resources;
		if (resources.length() > 1) return false;

		auto* animation = static_cast<PropertyAnimation*>(resources[0]);
		if (!animation->isReady()) return false;

		if (ImGui::Button(ICON_FA_SAVE "Save")) savePropertyAnimation(*animation);
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_EXTERNAL_LINK_ALT "Open externally")) m_app.getAssetBrowser().openInExternalEditor(animation);
			
		ShowAddCurveMenu(animation);

		bool changed = false;
		if (!animation->curves.empty())
		{
			int frames = animation->curves[0].frames.back();
			ImGuiEx::Label("Frames");
			if (ImGui::InputInt("##frames", &frames))
			{
				for (auto& curve : animation->curves)
				{
					curve.frames.back() = frames;
					changed = true;
				}
			}
		}

		for (int i = 0, n = animation->curves.size(); i < n; ++i)
		{
			PropertyAnimation::Curve& curve = animation->curves[i];
			const char* cmp_name = m_app.getComponentTypeName(curve.cmp_type);
			StaticString<64> tmp(cmp_name, " - ", curve.property->name);
			if (ImGui::Selectable(tmp, m_selected_curve == i)) m_selected_curve = i;
		}

		if (m_selected_curve >= animation->curves.size()) m_selected_curve = -1;
		if (m_selected_curve < 0) return changed;

		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 20);
		static ImVec2 size(-1, 200);
			
		PropertyAnimation::Curve& curve = animation->curves[m_selected_curve];
		ImVec2 points[16];
		ASSERT((u32)curve.frames.size() < lengthOf(points));
		for (int i = 0; i < curve.frames.size(); ++i)
		{
			points[i].x = (float)curve.frames[i];
			points[i].y = curve.values[i];
		}
		int new_count;
		int last_frame = curve.frames.back();
		int flags = (int)ImGuiEx::CurveEditorFlags::NO_TANGENTS | (int)ImGuiEx::CurveEditorFlags::SHOW_GRID;
		if (m_fit_curve_in_editor)
		{
			flags |= (int)ImGuiEx::CurveEditorFlags::RESET;
			m_fit_curve_in_editor = false;
		}
		int changed_idx = ImGuiEx::CurveEditor("curve", (float*)points, curve.frames.size(), lengthOf(points), size, flags, &new_count, &m_selected_point);
		if (changed_idx >= 0)
		{
			curve.frames[changed_idx] = int(points[changed_idx].x + 0.5f);
			curve.values[changed_idx] = points[changed_idx].y;
			curve.frames.back() = last_frame;
			curve.frames[0] = 0;
		}
		if (new_count != curve.frames.size())
		{
			curve.frames.resize(new_count);
			curve.values.resize(new_count);
			for (int i = 0; i < new_count; ++i)
			{
				curve.frames[i] = int(points[i].x + 0.5f);
				curve.values[i] = points[i].y;
			}
		}

		ImGui::PopItemWidth();

		if (ImGui::BeginPopupContextItem("curve"))
		{
			if (ImGui::Selectable("Fit data")) m_fit_curve_in_editor = true;

			ImGui::EndPopup();
		}

		if (m_selected_point >= 0 && m_selected_point < curve.frames.size())
		{
			ImGuiEx::Label("Frame");
			changed = ImGui::InputInt("##frame", &curve.frames[m_selected_point]) || changed;
			ImGuiEx::Label("Value");
			changed = ImGui::InputFloat("##val", &curve.values[m_selected_point]) || changed;
		}

		ImGuiEx::HSplitter("sizer", &size);
		return changed;
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Property animation"; }
	ResourceType getResourceType() const override { return PropertyAnimation::TYPE; }


	StudioApp& m_app;
	int m_selected_point = -1;
	int m_selected_curve = -1;
	bool m_fit_curve_in_editor = false;
	Span<Resource*> m_current_resources;
};


struct AnimControllerAssetBrowserPlugin : AssetBrowser::Plugin, AssetCompiler::IPlugin
{
	explicit AnimControllerAssetBrowserPlugin(StudioApp& app)
		: m_app(app)
		, AssetBrowser::Plugin(app.getAllocator())
	{
		app.getAssetCompiler().registerExtension("act", anim::Controller::TYPE);
	}

	void deserialize(InputMemoryStream& blob) override { ASSERT(false); }
	void serialize(OutputMemoryStream& blob) override {}

	bool compile(const Path& src) override {
		return m_app.getAssetCompiler().copyCompile(src);
	}

	bool onGUI(Span<Resource*> resources) override {
		if (resources.length() == 1 && ImGui::Button("Open in animation editor")) {
			m_controller_editor->show(resources[0]->getPath().c_str());
		}
		return false;
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Animation Controller"; }
	ResourceType getResourceType() const override { return anim::Controller::TYPE; }


	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		if (type == anim::Controller::TYPE) return m_app.getAssetBrowser().copyTile("editor/textures/tile_animation_graph.tga", out_path);
		return false;
	}


	StudioApp& m_app;
	anim::ControllerEditor* m_controller_editor = nullptr;
};


struct AnimablePropertyGridPlugin final : PropertyGrid::IPlugin
{
	explicit AnimablePropertyGridPlugin(StudioApp& app)
		: m_app(app)
	{
		m_is_playing = false;
	}


	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, WorldEditor& editor) override
	{
		if (cmp_type != ANIMABLE_TYPE) return;
		if (entities.length() != 1) return;

		const EntityRef entity = entities[0];
		auto* scene = (AnimationScene*)editor.getUniverse()->getScene(cmp_type);
		auto* animation = scene->getAnimableAnimation(entity);
		if (!animation) return;
		if (!animation->isReady()) return;

		ImGui::Checkbox("Preview", &m_is_playing);
		float time = scene->getAnimable(entity).time.seconds();
		if (ImGui::SliderFloat("Time", &time, 0, animation->getLength().seconds()))
		{
			scene->getAnimable(entity).time = Time::fromSeconds(time);
			scene->updateAnimable(entity, 0);
		}

		if (m_is_playing)
		{
			float time_delta = m_app.getEngine().getLastTimeDelta();
			scene->updateAnimable(entity, time_delta);
		}

		if (ImGui::CollapsingHeader("Transformation"))
		{
			auto* render_scene = (RenderScene*)scene->getUniverse().getScene(RENDERABLE_TYPE);
			if (scene->getUniverse().hasComponent(entity, RENDERABLE_TYPE))
			{
				const Pose* pose = render_scene->lockPose(entity);
				Model* model = render_scene->getModelInstanceModel(entity);
				if (pose && model)
				{
					ImGui::Columns(3);
					for (u32 i = 0; i < pose->count; ++i)
					{
						ImGui::Text("%s", model->getBone(i).name.c_str());
						ImGui::NextColumn();
						ImGui::Text("%f; %f; %f", pose->positions[i].x, pose->positions[i].y, pose->positions[i].z);
						ImGui::NextColumn();
						ImGui::Text("%f; %f; %f; %f", pose->rotations[i].x, pose->rotations[i].y, pose->rotations[i].z, pose->rotations[i].w);
						ImGui::NextColumn();
					}
					ImGui::Columns();
				}
				if (pose) render_scene->unlockPose(entity, false);
			}
		}
	}


	StudioApp& m_app;
	bool m_is_playing;
};


struct StudioAppPlugin : StudioApp::IPlugin
{
	explicit StudioAppPlugin(StudioApp& app)
		: m_app(app)
		, m_animable_plugin(app)
		, m_animation_plugin(app)
		, m_prop_anim_plugin(app)
		, m_anim_ctrl_plugin(app)
	{}

	const char* getName() const override { return "animation"; }

	void init() override {
		AssetCompiler& compiler = m_app.getAssetCompiler();
		const char* act_exts[] = { "act", nullptr };
		const char* anp_exts[] = { "anp", nullptr };
		compiler.addPlugin(m_anim_ctrl_plugin, act_exts);
		compiler.addPlugin(m_prop_anim_plugin, anp_exts);

		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		asset_browser.addPlugin(m_animation_plugin);
		asset_browser.addPlugin(m_prop_anim_plugin);
		asset_browser.addPlugin(m_anim_ctrl_plugin);

		m_app.getPropertyGrid().addPlugin(m_animable_plugin);
		
		m_anim_editor = anim::ControllerEditor::create(m_app);
		m_app.addPlugin(*m_anim_editor);

		m_anim_ctrl_plugin.m_controller_editor = m_anim_editor.get();
	}

	bool showGizmo(UniverseView&, ComponentUID) override { return false; }
	
	~StudioAppPlugin() {
		AssetCompiler& compiler = m_app.getAssetCompiler();
		compiler.removePlugin(m_anim_ctrl_plugin);
		compiler.removePlugin(m_prop_anim_plugin);

		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		asset_browser.removePlugin(m_animation_plugin);
		asset_browser.removePlugin(m_prop_anim_plugin);
		asset_browser.removePlugin(m_anim_ctrl_plugin);
		m_app.getPropertyGrid().removePlugin(m_animable_plugin);
		m_app.removePlugin(*m_anim_editor);
	}


	StudioApp& m_app;
	AnimablePropertyGridPlugin m_animable_plugin;
	AnimationAssetBrowserPlugin m_animation_plugin;
	PropertyAnimationPlugin m_prop_anim_plugin;
	AnimControllerAssetBrowserPlugin m_anim_ctrl_plugin;
	UniquePtr<anim::ControllerEditor> m_anim_editor;
};


} // anonymous namespace


LUMIX_STUDIO_ENTRY(animation)
{
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}

