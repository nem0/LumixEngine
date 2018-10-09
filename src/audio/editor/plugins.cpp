#include "animation/editor/animation_editor.h"
#include "audio_device.h"
#include "audio_scene.h"
#include "audio_system.h"
#include "clip_manager.h"
#include "editor/asset_browser.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "engine/reflection.h"
#include "engine/system.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include "renderer/render_scene.h"


using namespace Lumix;


namespace
{


struct AssetBrowserPlugin final : public AssetBrowser::IPlugin
{
	explicit AssetBrowserPlugin(StudioApp& app)
		: m_app(app)
		, m_browser(app.getAssetBrowser())
		, m_playing_clip(-1)
	{
		app.getAssetBrowser().registerExtension("ogg", Clip::TYPE);
	}


	static AudioDevice& getAudioDevice(Engine& engine)
	{
		auto* audio = static_cast<AudioSystem*>(engine.getPluginManager().getPlugin("audio"));
		return audio->getDevice();
	}


	void stopAudio()
	{
		if (m_playing_clip < 0) return;

		getAudioDevice(m_app.getWorldEditor().getEngine()).stop(m_playing_clip);
		m_playing_clip = -1;
	}


	const char* getName() const override { return "Audio"; }


	void onGUI(Resource* resource) override
	{
		auto* clip = static_cast<Clip*>(resource);
		ImGui::LabelText("Length", "%f", clip->getLengthSeconds());
		auto& device = getAudioDevice(m_app.getWorldEditor().getEngine());

		if (m_playing_clip >= 0)
		{
			if (ImGui::Button("Stop"))
			{
				stopAudio();
				return;
			}
			float time = device.getCurrentTime(m_playing_clip);
			if (ImGui::SliderFloat("Time", &time, 0, clip->getLengthSeconds(), "%.2fs"))
			{
				device.setCurrentTime(m_playing_clip, time);
			}
		}

		if (m_playing_clip < 0 && ImGui::Button("Play"))
		{
			stopAudio();

			auto handle =
				device.createBuffer(clip->getData(), clip->getSize(), clip->getChannels(), clip->getSampleRate(), 0);
			device.play(handle, true);
			m_playing_clip = handle;
		}
	}


	void onResourceUnloaded(Resource*) override { stopAudio(); }


	ResourceType getResourceType() const override { return Clip::TYPE; }
	

	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		if (type == Clip::TYPE) return copyFile("models/editor/tile_audio.dds", out_path);
		return false;
	}


	int m_playing_clip;
	StudioApp& m_app;
	AssetBrowser& m_browser;
};


struct ClipManagerUI final : public StudioApp::GUIPlugin
{
	explicit ClipManagerUI(StudioApp& app)
		: m_app(app)
	{
		m_filter[0] = 0;
		m_is_open = false;
		Action* action = LUMIX_NEW(app.getWorldEditor().getAllocator(), Action)("Clip manager", "Toggle clip manager", "clip_manager");
		action->func.bind<ClipManagerUI, &ClipManagerUI::onAction>(this);
		action->is_selected.bind<ClipManagerUI, &ClipManagerUI::isOpen>(this);
		app.addWindowAction(action);
	}


	void pluginAdded(GUIPlugin& plugin) override
	{
		if (!equalStrings(plugin.getName(), "animation_editor")) return;

		auto& anim_editor = (AnimEditor::IAnimationEditor&)plugin;
		auto& event_type = anim_editor.createEventType("sound");
		event_type.size = sizeof(SoundAnimationEvent);
		event_type.label = "Sound";
		event_type.editor.bind<ClipManagerUI, &ClipManagerUI::onSoundEventGUI>(this);
	}


	void onSoundEventGUI(u8* data, AnimEditor::Component& component) const
	{
		auto* ev = (SoundAnimationEvent*)data;
		AudioScene* scene = (AudioScene*)m_app.getWorldEditor().getUniverse()->getScene(crc32("audio"));
		auto getter = [](void* data, int idx, const char** out) -> bool {
			auto* scene = (AudioScene*)data;
			*out = scene->getClipName(idx);
			return true;
		};
		AudioScene::ClipInfo* clip = scene->getClipInfo(ev->clip);
		int current = clip ? scene->getClipInfoIndex(clip) : -1;

		if (ImGui::Combo("Clip", &current, getter, scene, scene->getClipCount()))
		{
			ev->clip = scene->getClipInfo(current)->name_hash;
		}
	}


	const char* getName() const override { return "audio"; }


	bool isOpen() const { return m_is_open; }
	void onAction() { m_is_open = !m_is_open; }


