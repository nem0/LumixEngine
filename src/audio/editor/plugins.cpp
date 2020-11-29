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
#include "engine/lua_wrapper.h"
#include "engine/reflection.h"
#include "engine/universe.h"


using namespace Lumix;


namespace
{


struct AssetBrowserPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	explicit AssetBrowserPlugin(StudioApp& app)
		: m_app(app)
		, m_browser(app.getAssetBrowser())
		, m_playing_clip(-1)
	{
		app.getAssetCompiler().registerExtension("ogg", Clip::TYPE);
	}

	struct Meta {
		bool looped = true;
		float volume = 1.f;
	};

	Meta getMeta(const Path& path) const {
		Meta meta;
		m_app.getAssetCompiler().getMeta(path, [&path, &meta](lua_State* L){
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "looped", &meta.looped);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "volume", &meta.volume);
		});
		return meta;
	}

	bool compile(const Path& src) override {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream src_data(m_app.getAllocator());
		if (!fs.getContentSync(src, Ref(src_data))) return false;
		
		Meta meta = getMeta(src);

		OutputMemoryStream compiled(m_app.getAllocator());
		compiled.reserve(64 + src_data.size());
		compiled.write((u32)0);
		compiled.write(Clip::Format::OGG);
		compiled.write(meta.looped);
		compiled.write(meta.volume);
		compiled.write(src_data.data(), src_data.size());
		return m_app.getAssetCompiler().writeCompiledResource(src.c_str(), Span(compiled.data(), (i32)compiled.size()));
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

		if(resources[0]->getPath().getHash() != m_meta_res) {
			m_meta = getMeta(resources[0]->getPath());
			m_meta_res = resources[0]->getPath().getHash();
		}

		ImGuiEx::Label("Looped");
		ImGui::Checkbox("##loop", &m_meta.looped);
		ImGuiEx::Label("Volume");
		ImGui::DragFloat("##vol", &m_meta.volume, 0.01f, 0, FLT_MAX);

		auto* clip = static_cast<Clip*>(resources[0]);
		ImGuiEx::Label("Length");
		ImGui::Text("%f", clip->getLengthSeconds());
		auto& device = getAudioDevice(m_app.getEngine());

		if (m_playing_clip >= 0)
		{
			if (ImGui::Button(ICON_FA_STOP "Stop"))
			{
				stopAudio();
				return;
			}
			float time = device.getCurrentTime(m_playing_clip);
			ImGuiEx::Label("Time");
			if (ImGui::SliderFloat("##time", &time, 0, clip->getLengthSeconds(), "%.2fs"))
			{
				device.setCurrentTime(m_playing_clip, time);
			}
		}

		if (m_playing_clip < 0 && ImGui::Button(ICON_FA_PLAY "Play"))
		{
			stopAudio();

			auto handle =
				device.createBuffer(clip->getData(), clip->getSize(), clip->getChannels(), clip->getSampleRate(), 0);
			device.play(handle, true);
			m_playing_clip = handle;
		}

		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_CHECK "Apply")) {
			const StaticString<512> src("volume = ", m_meta.volume
				, "\nlooped = ", m_meta.looped ? "true" : "false"
			);
			AssetCompiler& compiler = m_app.getAssetCompiler();
			compiler.updateMeta(resources[0]->getPath(), src);
			if (compiler.compile(resources[0]->getPath())) {
				resources[0]->getResourceManager().reload(*resources[0]);
			}
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
	Meta m_meta;
	u32 m_meta_res = 0;
};


struct StudioAppPlugin : StudioApp::IPlugin
{
	explicit StudioAppPlugin(StudioApp& app)
		: m_app(app)
		, m_asset_browser_plugin(app)
	{}

	const char* getName() const override { return "audio"; }

	void init() override 
	{
		m_app.registerComponent("", "ambient_sound", "Audio / Ambient sound");
		m_app.registerComponent(ICON_FA_HEADPHONES, "audio_listener", "Audio / Listener");
		m_app.registerComponent("", "echo_zone", "Audio / Echo zone");
		m_app.registerComponent("", "chorus_zone", "Audio / Chorus zone");

		IAllocator& allocator = m_app.getAllocator();

		m_app.getAssetBrowser().addPlugin(m_asset_browser_plugin);
		const char* extensions[] = { "ogg", nullptr };
		m_app.getAssetCompiler().addPlugin(m_asset_browser_plugin, extensions);
	}


	bool showGizmo(UniverseView& view, ComponentUID cmp) override
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

			addSphere(view, pos, radius, Color::BLUE);
			return true;
		}
		else if (cmp.type == CHORUS_ZONE_TYPE)
		{
			auto* audio_scene = static_cast<AudioScene*>(cmp.scene);
			float radius = audio_scene->getChorusZone(entity).radius;
			Universe& universe = audio_scene->getUniverse();
			const DVec3 pos = universe.getPosition(entity);

			addSphere(view, pos, radius, Color::BLUE);
			return true;
		}

		return false;
	}


	~StudioAppPlugin()
	{
		m_app.getAssetBrowser().removePlugin(m_asset_browser_plugin);
		m_app.getAssetCompiler().removePlugin(m_asset_browser_plugin);
	}


	StudioApp& m_app;
	AssetBrowserPlugin m_asset_browser_plugin;
};


} // anonymous namespace


LUMIX_STUDIO_ENTRY(audio)
{
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}
