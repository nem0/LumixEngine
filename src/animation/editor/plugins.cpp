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
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/reflection.h"
#include "engine/system.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "renderer/render_scene.h"


using namespace Lumix;


static const ComponentType ANIMABLE_TYPE = Reflection::getComponentType("animable");
static const ComponentType PROPERTY_ANIMATOR_TYPE = Reflection::getComponentType("property_animator");
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


	void ShowAddCurveMenu(PropertyAnimation* animation)
	{
		WorldEditor& editor = m_app.getWorldEditor();
		auto& selected_entities = editor.getSelectedEntities();
		if (selected_entities.empty()) return;
			
		if (!ImGui::BeginMenu("Add curve")) return;

		Universe* universe = editor.getUniverse();
		;
		for (ComponentUID cmp = universe->getFirstComponent(selected_entities[0]); cmp.isValid(); cmp = universe->getNextComponent(cmp))
		{
			const char* cmp_type_name = m_app.getComponentTypeName(cmp.type);
			if (!ImGui::BeginMenu(cmp_type_name)) continue;

			const Reflection::ComponentBase* component = Reflection::getComponent(cmp.type);
			struct : Reflection::ISimpleComponentVisitor
			{
				void visitProperty(const Reflection::PropertyBase& prop) override {}
				void visit(const Reflection::Property<float>& prop) override
				{
					int idx = animation->curves.find([&](PropertyAnimation::Curve& rhs) {
						return rhs.cmp_type == cmp.type && rhs.property == &prop;
					});
					if (idx < 0 &&ImGui::MenuItem(prop.name))
					{
						PropertyAnimation::Curve& curve = animation->addCurve();
						curve.cmp_type = cmp.type;
						curve.property = &prop;
						curve.frames.push(0);
						curve.frames.push(animation->curves.size() > 1 ? animation->curves[0].frames.back() : 1);
						curve.values.push(0);
						curve.values.push(0);
					}
				}
				PropertyAnimation* animation;
				ComponentUID cmp;
			} visitor;

			visitor.animation = animation;
			visitor.cmp = cmp;
			component->visit(visitor);

			ImGui::EndMenu();
		}

		ImGui::EndMenu();
	}


	void savePropertyAnimation(PropertyAnimation& anim)
	{
		if (FS::IFile* file = m_app.getAssetBrowser().beginSaveResource(anim))
		{
			bool success = true;
			JsonSerializer serializer(*file, anim.getPath());
			if (!anim.save(serializer))
			{
				success = false;
				g_log_error.log("Editor") << "Could not save file " << anim.getPath().c_str();
			}
			m_app.getAssetBrowser().endSaveResource(anim, *file, success);
		}
	}


	bool onGUI(Resource* resource, ResourceType type) override
	{
		if (type == PropertyAnimation::TYPE)
		{
			auto* animation = static_cast<PropertyAnimation*>(resource);
			if (!animation->isReady()) return true;

			if (ImGui::Button("Save")) savePropertyAnimation(*animation);
			ImGui::SameLine();
			if (ImGui::Button("Open in external editor")) m_app.getAssetBrowser().openInExternalEditor(animation);
			
			ShowAddCurveMenu(animation);

			if (!animation->curves.empty())
			{
				int frames = animation->curves[0].frames.back();
				if (ImGui::InputInt("Frames", &frames))
				{
					for (auto& curve : animation->curves)
					{
						curve.frames.back() = frames;
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
			if (m_selected_curve < 0) return true;

			ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() - 20);
			static ImVec2 size(-1, 200);
			
			PropertyAnimation::Curve& curve = animation->curves[m_selected_curve];
			ImVec2 points[16];
			ASSERT(curve.frames.size() < lengthOf(points));
			for (int i = 0; i < curve.frames.size(); ++i)
			{
				points[i].x = (float)curve.frames[i];
				points[i].y = curve.values[i];
			}
			int new_count;
			int last_frame = curve.frames.back();
			int flags = (int)ImGui::CurveEditorFlags::NO_TANGENTS | (int)ImGui::CurveEditorFlags::SHOW_GRID;
			if (m_fit_curve_in_editor)
			{
				flags |= (int)ImGui::CurveEditorFlags::RESET;
				m_fit_curve_in_editor = false;
			}
			int changed = ImGui::CurveEditor("curve", (float*)points, curve.frames.size(), size, flags, &new_count, &m_selected_point);
			if (changed >= 0)
			{
				curve.frames[changed] = int(points[changed].x + 0.5f);
				curve.values[changed] = points[changed].y;
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
				ImGui::InputInt("Frame", &curve.frames[m_selected_point]);
				ImGui::InputFloat("Value", &curve.values[m_selected_point]);
			}

			ImGui::HSplitter("sizer", &size);
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


	int m_selected_point = -1;
	int m_selected_curve = -1;
	bool m_fit_curve_in_editor = false;
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
		auto* animation = scene->getAnimableAnimation(cmp.entity);
		if (!animation) return;
		if (!animation->isReady()) return;

		ImGui::Checkbox("Preview", &m_is_playing);
		float time = scene->getAnimableTime(cmp.entity);
		if (ImGui::SliderFloat("Time", &time, 0, animation->getLength()))
		{
			scene->setAnimableTime(cmp.entity, time);
			scene->updateAnimable(cmp.entity, 0);
		}

		if (m_is_playing)
		{
			float time_delta = m_app.getWorldEditor().getEngine().getLastTimeDelta();
			scene->updateAnimable(cmp.entity, time_delta);
		}

		if (ImGui::CollapsingHeader("Transformation"))
		{
			auto* render_scene = (RenderScene*)scene->getUniverse().getScene(RENDERABLE_TYPE);
			if (scene->getUniverse().hasComponent(cmp.entity, RENDERABLE_TYPE))
			{
				const Pose* pose = render_scene->lockPose(cmp.entity);
				Model* model = render_scene->getModelInstanceModel(cmp.entity);
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
				if (pose) render_scene->unlockPose(cmp.entity, false);
			}
		}
	}


	StudioApp& m_app;
	bool m_is_playing;
};


} // anonymous namespace


LUMIX_STUDIO_ENTRY(animation)
{
	app.registerComponentWithResource("property_animator", "Animation/Property animator", PropertyAnimation::TYPE, *Reflection::getProperty(PROPERTY_ANIMATOR_TYPE, "Animation"));
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

