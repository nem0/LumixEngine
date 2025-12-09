#pragma once

#include "core/arena_allocator.h"
#include "core/defer.h"
#include "core/log.h"
#include "core/string.h"
#include "particle_script_compiler.h"
#include "renderer/editor/world_viewer.h"
#include "renderer/particle_system.h"
#include "renderer/render_module.h"

namespace Lumix {

struct ParticleScriptImportEditorWindow : AssetEditorWindow {
	ParticleScriptImportEditorWindow(const Path& path, StudioApp& app)
		: AssetEditorWindow(app)
		, m_app(app)
		, m_path(path)
	{
		m_editor = createParticleScriptEditor(m_app);
		m_editor->focus();
			
		OutputMemoryStream blob(app.getAllocator());
		if (app.getEngine().getFileSystem().getContentSync(path, blob)) {
			StringView v((const char*)blob.data(), (u32)blob.size());
			m_editor->setText(v);
		}
	}

	void save() {
		OutputMemoryStream blob(m_app.getAllocator());
		m_editor->serializeText(blob);
		m_app.getAssetBrowser().saveResource(m_path, blob);
		m_dirty = false;
	}

	void fileChangedExternally() override {
		OutputMemoryStream tmp(m_app.getAllocator());
		OutputMemoryStream tmp2(m_app.getAllocator());
		m_editor->serializeText(tmp);
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.getContentSync(m_path, tmp2)) return;

		if (tmp.size() == tmp2.size() && memcmp(tmp.data(), tmp2.data(), tmp.size()) == 0) {
			m_dirty = false;
		}
	}

	void windowGUI() override {
		CommonActions& actions = m_app.getCommonActions();

		if (ImGui::BeginMenuBar()) {
			if (actions.save.iconButton(m_dirty, &m_app)) save();
			if (actions.open_externally.iconButton(true, &m_app)) m_app.getAssetBrowser().openInExternalEditor(m_path);
			if (actions.view_in_browser.iconButton(true, &m_app)) m_app.getAssetBrowser().locate(m_path);
			ImGui::EndMenuBar();
		}

		if (m_editor->gui("codeeditor", ImVec2(0, 0), m_app.getMonospaceFont(), m_app.getDefaultFont())) m_dirty = true;
	}
	
	const Path& getPath() override { return m_path; }
	const char* getName() const override { return "particle script import editor"; }

	StudioApp& m_app;
	UniquePtr<CodeEditor> m_editor;
	Path m_path;
};

struct ParticleScriptImportPlugin : EditorAssetPlugin {
	ParticleScriptImportPlugin(StudioApp& app, IAllocator& allocator)
		: EditorAssetPlugin("Particle script import", "pai", TYPE, app, allocator)
		, m_app(app)
	{}

	bool compile(const Path& src) override { return true; }
	void openEditor(const Path& path) override {
		UniquePtr<ParticleScriptImportEditorWindow> win = UniquePtr<ParticleScriptImportEditorWindow>::create(m_app.getAllocator(), path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}
	void createResource(OutputMemoryStream& blob) override {}

	StudioApp& m_app;
	static inline ResourceType TYPE = ResourceType("particle_script_import");
};

struct ParticleScriptPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	ParticleScriptPlugin(StudioApp& app, IAllocator& allocator)
		: m_app(app)
		, m_allocator(allocator)
	{
		AssetCompiler& compiler = app.getAssetCompiler();
		compiler.registerExtension("pat", ParticleSystemResource::TYPE);
		const char* particle_emitter_exts[] = { "pat" };
		compiler.addPlugin(*this, Span(particle_emitter_exts));
		m_app.getAssetBrowser().addPlugin(*this, Span(particle_emitter_exts));

		m_app.getSettings().registerOption("particle_script_preview", &m_show_preview, "Particle script", "Show preview");
	}

	bool compile(const Path& src) override {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream src_data(m_app.getAllocator());
		if (!fs.getContentSync(src, src_data)) return false;
		if (src_data.empty()) return false;

		StringView content = { (const char*)src_data.data(), (const char*)src_data.data() + src_data.size() };
		ParticleScriptCompiler compiler(fs, m_allocator);
		OutputMemoryStream output(m_app.getAllocator());
		if (!compiler.compile(src, content, output)) return false;

		return m_app.getAssetCompiler().writeCompiledResource(src, Span(output.data(), (i32)output.size()));
	}

	const char* getIcon() const override { return ICON_FA_FIRE; }

