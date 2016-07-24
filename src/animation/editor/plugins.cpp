#include "animation/animation_system.h"
#include "animation/animation.h"
#include "engine/crc32.h"
#include "engine/property_register.h"
#include "engine/resource_manager.h"
#include "editor/asset_browser.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "imgui/imgui.h"


using namespace Lumix;


namespace
{


static const ComponentType ANIMABLE_HASH = PropertyRegister::getComponentType("animable");
static const ResourceType ANIMATION_TYPE("animation");


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
	app.registerComponentWithResource("animable", "Animable", ANIMATION_TYPE, "Animation");

	auto& allocator = app.getWorldEditor()->getAllocator();
	auto* ab_plugin = LUMIX_NEW(allocator, AssetBrowserPlugin)(app);
	app.getAssetBrowser()->addPlugin(*ab_plugin);

	auto* pg_plugin = LUMIX_NEW(allocator, PropertyGridPlugin)(app);
	app.getPropertyGrid()->addPlugin(*pg_plugin);
}

