#include "animation/animation.h"
#include "animation/animation_system.h"
#include "animation/controller.h"
#include "animation/editor/state_machine_editor.h"
#include "animation/state_machine.h"
#include "editor/asset_browser.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/blob.h"
#include "engine/engine.h"
#include "engine/engine.h"
#include "engine/fs/os_file.h"
#include "engine/hash_map.h"
#include "engine/path.h"
#include "engine/property_register.h"
#include "engine/resource_manager.h"
#include "imgui/imgui.h"


using namespace Lumix;


static const ComponentType ANIMABLE_HASH = PropertyRegister::getComponentType("animable");
static const ResourceType ANIMATION_TYPE("animation");
static const ResourceType CONTROLLER_RESOURCE_TYPE("anim_controller");


inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y); }


namespace AnimEditor
{


struct AnimationEditor : public StudioApp::IPlugin
{
	AnimationEditor(StudioApp& app)
		: m_app(app)
		, m_opened(false)
		, m_offset(0, 0)
	{
		IAllocator& allocator = app.getWorldEditor()->getAllocator();
		m_action = LUMIX_NEW(allocator, Action)("Animation Editor", "animation_editor");
		m_action->func.bind<AnimationEditor, &AnimationEditor::toggleOpened>(this);
		m_action->is_selected.bind<AnimationEditor, &AnimationEditor::isOpened>(this);

		auto* manager = m_app.getWorldEditor()->getEngine().getResourceManager().get(CONTROLLER_RESOURCE_TYPE);

		m_resource = LUMIX_NEW(allocator, ControllerResource)(allocator, *manager);
	}


	bool isOpened() { return m_opened; }
	void toggleOpened() { m_opened = !m_opened; }


	void createState()
	{
		auto* root = m_resource->getRoot();
		if (!root->isContainer()) return;
		auto* container = (Container*)root;

		Engine& engine = m_app.getWorldEditor()->getEngine();
		IAllocator& allocator = m_app.getWorldEditor()->getAllocator();
		auto* engine_cmp = LUMIX_NEW(allocator, Anim::SimpleAnimationNode)(allocator);
		auto* engine_container = (Anim::Container*)container->engine_cmp;
		auto* ed = LUMIX_NEW(allocator, SimpleAnimationNode)(engine_cmp, container, allocator);
		ed->pos = ImVec2(0, 0);
		ed->size = ImVec2(100, 30);
		if (engine_container->children.empty())
		{
			engine_cmp->uid = 0;
		}
		else
		{
			engine_cmp->uid = engine_container->children.back()->uid + 1;
		}
		engine_container->children.push(ed->engine_cmp);
		container->m_editor_cmps.push(ed);
	}


	void drawGraph()
	{
		ImGui::BeginChild("canvas", ImVec2(0, 0), true);
		if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(2, 0.0f))
		{
			m_offset = m_offset + ImGui::GetIO().MouseDelta;
		}

		ImDrawList* draw = ImGui::GetWindowDrawList();
		auto canvas_screen_pos = ImGui::GetCursorScreenPos() + m_offset;
		m_resource->getRoot()->drawInside(draw, canvas_screen_pos);
		ImGui::EndChild();
	}


	void save()
	{
		IAllocator& allocator = m_app.getWorldEditor()->getAllocator();
		OutputBlob blob(allocator);
		m_resource->serialize(blob);
		FS::OsFile file;
		file.open("animations/test.act", FS::Mode::CREATE_AND_WRITE, allocator);
		file.write(blob.getData(), blob.getPos());
		file.close();
	}


	void load()
	{
		IAllocator& allocator = m_app.getWorldEditor()->getAllocator();
		FS::OsFile file;
		file.open("animations/test.act", FS::Mode::OPEN_AND_READ, allocator);
		Array<uint8> data(allocator);
		data.resize((int)file.size());
		file.read(&data[0], data.size());
		InputBlob blob(&data[0], data.size());
		m_resource->deserialize(blob, m_app.getWorldEditor()->getEngine(), allocator);
		file.close();
	}


	void onWindowGUI() override
	{
		if (!m_opened) return;
		if (ImGui::BeginDock("Animation Editor"))
		{
			if (ImGui::Button("Create state")) createState();
			ImGui::SameLine();
			if (ImGui::Button("Save")) save();
			ImGui::SameLine();
			if (ImGui::Button("Load")) load();
			ImGui::Columns(2);
			drawGraph();
			ImGui::NextColumn();
			ImGui::Text("Properties");
			m_resource->getRoot()->onGUI();
			ImGui::Columns();
		}
		ImGui::EndDock();

		/*if (ImGui::BeginDock("Animation set"))
		{
			//m_resource->
		}
		ImGui::EndDock();*/
	}


private:
	StudioApp& m_app;
	bool m_opened;
	ImVec2 m_offset;
	ControllerResource* m_resource;
};


} // namespace AnimEditor



