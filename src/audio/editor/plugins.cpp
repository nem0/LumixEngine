#include "audio_system.h"
#include "audio_device.h"
#include "audio_scene.h"
#include "clip_manager.h"
#include "editor/asset_browser.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include "renderer/render_scene.h"


using namespace Lumix;


namespace
{


static const Lumix::uint32 CLIP_HASH = Lumix::crc32("CLIP");


struct AssetBrowserPlugin : public AssetBrowser::IPlugin
{
	explicit AssetBrowserPlugin(StudioApp& app)
		: m_app(app)
		, m_browser(*app.getAssetBrowser())
		, m_playing_clip(-1)
	{
	}


	Lumix::AudioDevice& getAudioDevice(Lumix::Engine& engine)
	{
		auto* audio = static_cast<Lumix::AudioSystem*>(engine.getPluginManager().getPlugin("audio"));
		return audio->getDevice();
	}


	bool acceptExtension(const char* ext, Lumix::uint32 type) const override { return false; }


	void stopAudio()
	{
		if (m_playing_clip < 0) return;

		getAudioDevice(m_app.getWorldEditor()->getEngine()).stop(m_playing_clip);
		m_playing_clip = -1;
	}


	const char* getName() const override { return "Audio"; }


	bool onGUI(Lumix::Resource* resource, Lumix::uint32 type) override
	{
		if (type != CLIP_HASH) return false;
		auto* clip = static_cast<Lumix::Clip*>(resource);
		ImGui::LabelText("Length", "%f", clip->getLengthSeconds());
		auto& device = getAudioDevice(m_app.getWorldEditor()->getEngine());

		if (m_playing_clip >= 0)
		{
			if (ImGui::Button("Stop"))
			{
				stopAudio();
				return true;
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
			device.play(handle, false);
			m_playing_clip = handle;
		}
		return true;
	}


	void onResourceUnloaded(Lumix::Resource*) override { stopAudio(); }


	bool hasResourceManager(Lumix::uint32 type) const override { return type == CLIP_HASH; }


	Lumix::uint32 getResourceType(const char* ext) override
	{
		if (Lumix::equalStrings(ext, "ogg")) return CLIP_HASH;
		return 0;
	}

	int m_playing_clip;
	StudioApp& m_app;
	AssetBrowser& m_browser;
};


struct StudioAppPlugin : public StudioApp::IPlugin
{
	explicit StudioAppPlugin(StudioApp& app)
		: m_app(app)
	{
		m_filter[0] = 0;
		m_is_opened = false;
		m_action = LUMIX_NEW(app.getWorldEditor()->getAllocator(), Action)("Clip manager", "clip_manager");
		m_action->func.bind<StudioAppPlugin, &StudioAppPlugin::onAction>(this);
	}


	void onAction() { m_is_opened = !m_is_opened; }


	void onWindowGUI() override
	{
		if (ImGui::BeginDock("Clip Manager", &m_is_opened))
		{
			ImGui::InputText("Filter", m_filter, Lumix::lengthOf(m_filter));

			auto* audio_scene =
				static_cast<Lumix::AudioScene*>(m_app.getWorldEditor()->getScene(Lumix::crc32("audio")));
			int clip_count = audio_scene->getClipCount();
			for (int clip_id = 0; clip_id < clip_count; ++clip_id)
			{
				auto* clip_info = audio_scene->getClipInfo(clip_id);

				if (m_filter[0] != 0 && Lumix::stristr(clip_info->name, m_filter) == 0)
				{
					continue;
				}

				if (ImGui::TreeNode((const void*)(uintptr)clip_id, "%s", clip_info->name))
				{
					char buf[30];
					Lumix::copyString(buf, Lumix::lengthOf(buf), clip_info->name);
					if (ImGui::InputText("Name", buf, sizeof(buf)))
					{
						Lumix::copyString(clip_info->name, buf);
						clip_info->name_hash = Lumix::crc32(buf);
					}
					auto* clip = audio_scene->getClipInfo(clip_id)->clip;
					char path[Lumix::MAX_PATH_LENGTH];
					Lumix::copyString(path, clip ? clip->getPath().c_str() : "");
					if (m_app.getAssetBrowser()->resourceInput("Clip", "", path, Lumix::lengthOf(path), CLIP_HASH))
					{
						audio_scene->setClip(clip_id, Lumix::Path(path));
					}
					bool looped = audio_scene->getClipInfo(clip_id)->looped;
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
				audio_scene->addClip("test", Lumix::Path("test.ogg"));
			}
		}
		ImGui::EndDock();
	}


	StudioApp& m_app;
	char m_filter[256];
	bool m_is_opened;
};


struct EditorPlugin : public WorldEditor::Plugin
{
	explicit EditorPlugin(WorldEditor& editor)
		: m_editor(editor)
	{
	}

	bool showGizmo(ComponentUID cmp) override
	{
		static const uint32 ECHO_ZONE_HASH = crc32("echo_zone");

		if (cmp.type == ECHO_ZONE_HASH)
		{
			auto* audio_scene = static_cast<AudioScene*>(cmp.scene);
			float radius = audio_scene->getEchoZoneRadius(cmp.index);
			Universe& universe = audio_scene->getUniverse();
			Vec3 pos = universe.getPosition(cmp.entity);

			auto* scene = static_cast<RenderScene*>(m_editor.getScene(crc32("renderer")));
			if (!scene) return true;
			scene->addDebugSphere(pos, radius, 0xff0000ff, 0);
			return true;
		}

		return false;
	}

	WorldEditor& m_editor;
};

} // anonymous namespace


LUMIX_STUDIO_ENTRY(audio)
{
	auto& editor = *app.getWorldEditor();
	auto& allocator = editor.getAllocator();

	auto* asset_browser_plugin = LUMIX_NEW(allocator, AssetBrowserPlugin)(app);
	app.getAssetBrowser()->addPlugin(*asset_browser_plugin);

	auto* app_plugin = LUMIX_NEW(allocator, StudioAppPlugin)(app);
	app.addPlugin(*app_plugin);

	auto* plugin = LUMIX_NEW(allocator, EditorPlugin)(editor);
	editor.addPlugin(*plugin);
}