	void onWindowGUI() override
	{
		if (ImGui::Begin("Clip Manager", &m_is_open))
		{
			ImGui::InputText("Filter", m_filter, lengthOf(m_filter));

			auto universe = m_app.getWorldEditor().getUniverse();
			auto* audio_scene = static_cast<AudioScene*>(universe->getScene(crc32("audio")));
			int clip_count = audio_scene->getClipCount();
			for (int clip_id = 0; clip_id < clip_count; ++clip_id)
			{
				auto* clip_info = audio_scene->getClipInfo(clip_id);

				if (m_filter[0] != 0 && stristr(clip_info->name, m_filter) == nullptr)
				{
					continue;
				}

				if (ImGui::TreeNode((const void*)(uintptr)clip_id, "%s", clip_info->name))
				{
					char buf[30];
					copyString(buf, lengthOf(buf), clip_info->name);
					if (ImGui::InputText("Name", buf, sizeof(buf)))
					{
						copyString(clip_info->name, buf);
						clip_info->name_hash = crc32(buf);
					}
					auto* clip = audio_scene->getClipInfo(clip_id)->clip;
					char path[MAX_PATH_LENGTH];
					copyString(path, clip ? clip->getPath().c_str() : "");
					if (m_app.getAssetBrowser().resourceInput("Clip", "", path, lengthOf(path), Clip::TYPE))
					{
						audio_scene->setClip(clip_id, Path(path));
					}
					bool looped = audio_scene->getClipInfo(clip_id)->looped;
					ImGui::InputFloat("Volume", &clip_info->volume);
					if (ImGui::Checkbox("Looped", &looped))
					{
						clip_info->looped = looped;
					}
					if (ImGui::Button("Remove"))
					{
						audio_scene->removeClip(clip_info);
						--clip_count;
					}
					ImGui::TreePop();
				}
			}

			if (ImGui::Button("Add"))
			{
				audio_scene->addClip("test", Path("test.ogg"));
			}
		}
		ImGui::End();
	}


	StudioApp& m_app;
	char m_filter[256];
	bool m_is_open;
};


struct GizmoPlugin final : public WorldEditor::Plugin
{
	explicit GizmoPlugin(WorldEditor& editor)
		: m_editor(editor)
	{
	}

	bool showGizmo(ComponentUID cmp) override
	{
		static const ComponentType ECHO_ZONE_TYPE = Reflection::getComponentType("echo_zone");
		static const ComponentType CHORUS_ZONE_TYPE = Reflection::getComponentType("chorus_zone");

		const EntityRef entity = (EntityRef)cmp.entity;
		if (cmp.type == ECHO_ZONE_TYPE)
		{
			auto* audio_scene = static_cast<AudioScene*>(cmp.scene);
			float radius = audio_scene->getEchoZoneRadius(entity);
			Universe& universe = audio_scene->getUniverse();
			const DVec3 pos = universe.getPosition(entity);

			auto* scene = static_cast<RenderScene*>(universe.getScene(crc32("renderer")));
			if (!scene) return true;
			scene->addDebugSphere(pos, radius, 0xff0000ff, 0);
			return true;
		}
		else if (cmp.type == CHORUS_ZONE_TYPE)
		{
			auto* audio_scene = static_cast<AudioScene*>(cmp.scene);
			float radius = audio_scene->getChorusZoneRadius(entity);
			Universe& universe = audio_scene->getUniverse();
			const DVec3 pos = universe.getPosition(entity);

			auto* scene = static_cast<RenderScene*>(universe.getScene(crc32("renderer")));
			if (!scene) return true;
			scene->addDebugSphere(pos, radius, 0xff0000ff, 0);
			return true;
		}

		return false;
	}

	WorldEditor& m_editor;
};


struct StudioAppPlugin : StudioApp::IPlugin
{
	explicit StudioAppPlugin(StudioApp& app)
		: m_app(app)
	{
		app.registerComponent("ambient_sound", "Audio/Ambient sound");
		app.registerComponent("audio_listener", "Audio/Listener");
		app.registerComponent("echo_zone", "Audio/Echo zone");
		app.registerComponent("chorus_zone", "Audio/Chorus zone");

		WorldEditor& editor = app.getWorldEditor();
		IAllocator& allocator = editor.getAllocator();

		m_asset_browser_plugin = LUMIX_NEW(allocator, AssetBrowserPlugin)(app);
		app.getAssetBrowser().addPlugin(*m_asset_browser_plugin);

		m_clip_manager_ui = LUMIX_NEW(allocator, ClipManagerUI)(app);
		app.addPlugin(*m_clip_manager_ui);

		m_gizmo_plugin = LUMIX_NEW(allocator, GizmoPlugin)(editor);
		editor.addPlugin(*m_gizmo_plugin);
	}


	~StudioAppPlugin()
	{
		WorldEditor& editor = m_app.getWorldEditor();
		IAllocator& allocator = editor.getAllocator();

		m_app.getAssetBrowser().removePlugin(*m_asset_browser_plugin);
		m_app.getWorldEditor().removePlugin(*m_gizmo_plugin);
		m_app.removePlugin(*m_clip_manager_ui);

		LUMIX_DELETE(allocator, m_asset_browser_plugin);
		LUMIX_DELETE(allocator, m_gizmo_plugin);
		LUMIX_DELETE(allocator, m_clip_manager_ui);
	}


	StudioApp& m_app;
	AssetBrowserPlugin* m_asset_browser_plugin;
	ClipManagerUI* m_clip_manager_ui;
	GizmoPlugin* m_gizmo_plugin;
};


} // anonymous namespace


LUMIX_STUDIO_ENTRY(audio)
{
	WorldEditor& editor = app.getWorldEditor();
	IAllocator& allocator = editor.getAllocator();

	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}
