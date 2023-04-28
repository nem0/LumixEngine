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
#include "engine/engine.h"
#include "engine/lua_wrapper.h"
#include "engine/stream.h"
#include "engine/world.h"


using namespace Lumix;


namespace
{


struct AssetBrowserPlugin final : AssetBrowser::Plugin, AssetCompiler::IPlugin
{
	explicit AssetBrowserPlugin(StudioApp& app)
		: m_app(app)
		, m_browser(app.getAssetBrowser())
		, m_playing_clip(-1)
		, AssetBrowser::Plugin(app.getAllocator())
	{
		app.getAssetCompiler().registerExtension("ogg", Clip::TYPE);
		app.getAssetCompiler().registerExtension("wav", Clip::TYPE);
	}

	struct Meta {
		bool looped = true;
		float volume = 1.f;
	};

	Meta getMeta(const Path& path) const {
		Meta meta;
		m_app.getAssetCompiler().getMeta(path, [&meta](lua_State* L){
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "looped", &meta.looped);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "volume", &meta.volume);
		});
		return meta;
	}

	bool compile(const Path& src) override {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream src_data(m_app.getAllocator());
		if (!fs.getContentSync(src, src_data)) return false;
		
		Meta meta = getMeta(src);

		OutputMemoryStream compiled(m_app.getAllocator());
		compiled.reserve(64 + src_data.size());
		compiled.write((u32)0);
		const bool is_wav = Path::hasExtension(src.c_str(), "wav");
		compiled.write(is_wav ? Clip::Format::WAV : Clip::Format::OGG);
		compiled.write(meta.looped);
		compiled.write(meta.volume);
		compiled.write(src_data.data(), src_data.size());
		return m_app.getAssetCompiler().writeCompiledResource(src.c_str(), Span(compiled.data(), (i32)compiled.size()));
	}

	static AudioDevice& getAudioDevice(Engine& engine)
	{
		auto* audio = static_cast<AudioSystem*>(engine.getSystemManager().getSystem("audio"));
		return audio->getDevice();
	}


	void stopAudio()
	{
		if (m_playing_clip < 0) return;

		getAudioDevice(m_app.getEngine()).stop(m_playing_clip);
		m_playing_clip = -1;
	}


	const char* getName() const override { return "Audio"; }


	bool onGUI(Span<Resource*> resources) override
	{
		if(resources.length() > 1) return false;

		if(resources[0]->getPath().getHash() != m_meta_res) {
			m_meta = getMeta(resources[0]->getPath());
			m_meta_res = resources[0]->getPath().getHash();
		}

		ImGuiEx::Label("Looped");
		bool changed = ImGui::Checkbox("##loop", &m_meta.looped);
		ImGuiEx::Label("Volume");
		changed = ImGui::DragFloat("##vol", &m_meta.volume, 0.01f, 0, FLT_MAX) || changed;

		auto* clip = static_cast<Clip*>(resources[0]);
		ImGuiEx::Label("Length");
		ImGui::Text("%f", clip->getLengthSeconds());
		auto& device = getAudioDevice(m_app.getEngine());

		if (m_playing_clip >= 0)
		{
			if (ImGui::Button(ICON_FA_STOP "Stop"))
			{
				stopAudio();
				return changed;
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

			AudioDevice::BufferHandle handle = device.createBuffer(clip->getData(), clip->getSize(), clip->getChannels(), clip->getSampleRate(), 0);
			if (handle != AudioDevice::INVALID_BUFFER_HANDLE) {
				device.setVolume(handle, clip->m_volume);
				device.play(handle, true);
				m_playing_clip = handle;
			}
		}

		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_CHECK "Apply")) {
			const StaticString<512> src("volume = ", m_meta.volume
				, "\nlooped = ", m_meta.looped ? "true" : "false"
			);
			AssetCompiler& compiler = m_app.getAssetCompiler();
			compiler.updateMeta(resources[0]->getPath(), src);
		}
		return changed;
	}

	void deserialize(InputMemoryStream& blob) override { blob.read(m_meta); }
	void serialize(OutputMemoryStream& blob) override { blob.write(m_meta); }

	void onResourceUnloaded(Resource*) override { stopAudio(); }


	ResourceType getResourceType() const override { return Clip::TYPE; }
	

	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		if (type == Clip::TYPE) return m_app.getAssetBrowser().copyTile("editor/textures/tile_audio.tga", out_path);
		return false;
	}


	int m_playing_clip;
	StudioApp& m_app;
	AssetBrowser& m_browser;
	Meta m_meta;
	FilePathHash m_meta_res;
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
		m_app.getAssetBrowser().addPlugin(m_asset_browser_plugin);
		const char* extensions[] = { "ogg", "wav", nullptr };
		m_app.getAssetCompiler().addPlugin(m_asset_browser_plugin, extensions);
	}


	bool showGizmo(WorldView& view, ComponentUID cmp) override
	{
		static const ComponentType ECHO_ZONE_TYPE = reflection::getComponentType("echo_zone");
		static const ComponentType CHORUS_ZONE_TYPE = reflection::getComponentType("chorus_zone");

		const EntityRef entity = (EntityRef)cmp.entity;
		if (cmp.type == ECHO_ZONE_TYPE)
		{
			auto* audio_module = static_cast<AudioModule*>(cmp.module);
			float radius = audio_module->getEchoZone(entity).radius;
			World& world = audio_module->getWorld();
			const DVec3 pos = world.getPosition(entity);

			addSphere(view, pos, radius, Color::BLUE);
			return true;
		}
		else if (cmp.type == CHORUS_ZONE_TYPE)
		{
			auto* audio_module = static_cast<AudioModule*>(cmp.module);
			float radius = audio_module->getChorusZone(entity).radius;
			World& world = audio_module->getWorld();
			const DVec3 pos = world.getPosition(entity);

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