namespace
{


struct AssetBrowserPlugin : AssetBrowser::IPlugin
{

	explicit AssetBrowserPlugin(StudioApp& app)
		: m_app(app)
	{
	}


	bool acceptExtension(const char* ext, Lumix::ResourceType type) const override
	{
		return type == ANIMATION_TYPE && equalStrings(ext, "anm");
	}


	bool onGUI(Lumix::Resource* resource, Lumix::ResourceType type) override
	{
		if (type == ANIMATION_TYPE)
		{
			auto* animation = static_cast<Animation*>(resource);
			ImGui::LabelText("FPS", "%d", animation->getFPS());
			ImGui::LabelText("Length", "%.3fs", animation->getLength());
			ImGui::LabelText("Frames", "%d", animation->getFrameCount());

			return true;
		}
		return false;
	}


	void onResourceUnloaded(Resource* resource) override {}


	const char* getName() const override { return "Animation"; }


	bool hasResourceManager(ResourceType type) const override { return type == ANIMATION_TYPE; }


	ResourceType getResourceType(const char* ext) override
	{
		if (equalStrings(ext, "ani")) return ANIMATION_TYPE;
		return INVALID_RESOURCE_TYPE;
	}


	StudioApp& m_app;
};


struct AnimControllerAssetBrowserPlugin : AssetBrowser::IPlugin
{

	explicit AnimControllerAssetBrowserPlugin(StudioApp& app)
		: m_app(app)
	{
	}


	bool acceptExtension(const char* ext, Lumix::ResourceType type) const override
	{
		return type == CONTROLLER_RESOURCE_TYPE && equalStrings(ext, "act");
	}


	bool onGUI(Lumix::Resource* resource, Lumix::ResourceType type) override
	{
		if (type == CONTROLLER_RESOURCE_TYPE)
		{
			auto* controller = static_cast<Anim::ControllerResource*>(resource);

			return true;
		}
		return false;
	}


	void onResourceUnloaded(Resource* resource) override {}


	const char* getName() const override { return "Animation Controller"; }


	bool hasResourceManager(ResourceType type) const override { return type == CONTROLLER_RESOURCE_TYPE; }


	ResourceType getResourceType(const char* ext) override
	{
		if (equalStrings(ext, "act")) return CONTROLLER_RESOURCE_TYPE;
		return INVALID_RESOURCE_TYPE;
	}


	StudioApp& m_app;
};


struct PropertyGridPlugin : PropertyGrid::IPlugin
{
	explicit PropertyGridPlugin(StudioApp& app)
		: m_app(app)
	{
		m_is_playing = false;
	}


	void onGUI(PropertyGrid& grid, Lumix::ComponentUID cmp) override
	{
		if (cmp.type != ANIMABLE_HASH) return;

		auto* scene = static_cast<AnimationScene*>(cmp.scene);
		auto* animation = scene->getAnimableAnimation(cmp.handle);
		if (!animation) return;
		if (!animation->isReady()) return;

		ImGui::Checkbox("Preview", &m_is_playing);
		float time = scene->getAnimableTime(cmp.handle);
		if (ImGui::SliderFloat("Time", &time, 0, animation->getLength()))
		{
			scene->setAnimableTime(cmp.handle, time);
			scene->updateAnimable(cmp.handle, 0);
		}

		if (m_is_playing)
		{
			float time_delta = m_app.getWorldEditor()->getEngine().getLastTimeDelta();
			scene->updateAnimable(cmp.handle, time_delta);
		}
	}


	StudioApp& m_app;
	bool m_is_playing;
};


} // anonymous namespace


LUMIX_STUDIO_ENTRY(animation)
{
	app.registerComponentWithResource("animable", "Animation/Animable", ANIMATION_TYPE, "Animation");
	app.registerComponentWithResource("anim_controller", "Animation/Controller", CONTROLLER_RESOURCE_TYPE, "Source");

	auto& allocator = app.getWorldEditor()->getAllocator();
	auto* ab_plugin = LUMIX_NEW(allocator, AssetBrowserPlugin)(app);
	app.getAssetBrowser()->addPlugin(*ab_plugin);

	auto* anim_controller_ab_plugin = LUMIX_NEW(allocator, AnimControllerAssetBrowserPlugin)(app);
	app.getAssetBrowser()->addPlugin(*anim_controller_ab_plugin);

	auto* pg_plugin = LUMIX_NEW(allocator, PropertyGridPlugin)(app);
	app.getPropertyGrid()->addPlugin(*pg_plugin);

	app.addPlugin(*LUMIX_NEW(allocator, AnimEditor::AnimationEditor)(app));
}

