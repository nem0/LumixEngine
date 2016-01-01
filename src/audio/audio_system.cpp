#include "audio_system.h"
#include "audio_device.h"
#include "audio_scene.h"
#include "clip_manager.h"
#include "core/crc32.h"
#include "core/path.h"
#include "core/resource_manager.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "engine/plugin_manager.h"
#include "studio_lib/asset_browser.h"
#include "studio_lib/imgui/imgui.h"
#include "studio_lib/studio_app.h"
#include "studio_lib/utils.h"


static const Lumix::uint32 CLIP_HASH = Lumix::crc32("CLIP");


namespace Lumix
{


struct AudioSystemImpl : public AudioSystem
{
	AudioSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_manager(engine.getAllocator())
		, m_device(nullptr)
	{
	}


	AudioDevice& getDevice() override { return *m_device; }


	ClipManager& getClipManager() override { return m_manager; }


	bool create() override
	{
		m_device = AudioDevice::create(m_engine);
		if (!m_device) return false;
		m_manager.create(CLIP_HASH, m_engine.getResourceManager());
		return true;
	}


	void destroy() override
	{
		AudioDevice::destroy(*m_device);
		m_manager.destroy();
	}


	const char* getName() const override { return "audio"; }


	IScene* createScene(UniverseContext& ctx)
	{
		return AudioScene::createInstance(*this, ctx, m_engine.getAllocator());
	}


	void destroyScene(IScene* scene) { AudioScene::destroyInstance(static_cast<AudioScene*>(scene)); }


	ClipManager m_manager;
	Engine& m_engine;
	AudioDevice* m_device;
};


} // namespace Lumix


extern "C" LUMIX_LIBRARY_EXPORT void setStudioApp(StudioApp& app)
{
	struct AssetBrowserPlugin : public AssetBrowser::IPlugin
	{
		AssetBrowserPlugin(StudioApp& app)
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


		void stopAudio()
		{
			if (m_playing_clip < 0) return;

			getAudioDevice(m_app.getWorldEditor()->getEngine()).stop(m_playing_clip);
			m_playing_clip = -1;
		}


		const char* getName() const override
		{
			return "Audio";
		}


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

				auto handle = device.createBuffer(
					clip->getData(), clip->getSize(), clip->getChannels(), clip->getSampleRate(), 0);
				device.play(handle, false);
				m_playing_clip = handle;
			}			
			return true;
		}


		void onResourceUnloaded(Lumix::Resource*) override
		{
			stopAudio();
		}


		bool hasResourceManager(Lumix::uint32 type) const override
		{
			return type == CLIP_HASH;
		}


		Lumix::uint32 getResourceType(const char* ext) override
		{
			if (Lumix::compareString(ext, "ogg") == 0) return CLIP_HASH;
			return 0;
		}

		int m_playing_clip;
		StudioApp& m_app;
		AssetBrowser& m_browser;
	};


	auto* asset_browser_plugin = LUMIX_NEW(app.getWorldEditor()->getAllocator(), AssetBrowserPlugin)(app);
	app.getAssetBrowser()->addPlugin(*asset_browser_plugin);
	
	struct Plugin : public StudioApp::IPlugin
	{
		Plugin(StudioApp& app)
			: m_app(app)
		{
			m_filter[0] = 0;
			m_is_opened = false;
			m_action = LUMIX_NEW(app.getWorldEditor()->getAllocator(), Action)("Clip manager", "clip_manager");
			m_action->func.bind<Plugin, &Plugin::onAction>(this);
		}


		void onAction()
		{
			m_is_opened = !m_is_opened;
		}


		void onWindowGUI() override
		{
			if (ImGui::BeginDock("Clip manager", &m_is_opened))
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

					if (ImGui::TreeNode((const void*)clip_id, clip_info->name))
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
						int type = m_app.getAssetBrowser()->getTypeFromResourceManagerType(CLIP_HASH);
						if (m_app.getAssetBrowser()->resourceInput(
								"Clip", "", path, Lumix::lengthOf(path), type))
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


		const char* getWindowName() override
		{
			return "Clip manager";
		}


		StudioApp& m_app;
		char m_filter[256];
		bool m_is_opened;
	};


	auto* app_plugin = LUMIX_NEW(app.getWorldEditor()->getAllocator(), Plugin)(app);
	app.addPlugin(*app_plugin);
}


extern "C" LUMIX_LIBRARY_EXPORT Lumix::IPlugin* createPlugin(Lumix::Engine& engine)
{
	return LUMIX_NEW(engine.getAllocator(), Lumix::AudioSystemImpl)(engine);
}