	void addSubresources(AssetCompiler& compiler, const Path& path, AtomicI32&) override {
		compiler.addResource(ParticleSystemResource::TYPE, path);
		
		OutputMemoryStream content(m_allocator);
		if (!m_app.getEngine().getFileSystem().getContentSync(path, content)) {
			logError("Failed to read ", path);
			return;
		}

		if (content.empty()) {
			logError(path, " is empty.");
			return;
		}

		ParticleScriptTokenizer tokenizer;
		tokenizer.m_document.begin = (const char*)content.data();
		tokenizer.m_document.end = tokenizer.m_document.begin + content.size();
		tokenizer.m_current = tokenizer.m_document.begin;
		tokenizer.m_current_token = tokenizer.nextToken();
		
		for (;;) {
			ParticleScriptTokenizer::Token token = tokenizer.m_current_token;
			tokenizer.m_current_token = tokenizer.nextToken();
			
			switch (token.type) {
				case ParticleScriptTokenizer::Token::EOF: return;
				case ParticleScriptTokenizer::Token::ERROR: return;
				case ParticleScriptTokenizer::Token::IMPORT: {
					ParticleScriptTokenizer::Token t = tokenizer.m_current_token;
					tokenizer.m_current_token = tokenizer.nextToken();
					if (t.type == ParticleScriptTokenizer::Token::STRING) {
						m_app.getAssetCompiler().registerDependency(path, Path(t.value));
					}
					break;
				}
				default: break;
			}
		}	
	}

	bool canCreateResource() const override { return true; }
	bool canMultiEdit() override { return false; }
	void createResource(struct OutputMemoryStream& content) override {
		content << R"#(
emitter Emitter0 {
	material "/engine/materials/particle.mat"
	init_emit_count 0
	emit_per_second 10
	
	out i_position : float3
	out i_scale : float
	out i_color : float4
	out i_rot : float
	out i_frame : float
	out i_emission : float

	var pos : float3
	var t : float

	fn update() {
		t = t + time_delta;
		if t > 1 {
			kill();
		}
	}
	fn emit() {
		pos.x = random(-1, 1);
		pos.y = random(0, 2);
		pos.z = random(-1, 1);
		t = 0;
	}
	fn output() {
		i_position = pos;
		i_scale = 0.1;
		i_color = {1, 0, 0, 1};
		i_rot = 0;
		i_frame = 0;
		i_emission = 10;
	}
}
		)#";
	}
	const char* getDefaultExtension() const override { return "pat"; }

	const char* getLabel() const override { return "Particle script"; }
	ResourceType getResourceType() const override { return ParticleSystemResource::TYPE; }
	
	void openEditor(const struct Path& path) override;

	IAllocator& m_allocator;
	StudioApp& m_app;
	bool m_show_preview = true;
};


struct ParticleScriptEditorWindow : AssetEditorWindow {
	ParticleScriptEditorWindow(const Path& path, ParticleScriptPlugin& plugin)
		: AssetEditorWindow(plugin.m_app)
		, m_app(plugin.m_app)
		, m_path(path)
		, m_viewer(plugin.m_app)
		, m_plugin(plugin)
	{
		m_editor = createParticleScriptEditor(m_app);
		m_editor->focus();

		OutputMemoryStream blob(m_app.getAllocator());
		if (m_app.getEngine().getFileSystem().getContentSync(path, blob)) {
			StringView v((const char*)blob.data(), (u32)blob.size());
			m_editor->setText(v);
		}

		World* world = m_viewer.m_world;
		m_preview_entity = world->createEntity({ 0, 0, 0 }, Quat::IDENTITY);
		world->createComponent(types::particle_emitter, m_preview_entity);
		RenderModule* module = (RenderModule*)world->getModule(types::particle_emitter);
		module->setParticleEmitterPath(m_preview_entity, m_path);

		m_viewer.m_viewport.pos = { 0, 2, 5 };
		m_viewer.m_viewport.rot = { 0, 0, 1, 0 };
	}

	void save() {
		OutputMemoryStream blob(m_app.getAllocator());
		m_editor->serializeText(blob);
		m_app.getAssetBrowser().saveResource(m_path, blob);
		m_dirty = false;
	}

	void fileChangedExternally() override {
		OutputMemoryStream tmp(m_app.getAllocator());
		OutputMemoryStream tmp2(m_app.getAllocator());
		m_editor->serializeText(tmp);
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.getContentSync(m_path, tmp2)) return;

