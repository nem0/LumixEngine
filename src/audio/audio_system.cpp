#include "audio_system.h"
#include "audio_device.h"
#include "audio_scene.h"
#include "clip_manager.h"
#include "core/crc32.h"
#include "core/path.h"
#include "core/resource_manager.h"
#include "editor/asset_browser.h"
#include "editor/imgui/imgui.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "engine/plugin_manager.h"
#include "engine/property_register.h"
#include "engine/property_descriptor.h"
#include "renderer/render_scene.h"


static const Lumix::uint32 CLIP_HASH = Lumix::crc32("CLIP");


namespace Lumix
{


static void registerProperties(Lumix::IAllocator& allocator)
{
	PropertyRegister::registerComponentType("ambient_sound", "Ambient sound");
	PropertyRegister::registerComponentType("audio_listener", "Audio listener");
	PropertyRegister::registerComponentType("echo_zone", "Echo zone");

	PropertyRegister::add("ambient_sound",
		LUMIX_NEW(allocator, EnumPropertyDescriptor<AudioScene>)("Sound",
			&AudioScene::getAmbientSoundClipIndex,
			&AudioScene::setAmbientSoundClipIndex,
			&AudioScene::getClipCount,
			&AudioScene::getClipName,
			allocator));

	PropertyRegister::add("ambient_sound",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<AudioScene>)("3D",
			&AudioScene::isAmbientSound3D,
			&AudioScene::setAmbientSound3D,
			allocator));

	PropertyRegister::add("echo_zone",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<AudioScene>)("Radius",
			&AudioScene::getEchoZoneRadius,
			&AudioScene::setEchoZoneRadius,
			0.01f,
			FLT_MAX,
			0.1f,
			allocator));
	PropertyRegister::add("echo_zone",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<AudioScene>)("Delay (ms)",
			&AudioScene::getEchoZoneDelay,
			&AudioScene::setEchoZoneDelay,
			0.01f,
			FLT_MAX,
			100.0f,
			allocator));
}


struct AudioSystemImpl : public AudioSystem
{
	explicit AudioSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_manager(engine.getAllocator())
		, m_device(nullptr)
	{
		registerProperties(engine.getAllocator());
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


	IScene* createScene(Universe& ctx)
	{
		return AudioScene::createInstance(*this, ctx, m_engine.getAllocator());
	}


	void destroyScene(IScene* scene) { AudioScene::destroyInstance(static_cast<AudioScene*>(scene)); }


	ClipManager m_manager;
	Engine& m_engine;
	AudioDevice* m_device;
};


namespace {


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

				auto handle = device.createBuffer(clip->getData(),
					clip->getSize(),
					clip->getChannels(),
					clip->getSampleRate(),
					0);
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


		void onAction()
		{
			m_is_opened = !m_is_opened;
		}


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

					if (ImGui::TreeNode((const void*)(intptr_t)clip_id, clip_info->name))
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
						if (m_app.getAssetBrowser()->resourceInput(
							"Clip", "", path, Lumix::lengthOf(path), CLIP_HASH))
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


LUMIX_PLUGIN_ENTRY(audio)
{
	return LUMIX_NEW(engine.getAllocator(), Lumix::AudioSystemImpl)(engine);
}


} // namespace Lumix

