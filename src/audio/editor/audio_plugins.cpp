#include <imgui/imgui.h>

#include "audio_device.h"
#include "audio_module.h"
#include "audio_system.h"
#include "clip.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/stream.h"
#include "core/tokenizer.h"
#include "editor/action.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_asset.h"
#include "editor/studio_app.h"
#include "editor/text_filter.h"
#include "editor/world_editor.h"
#include "engine/component_types.h"
#include "engine/component_uid.h"
#include "engine/engine.h"
#include "engine/resource_manager.h"
#include "engine/world.h"


using namespace black;


namespace
{


struct AssetBrowserPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	struct Meta {
		bool looped = true;
		float volume = 1.f;

		void load(const Path& path, StudioApp& app) {
			OutputMemoryStream blob(app.getAllocator());
			if (app.getAssetCompiler().getMeta(path, blob)) {
				StringView sv((const char*)blob.data(), (u32)blob.size());
				const ParseItemDesc descs[] = {
					{"looped", &looped},
					{"volume", &volume},
				};
				parse(sv, path.c_str(), descs);
			}
		}
	};

	struct EditorWindow : AssetEditorWindow {
		EditorWindow(const Path& path, StudioApp& app)
			: AssetEditorWindow(app)
			, m_app(app)
		{
			m_resource = app.getEngine().getResourceManager().load<Clip>(path);
			m_meta.load(path, app);
		}

		~EditorWindow() {
			stopAudio();
			m_resource->decRefCount();
		}

		void save() {
			AssetCompiler& compiler = m_app.getAssetCompiler();
			const StaticString<512> src("looped = ", m_meta.looped ? "true" : "false"
				, "\nvolume = ", m_meta.volume
			);
			Span span((const u8*)src.data, stringLength(src.data));
			compiler.updateMeta(m_resource->getPath(), span);
			m_dirty = false;
		}
		
		void windowGUI() override {
			CommonActions& actions = m_app.getCommonActions();
			if (ImGui::BeginMenuBar()) {
				if (actions.save.iconButton(m_dirty, &m_app)) save();
				if (actions.open_externally.iconButton(true, &m_app)) m_app.getAssetBrowser().openInExternalEditor(m_resource);
				if (actions.view_in_browser.iconButton(true, &m_app)) m_app.getAssetBrowser().locate(*m_resource);
				ImGui::EndMenuBar();
			}

			if (m_resource->isEmpty()) {
				ImGui::TextUnformatted("Loading...");
				return;
			}

			ImGuiEx::Label("Looped");
			m_dirty = ImGui::Checkbox("##loop", &m_meta.looped) || m_dirty;
			ImGuiEx::Label("Volume");
			m_dirty = ImGui::DragFloat("##vol", &m_meta.volume, 0.01f, 0, FLT_MAX) || m_dirty;

			ImGuiEx::Label("Length");
			ImGui::Text("%f", m_resource->getLengthSeconds());
			auto& device = getAudioDevice(m_app.getEngine());

			if (m_playing_clip >= 0)
			{
				if (ImGui::Button(ICON_FA_STOP "Stop")) {
					stopAudio();
					return;
				}
				float time = device.getCurrentTime(m_playing_clip);
				ImGuiEx::Label("Time");
				if (ImGui::SliderFloat("##time", &time, 0, m_resource->getLengthSeconds(), "%.2fs")) {
					device.setCurrentTime(m_playing_clip, time);
				}
			}

			if (m_playing_clip < 0 && ImGui::Button(ICON_FA_PLAY "Play")) {
				stopAudio();

				AudioDevice::BufferHandle handle = device.createBuffer(m_resource->getData(), m_resource->getSize(), m_resource->getChannels(), m_resource->getSampleRate(), 0);
				if (handle != AudioDevice::INVALID_BUFFER_HANDLE) {
					device.setVolume(handle, m_resource->m_volume);
					device.play(handle, true);
					m_playing_clip = handle;
				}
			}
		}
	
		const Path& getPath() override { return m_resource->getPath(); }
		const char* getName() const override { return "audio clip editor"; }
		
		static AudioDevice& getAudioDevice(Engine& engine) {
			auto* audio = static_cast<AudioSystem*>(engine.getSystemManager().getSystem("audio"));
			return audio->getDevice();
		}


		void stopAudio() {
			if (m_playing_clip < 0) return;

			getAudioDevice(m_app.getEngine()).stop(m_playing_clip);
			m_playing_clip = -1;
		}

		StudioApp& m_app;
		Clip* m_resource;
		Meta m_meta;
		i32 m_playing_clip;
	};

	explicit AssetBrowserPlugin(StudioApp& app)
		: m_app(app)
		, m_browser(app.getAssetBrowser())
	{
		app.getAssetCompiler().registerExtension("ogg", Clip::TYPE);
		app.getAssetCompiler().registerExtension("wav", Clip::TYPE);
	}
	
	void openEditor(const Path& path) override {
		IAllocator& allocator = m_app.getAllocator();
		UniquePtr<EditorWindow> win = UniquePtr<EditorWindow>::create(allocator, path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	bool compile(const Path& src) override {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream src_data(m_app.getAllocator());
		if (!fs.getContentSync(src, src_data)) return false;
		
		Meta meta;
		meta.load(src, m_app);

		OutputMemoryStream compiled(m_app.getAllocator());
		compiled.reserve(64 + src_data.size());
		compiled.write((u32)0);
		const bool is_wav = Path::hasExtension(src, "wav");
		compiled.write(is_wav ? Clip::Format::WAV : Clip::Format::OGG);
		compiled.write(meta.looped);
		compiled.write(meta.volume);
		compiled.write(src_data.data(), src_data.size());
		return m_app.getAssetCompiler().writeCompiledResource(src, Span(compiled.data(), (i32)compiled.size()));
	}

	const char* getIcon() const override { return ICON_FA_FILE_AUDIO; }
	const char* getLabel() const override { return "Audio"; }
	ResourceType getResourceType() const override { return Clip::TYPE; }
	
	StudioApp& m_app;
	AssetBrowser& m_browser;
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
		const char* extensions[] = { "ogg", "wav" };
		m_app.getAssetCompiler().addPlugin(m_asset_browser_plugin, Span(extensions));
		m_app.getAssetBrowser().addPlugin(m_asset_browser_plugin, Span(extensions));
	}


	bool showGizmo(WorldView& view, ComponentUID cmp) override {
		const EntityRef entity = (EntityRef)cmp.entity;
		if (cmp.type == types::echo_zone)
		{
			auto* audio_module = static_cast<AudioModule*>(cmp.module);
			float radius = audio_module->getEchoZone(entity).radius;
			World& world = audio_module->getWorld();
			const DVec3 pos = world.getPosition(entity);

			addSphere(view, pos, radius, Color::BLUE);
			return true;
		}
		else if (cmp.type == types::chorus_zone)
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


BLACK_STUDIO_ENTRY(audio) {
	PROFILE_FUNCTION();
	IAllocator& allocator = app.getAllocator();
	return BLACK_NEW(allocator, StudioAppPlugin)(app);
}