		if (tmp.size() == tmp2.size() && memcmp(tmp.data(), tmp2.data(), tmp.size()) == 0) {
			m_dirty = false;
		}
	}

	void windowGUI() override {
		CommonActions& actions = m_app.getCommonActions();

		if (ImGui::BeginMenuBar()) {
			if (actions.save.iconButton(m_dirty, &m_app)) save();
			if (actions.open_externally.iconButton(true, &m_app)) m_app.getAssetBrowser().openInExternalEditor(m_path);
			if (actions.view_in_browser.iconButton(true, &m_app)) m_app.getAssetBrowser().locate(m_path);
			if (ImGuiEx::IconButton(ICON_FA_ANGLE_DOUBLE_RIGHT, "Toggle preview")) m_plugin.m_show_preview = !m_plugin.m_show_preview;
			if (ImGuiEx::IconButton(ICON_FA_BUG, "Debug")) { ASSERT(false); /*TODO*/ };
			ImGui::EndMenuBar();
		}

		if (m_plugin.m_show_preview) {
			float w = ImGui::GetContentRegionAvail().x / 2;
			if (ImGui::BeginChild("code_pane", ImVec2(w, 0), ImGuiChildFlags_ResizeX)) {
				if (m_editor->gui("codeeditor", ImVec2(0, 0), m_app.getMonospaceFont(), m_app.getDefaultFont())) m_dirty = true;
			}
			ImGui::EndChild();
			ImGui::SameLine();
			if (ImGui::BeginChild("preview_pane")) {
				auto* module = (RenderModule*)m_viewer.m_world->getModule(types::particle_emitter);
				ParticleSystem& system = module->getParticleEmitter(m_preview_entity);

				if (ImGuiEx::IconButton(ICON_FA_INFO_CIRCLE, "Info")) m_show_info = !m_show_info;
				ImGui::SameLine();
				if (m_play) {
					if (ImGuiEx::IconButton(ICON_FA_PAUSE, "Pause")) m_play = false;
					float td = m_app.getEngine().getLastTimeDelta();
					module->updateParticleEmitter(m_preview_entity, td);
				}
				else {
					if (ImGuiEx::IconButton(ICON_FA_PLAY, "Play")) m_play = true;
				}
				ImGui::SameLine();
				if (ImGuiEx::IconButton(ICON_FA_STEP_FORWARD, "Next frame")) {
					if (m_play) logError("Particle simulation must be paused.");
					else {
						float td = m_app.getEngine().getLastTimeDelta();
						module->updateParticleEmitter(m_preview_entity, td);
					}
				}

				ImGui::SameLine();
				if (ImGuiEx::IconButton(ICON_FA_EYE, "Toggle ground")) {
					m_show_ground = !m_show_ground;
					module->enableModelInstance(m_viewer.m_ground, m_show_ground);
				};

				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_UNDO_ALT " Reset")) system.reset();
				u32 num_particles = 0;
				for (ParticleSystem::Emitter& emitter : system.getEmitters()) {
					if (emitter.resource_emitter.max_ribbons > 0) {
						for (const auto& ribbon : emitter.ribbons) num_particles += ribbon.length;
					}
					else {
						num_particles += emitter.particles_count;
					}
				}

				if (!system.m_globals.empty()) {
					ImGui::SameLine();
					if (ImGui::Button("Globals")) ImGui::OpenPopup("Globals");
					if (ImGui::BeginPopup("Globals")) {
						u32 offset = 0;
						for (const auto& p : system.getResource()->getGlobals()) {
							ImGui::PushID(&p);
							ImGuiEx::Label(p.name.c_str());
							ImGui::SetNextItemWidth(150);
							float* f = system.m_globals.begin() + offset;
							switch (p.num_floats) {
							case 1: ImGui::InputFloat("##v", f); break;
							case 2: ImGui::InputFloat2("##v", f); break;
							case 3: ImGui::InputFloat3("##v", f); break;
							case 4: ImGui::InputFloat4("##v", f); break;
							}
							offset += p.num_floats;
							ImGui::PopID();
						}
						ImGui::EndPopup();
					}
				}
				ImGui::SameLine();
				ImGui::Text("Particles: %d", num_particles);
				
				ImVec2 viewer_pos = ImGui::GetCursorScreenPos();
				m_viewer.gui();
				
				if (m_show_info) {
					ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
							 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;					
					ImGui::SetNextWindowPos(ImVec2(viewer_pos.x + 10, viewer_pos.y + 10), ImGuiCond_Always);
					if (ImGui::Begin("Emitter Info##overlay", &m_show_info, flags)) {
						Span<ParticleSystemResource::Emitter> emitters = system.getResource()->getEmitters();
						for (u32 i = 0, c = emitters.size(); i < c; ++i) {
							const auto& emitter = emitters[i];
							ImGui::Text("Emitter %d", i + 1);
							ImGui::Indent();
							ImGui::LabelText("Emit registers", "%d", emitter.emit_registers_count);
							ImGui::LabelText("Emit instructions", "%d", emitter.emit_instructions_count);
							ImGui::LabelText("Update registers", "%d", emitter.update_registers_count);
							ImGui::LabelText("Update instructions", "%d", emitter.update_instructions_count);
							ImGui::LabelText("Output registers", "%d", emitter.output_registers_count);
							ImGui::LabelText("Output instructions", "%d", emitter.output_instructions_count);
							ImGui::Unindent();
						}
						ImGui::End();
					}
				}
			}
			ImGui::EndChild();
		}
		else {
			if (m_editor->gui("codeeditor", ImVec2(0, 0), m_app.getMonospaceFont(), m_app.getDefaultFont())) m_dirty = true;
		}
	}

	const Path& getPath() override { return m_path; }
	const char* getName() const override { return "particle script editor"; }

	ParticleScriptPlugin& m_plugin;
	StudioApp& m_app;
	UniquePtr<CodeEditor> m_editor;
	WorldViewer m_viewer;
	Path m_path;
	EntityRef m_preview_entity;
	bool m_play = true;
	bool m_show_ground = true;
	bool m_show_info = false;
};

void ParticleScriptPlugin::openEditor(const struct Path& path) {
	UniquePtr<ParticleScriptEditorWindow> win = UniquePtr<ParticleScriptEditorWindow>::create(m_allocator, path, *this);
	m_app.getAssetBrowser().addWindow(win.move());
}


}