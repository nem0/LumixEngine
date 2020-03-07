#include <imgui/imgui.h>

#include "audio_device.h"
#include "audio_scene.h"
#include "audio_system.h"
#include "clip.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/reflection.h"
#include "engine/universe.h"
#include "renderer/render_scene.h"


using namespace Lumix;


namespace
{


struct AssetBrowserPlugin final : AssetBrowser::IPlugin
{
	explicit AssetBrowserPlugin(StudioApp& app)
		: m_app(app)
		, m_browser(app.getAssetBrowser())
		, m_playing_clip(-1)
	{
		app.getAssetCompiler().registerExtension("ogg", Clip::TYPE);
	}


	static AudioDevice& getAudioDevice(Engine& engine)
	{
		auto* audio = static_cast<AudioSystem*>(engine.getPluginManager().getPlugin("audio"));
		return audio->getDevice();
	}


	void stopAudio()
	{
		if (m_playing_clip < 0) return;

		getAudioDevice(m_app.getEngine()).stop(m_playing_clip);
		m_playing_clip = -1;
	}


	const char* getName() const override { return "Audio"; }


	void onGUI(Span<Resource*> resources) override
	{
		if(resources.length() > 1) return;

		auto* clip = static_cast<Clip*>(resources[0]);
		ImGui::LabelText("Length", "%f", clip->getLengthSeconds());
		auto& device = getAudioDevice(m_app.getEngine());

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
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (type == Clip::TYPE) return fs.copyFile("models/editor/tile_audio.dds", out_path);
		return false;
	}


	int m_playing_clip;
	StudioApp& m_app;
	AssetBrowser& m_browser;
};


struct ClipManagerUI final : StudioApp::GUIPlugin
{
	explicit ClipManagerUI(StudioApp& app)
		: m_app(app)
	{
		m_filter[0] = 0;
		m_is_open = false;
		Action* action = LUMIX_NEW(app.getAllocator(), Action)("Clip manager", "Toggle clip manager", "clip_manager");
		action->func.bind<&ClipManagerUI::onAction>(this);
		action->is_selected.bind<&ClipManagerUI::isOpen>(this);
		app.addWindowAction(action);
	}

	// TODO
	/*
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
	*/

	const char* getName() const override { return "audio"; }


	bool isOpen() const { return m_is_open; }
	void onAction() { m_is_open = !m_is_open; }


	void onWindowGUI() override
	{
		if (!m_is_open) return; 

		if (ImGui::Begin("Clip Manager", &m_is_open)) {
			ImGui::InputText("Filter", m_filter, sizeof(m_filter));

			Universe* universe = m_app.getWorldEditor().getUniverse();
			auto* audio_scene = static_cast<AudioScene*>(universe->getScene(crc32("audio")));
			u32 clip_count = audio_scene->getClipCount();
			for (u32 clip_id = 0; clip_id < clip_count; ++clip_id) {
				AudioScene::ClipInfo* clip_info = audio_scene->getClipInfoByIndex(clip_id);
				if (!clip_info) continue;

				if (m_filter[0] != 0 && stristr(clip_info->name, m_filter) == nullptr) {
					continue;
				}

				if (ImGui::TreeNode((const void*)(uintptr)clip_id, "%s", clip_info->name)) {
					if (ImGui::InputText("Name", clip_info->name, sizeof(clip_info->name))) {
						clip_info->name_hash = crc32(clip_info->name);
					}
					char path[MAX_PATH_LENGTH];
					copyString(path, clip_info->clip ? clip_info->clip->getPath().c_str() : "");
					if (m_app.getAssetBrowser().resourceInput("Clip", "clip", Span(path), Clip::TYPE)) {
						audio_scene->setClip(clip_id, Path(path));
					}
					ImGui::InputFloat("Volume", &clip_info->volume);
					ImGui::Checkbox("Looped", &clip_info->looped);
					if (ImGui::Button("Remove")) {
						audio_scene->removeClip(clip_info);
						--clip_count;
					}
					ImGui::TreePop();
				}
			}

			if (ImGui::Button("Add")) {
				audio_scene->addClip("test", Path("test.ogg"));
			}
		}
		ImGui::End();
	}


	StudioApp& m_app;
	char m_filter[256];
	bool m_is_open;
};


struct StudioAppPlugin : StudioApp::IPlugin
{
	explicit StudioAppPlugin(StudioApp& app)
		: m_app(app)
	{
	}


	const char* getName() const override { return "audio"; }


	void init() override 
	{
		m_app.registerComponent("ambient_sound", "Audio / Ambient sound");
		m_app.registerComponent("audio_listener", "Audio / Listener");
		m_app.registerComponent("echo_zone", "Audio / Echo zone");
		m_app.registerComponent("chorus_zone", "Audio / Chorus zone");

		IAllocator& allocator = m_app.getAllocator();

		m_asset_browser_plugin = LUMIX_NEW(allocator, AssetBrowserPlugin)(m_app);
		m_app.getAssetBrowser().addPlugin(*m_asset_browser_plugin);

		m_clip_manager_ui = LUMIX_NEW(allocator, ClipManagerUI)(m_app);
		m_app.addPlugin(*m_clip_manager_ui);
	}


	bool showGizmo(ComponentUID cmp) override
	{
		static const ComponentType ECHO_ZONE_TYPE = Reflection::getComponentType("echo_zone");
		static const ComponentType CHORUS_ZONE_TYPE = Reflection::getComponentType("chorus_zone");

		const EntityRef entity = (EntityRef)cmp.entity;
		if (cmp.type == ECHO_ZONE_TYPE)
		{
			auto* audio_scene = static_cast<AudioScene*>(cmp.scene);
			float radius = audio_scene->getEchoZone(entity).radius;
			Universe& universe = audio_scene->getUniverse();
			const DVec3 pos = universe.getPosition(entity);

			auto* scene = static_cast<RenderScene*>(universe.getScene(crc32("renderer")));
			if (!scene) return true;
			scene->addDebugSphere(pos, radius, 0xff0000ff);
			return true;
		}
		else if (cmp.type == CHORUS_ZONE_TYPE)
		{
			auto* audio_scene = static_cast<AudioScene*>(cmp.scene);
			float radius = audio_scene->getChorusZone(entity).radius;
			Universe& universe = audio_scene->getUniverse();
			const DVec3 pos = universe.getPosition(entity);

			auto* scene = static_cast<RenderScene*>(universe.getScene(crc32("renderer")));
			if (!scene) return true;
			scene->addDebugSphere(pos, radius, 0xff0000ff);
			return true;
		}

		return false;
	}


	~StudioAppPlugin()
	{
		IAllocator& allocator = m_app.getAllocator();

		m_app.getAssetBrowser().removePlugin(*m_asset_browser_plugin);
		m_app.removePlugin(*m_clip_manager_ui);

		LUMIX_DELETE(allocator, m_asset_browser_plugin);
		LUMIX_DELETE(allocator, m_clip_manager_ui);
	}


	StudioApp& m_app;
	AssetBrowserPlugin* m_asset_browser_plugin;
	ClipManagerUI* m_clip_manager_ui;
};


} // anonymous namespace


LUMIX_STUDIO_ENTRY(audio)
{
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}
