#include "animation/animation.h"
#include "animation/animation_scene.h"
#include "animation/controller.h"
#include "animation/property_animation.h"
#include "animation/editor/animation_editor.h"
#include "editor/asset_browser.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/hash_map.h"
#include "engine/reflection.h"
#include "engine/system.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "renderer/render_scene.h"


using namespace Lumix;


static const ComponentType ANIMABLE_TYPE = Reflection::getComponentType("animable");
static const ComponentType CONTROLLER_TYPE = Reflection::getComponentType("anim_controller");
static const ComponentType RENDERABLE_TYPE = Reflection::getComponentType("renderable");


namespace
{


struct AnimationAssetBrowserPlugin : AssetBrowser::IPlugin
{
	explicit AnimationAssetBrowserPlugin(StudioApp& app)
		: m_app(app)
	{
	}


	bool acceptExtension(const char* ext, ResourceType type) const override
	{
		return type == Animation::TYPE && equalStrings(ext, "anm");
	}


	bool onGUI(Resource* resource, ResourceType type) override
	{
		if (type == Animation::TYPE)
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


	bool hasResourceManager(ResourceType type) const override { return type == Animation::TYPE; }


	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		if (type == Animation::TYPE) return copyFile("models/editor/tile_animation.dds", out_path);
		return false;
	}

	ResourceType getResourceType(const char* ext) override
	{
		if (equalStrings(ext, "ani")) return Animation::TYPE;
		return INVALID_RESOURCE_TYPE;
	}


	StudioApp& m_app;
};


struct PropertyAnimationAssetBrowserPlugin : AssetBrowser::IPlugin
{
	explicit PropertyAnimationAssetBrowserPlugin(StudioApp& app)
		: m_app(app)
	{
	}


	bool acceptExtension(const char* ext, ResourceType type) const override
	{
		return type == PropertyAnimation::TYPE && equalStrings(ext, "anp");
	}


	bool onGUI(Resource* resource, ResourceType type) override
	{
		if (type == PropertyAnimation::TYPE)
		{
			auto* animation = static_cast<PropertyAnimation*>(resource);
			return true;
		}
		return false;
	}


	void onResourceUnloaded(Resource* resource) override {}


	const char* getName() const override { return "Property animation"; }


	bool hasResourceManager(ResourceType type) const override { return type == PropertyAnimation::TYPE; }


	ResourceType getResourceType(const char* ext) override
	{
		if (equalStrings(ext, "anp")) return PropertyAnimation::TYPE;
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


	bool acceptExtension(const char* ext, ResourceType type) const override
	{
		return type == Anim::ControllerResource::TYPE && equalStrings(ext, "act");
	}


	bool onGUI(Resource* resource, ResourceType type) override
	{
		if (type == Anim::ControllerResource::TYPE)
		{
			return true;
		}
		return false;
	}


	void onResourceUnloaded(Resource* resource) override {}


	const char* getName() const override { return "Animation Controller"; }


	bool hasResourceManager(ResourceType type) const override { return type == Anim::ControllerResource::TYPE; }


	ResourceType getResourceType(const char* ext) override
	{
		if (equalStrings(ext, "act")) return Anim::ControllerResource::TYPE;
		return INVALID_RESOURCE_TYPE;
	}


	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		if (type == Anim::ControllerResource::TYPE) return copyFile("models/editor/tile_animation_graph.dds", out_path);
		return false;
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


	void onGUI(PropertyGrid& grid, ComponentUID cmp) override
	{
		if (cmp.type != ANIMABLE_TYPE) return;

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
			float time_delta = m_app.getWorldEditor().getEngine().getLastTimeDelta();
			scene->updateAnimable(cmp.handle, time_delta);
		}

		if (ImGui::CollapsingHeader("Transformation"))
		{
			auto* render_scene = (RenderScene*)scene->getUniverse().getScene(RENDERABLE_TYPE);
			ComponentHandle renderable = render_scene->getComponent(cmp.entity, RENDERABLE_TYPE);
			if (renderable.isValid())
			{
				const Pose* pose = render_scene->lockPose(renderable);
				Model* model = render_scene->getModelInstanceModel(renderable);
				if (pose && model)
				{
					ImGui::Columns(3);
					for (int i = 0; i < pose->count; ++i)
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
				if (pose) render_scene->unlockPose(renderable, false);
			}
		}
	}


	StudioApp& m_app;
	bool m_is_playing;
};


} // anonymous namespace


LUMIX_STUDIO_ENTRY(animation)
{
	app.registerComponent("property_animator", "Animation/Property animator");
	app.registerComponentWithResource("animable", "Animation/Animable", Animation::TYPE, *Reflection::getProperty(ANIMABLE_TYPE, "Animation"));
	app.registerComponentWithResource("anim_controller", "Animation/Controller", Anim::ControllerResource::TYPE, *Reflection::getProperty(CONTROLLER_TYPE, "Source"));
	app.registerComponent("shared_anim_controller", "Animation/Shared controller");

	auto& allocator = app.getWorldEditor().getAllocator();
	auto* anim_ab_plugin = LUMIX_NEW(allocator, AnimationAssetBrowserPlugin)(app);
	app.getAssetBrowser().addPlugin(*anim_ab_plugin);

	auto* prop_anim_ab_plugin = LUMIX_NEW(allocator, PropertyAnimationAssetBrowserPlugin)(app);
	app.getAssetBrowser().addPlugin(*prop_anim_ab_plugin);

	auto* anim_controller_ab_plugin = LUMIX_NEW(allocator, AnimControllerAssetBrowserPlugin)(app);
	app.getAssetBrowser().addPlugin(*anim_controller_ab_plugin);

	auto* pg_plugin = LUMIX_NEW(allocator, PropertyGridPlugin)(app);
	app.getPropertyGrid().addPlugin(*pg_plugin);

	app.addPlugin(*AnimEditor::IAnimationEditor::create(allocator, app));
}

