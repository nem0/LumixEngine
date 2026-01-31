#include <imgui/imgui.h>

#include "core/array.h"
#include "core/hash_map.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/string.h"

#include "animation/animation.h"
#include "animation/animation_module.h"
#include "animation/controller.h"
#include "animation/property_animation.h"
#include "controller_editor.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_asset.h"
#include "editor/property_grid.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/component_types.h"
#include "engine/engine.h"
#include "engine/resource_manager.h"
#include "engine/reflection.h"
#include "engine/world.h"
#include "renderer/editor/model_meta.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include "renderer/editor/world_viewer.h"


using namespace black;


namespace {

static struct {
	const char* label;
	PropertyAnimation::CurveType type;
} g_transform_descs[] = {
	{ "Local position X", PropertyAnimation::CurveType::LOCAL_POS_X },
	{ "Local position Y", PropertyAnimation::CurveType::LOCAL_POS_Y },
	{ "Local position Z", PropertyAnimation::CurveType::LOCAL_POS_Z },
	{ "Position X", PropertyAnimation::CurveType::POS_X },
	{ "Position Y", PropertyAnimation::CurveType::POS_Y },
	{ "Position Z", PropertyAnimation::CurveType::POS_Z },
	//{ "Rotation X", PropertyAnimation::CurveType::ROT_X },
	//{ "Rotation Y", PropertyAnimation::CurveType::ROT_Y },
	//{ "Rotation Z", PropertyAnimation::CurveType::ROT_Z },
	{ "Scale X", PropertyAnimation::CurveType::SCALE_X },
	{ "Scale Y", PropertyAnimation::CurveType::SCALE_Y },
	{ "Scale Z", PropertyAnimation::CurveType::SCALE_Z },
};

static PropertyAnimation::CurveType toCurveType(StringView str) {
	for (auto& desc : g_transform_descs) {
		if (equalStrings(str, desc.label)) return desc.type;
	}
	return PropertyAnimation::CurveType::NOT_SET;
}

static const char* toString(PropertyAnimation::CurveType type) {
	switch (type) {
		case PropertyAnimation::CurveType::LOCAL_POS_X: return "Local position X";
		case PropertyAnimation::CurveType::LOCAL_POS_Y: return "Local position Y";
		case PropertyAnimation::CurveType::LOCAL_POS_Z: return "Local position Z";
		case PropertyAnimation::CurveType::POS_X: return "Position X";
		case PropertyAnimation::CurveType::POS_Y: return "Position Y";
		case PropertyAnimation::CurveType::POS_Z: return "Position Z";
		case PropertyAnimation::CurveType::SCALE_X: return "Scale X";
		case PropertyAnimation::CurveType::SCALE_Y: return "Scale Y";
		case PropertyAnimation::CurveType::SCALE_Z: return "Scale Z";
		case PropertyAnimation::CurveType::NOT_SET: return "Not set";
		case PropertyAnimation::CurveType::PROPERTY: return "Property";
	}
	return "ERROR";
} 

static const char* fromCString(StringView input, Time& value) {
	float seconds;
	const char* ret_value = fromCString(input, seconds);
	value = Time::fromSeconds(seconds);
	return ret_value;
}

template <bool use_frames, typename T>
static bool consumeNumberArray(Tokenizer& tokenizer, Array<T>& array) {
	if (!tokenizer.consume("[")) return false;
	for (;;) {
		Tokenizer::Token token = tokenizer.nextToken();
		if (!token) return false;
		if (token == "]") return true;
		T value;
		if constexpr (use_frames) {
			u32 frame;
			if (!fromCString(token.value, frame)) {
				logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected a number, got ", token.value);
				tokenizer.logErrorPosition(token.value.begin);
				return false;
			}
			value = Time::fromSeconds(frame / 30.f);
		}
		else {
			if (!fromCString(token.value, value)) {
				logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected a number, got ", token.value);
				tokenizer.logErrorPosition(token.value.begin);
				return false;
			}
		}
		array.push(value);
		token = tokenizer.nextToken();
		if (!token) return false;
		if (token == "]") return true;
		if (token != ",") {
			logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected ',' or ']', got ", token.value);
			tokenizer.logErrorPosition(token.value.begin);
			return false;
		}
	}
}

struct AnimationAssetBrowserPlugin : AssetBrowser::IPlugin {
	struct EditorWindow : AssetEditorWindow, SimpleUndoRedo {
		EditorWindow(const Path& path, StudioApp& app)
			: AssetEditorWindow(app)
			, SimpleUndoRedo(app.getAllocator())
			, m_app(app)
			, m_viewer(app)
			, m_parent_meta(app.getAllocator())
		{
			m_resource = app.getEngine().getResourceManager().load<Animation>(path);

			m_viewer.m_world->createComponent(types::animable, *m_viewer.m_mesh);

			auto* anim_module = static_cast<AnimationModule*>(m_viewer.m_world->getModule(types::animable));
			anim_module->setAnimableAnimation(*m_viewer.m_mesh, path);

			Path parent_path(ResourcePath::getResource(path));
			m_parent_meta.load(parent_path, m_app);

			auto* render_module = static_cast<RenderModule*>(m_viewer.m_world->getModule(types::model_instance));
			if (m_parent_meta.skeleton.isEmpty()) {
				m_model = m_app.getEngine().getResourceManager().load<Model>(parent_path);
				render_module->setModelInstancePath(*m_viewer.m_mesh, parent_path);
			}
			else {
				m_model = m_app.getEngine().getResourceManager().load<Model>(m_parent_meta.skeleton);
				render_module->setModelInstancePath(*m_viewer.m_mesh, m_parent_meta.skeleton);
			}
			pushUndo(NO_MERGE_UNDO);
		}

		~EditorWindow() {
			m_resource->decRefCount();
			if (m_model) m_model->decRefCount();
		}

		void deserialize(InputMemoryStream& blob) override {
			StringView sv((const char*)blob.getData(), (u32)blob.size());
			m_parent_meta.deserialize(sv, Path("undo/redo"));
		}
		void serialize(OutputMemoryStream& blob) override { m_parent_meta.serialize(blob, Path()); }

		void saveUndo(bool changed) {
			if (!changed) return;

			pushUndo(ImGui::GetItemID());
			m_dirty = true;
		}
		
		void save() {
			OutputMemoryStream blob(m_app.getAllocator());
			m_parent_meta.serialize(blob, m_resource->getPath());
			m_app.getAssetCompiler().updateMeta(Path(ResourcePath::getResource(m_resource->getPath())), blob);
			m_dirty = false;
		}

		void windowGUI() override {
			CommonActions& actions = m_app.getCommonActions();

			if (ImGui::BeginMenuBar()) {
				if (actions.save.iconButton(m_dirty, &m_app)) save();
				if (actions.view_in_browser.iconButton(true, &m_app)) m_app.getAssetBrowser().locate(*m_resource);
				if (actions.undo.iconButton(canUndo(), &m_app)) undo();
				if (actions.redo.iconButton(canRedo(), &m_app)) redo();
				if (ImGuiEx::IconButton(ICON_FA_EXTERNAL_LINK_ALT, "Go to parent")) {
					m_app.getAssetBrowser().openEditor(Path(ResourcePath::getResource(m_resource->getPath())));
				}
				ImGui::EndMenuBar();
			}

			if (m_resource->isEmpty()) {
				ImGui::TextUnformatted("Loading...");
				return;
			}

			if (!m_resource->isReady()) {
				ImGuiEx::Label("Skeleton");
				saveUndo(m_app.getAssetBrowser().resourceInput("##ske", m_parent_meta.skeleton, Model::TYPE, -1));
				return;
			}

			if (!ImGui::BeginTable("tab", 2, ImGuiTableFlags_Resizable)) return;
			ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 250);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			const Array<Animation::RotationTrack>& rotations = m_resource->getRotations();
			const Array<Animation::ConstRotationTrack>& const_rotations = m_resource->getConstRotations();
			const Array<Animation::TranslationTrack>& translations = m_resource->getTranslations();
			const Array<Animation::ConstTranslationTrack>& const_translations = m_resource->getConstTranslations();

			ImGuiEx::Label("Skeleton");
			saveUndo(m_app.getAssetBrowser().resourceInput("##ske", m_parent_meta.skeleton, Model::TYPE, -1));
			ImGuiEx::Label("Root rotation");
			saveUndo(ImGui::CheckboxFlags("##rmr", (i32*)&m_parent_meta.root_motion_flags, (i32)Animation::Flags::ROOT_ROTATION));
			ImGuiEx::Label("XZ root translation");
			saveUndo(ImGui::CheckboxFlags("##rmxz", (i32*)&m_parent_meta.root_motion_flags, (i32)Animation::Flags::XZ_ROOT_TRANSLATION));
			ImGuiEx::Label("Y root translation");
			saveUndo(ImGui::CheckboxFlags("##rmy", (i32*)&m_parent_meta.root_motion_flags, (i32)Animation::Flags::Y_ROOT_TRANSLATION));
			ImGuiEx::Label("Animation translation error");
			saveUndo(ImGui::DragFloat("##aert", &m_parent_meta.anim_translation_error, 0.01f));
			ImGuiEx::Label("Animation rotation error");
			saveUndo(ImGui::DragFloat("##aerr", &m_parent_meta.anim_rotation_error, 0.01f));

			ImGuiEx::Label("Frames");
			ImGui::Text("%d", m_resource->getFramesCount());
			ImGuiEx::Label("Translation frame size");
			ImGui::Text("%d", m_resource->getTranslationFrameSizeBits());
			ImGuiEx::Label("Rotation frame size");
			ImGui::Text("%d", m_resource->getRotationFrameSizeBits());

			ImGuiEx::Label("Translation tracks (constant / animated)");
			ImGui::Text("%d / %d", const_translations.size(), translations.size());

			ImGuiEx::Label("Rotation tracks (constant / animated)");
			ImGui::Text("%d / %d", const_rotations.size(), rotations.size());

			if (!translations.empty() && ImGui::TreeNode("Translations")) {
				for (const Animation::TranslationTrack& track : translations) {

					const Model::Bone& bone = m_model->getBone(track.bone_index);
					ImGuiTreeNodeFlags flags = m_selected_bone == track.bone_index ? ImGuiTreeNodeFlags_Selected : 0;
					flags |= ImGuiTreeNodeFlags_OpenOnArrow;
					u32 bits = track.bitsizes[0] + track.bitsizes[1] + track.bitsizes[2];
					bool open = ImGui::TreeNodeEx(&bone, flags, "%s (%d bits)", bone.name.c_str(), bits);
					if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
						m_selected_bone = track.bone_index;
					}
					if (open) {
						ImGui::Columns(4);
						for (u32 i = 0; i < m_resource->getFramesCount(); ++i) {
							const Vec3 p = m_resource->getTranslation(i, track);
							ImGui::Text("%d:", i);
							ImGui::NextColumn();
							ImGui::Text("%f", p.x);
							ImGui::NextColumn();
							ImGui::Text("%f", p.y);
							ImGui::NextColumn();
							ImGui::Text("%f", p.z);
							ImGui::NextColumn();

						}
						ImGui::Columns();
						ImGui::TreePop();
					}
				}
				for (const Animation::ConstTranslationTrack& track : const_translations) {
					const Model::Bone& bone = m_model->getBone(track.bone_index);
					ImGuiTreeNodeFlags flags = m_selected_bone == track.bone_index ? ImGuiTreeNodeFlags_Selected : 0;
					flags |= ImGuiTreeNodeFlags_OpenOnArrow;
					bool open = ImGui::TreeNodeEx(&bone, flags, "%s (constant)", bone.name.c_str());
					if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
						m_selected_bone = track.bone_index;
					}
					if (open) {
						ImGui::Text("%f; %f; %f", track.value.x, track.value.y, track.value.z);
						ImGui::TreePop();
					}
				}
				ImGui::TreePop();
			}

			if ((!rotations.empty() || !const_rotations.empty()) && ImGui::TreeNode("Rotations")) {
				for (const Animation::RotationTrack& track : rotations) {
					const Model::Bone& bone = m_model->getBone(track.bone_index);
					ImGuiTreeNodeFlags flags = m_selected_bone == track.bone_index ? ImGuiTreeNodeFlags_Selected : 0;
					flags |= ImGuiTreeNodeFlags_OpenOnArrow;

					u32 bits = track.bitsizes[0] + track.bitsizes[1] + track.bitsizes[2] + 1;
					bool open = ImGui::TreeNodeEx(&bone, flags, "%s (%d bits)", bone.name.c_str(), bits);
					if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
						m_selected_bone = track.bone_index;
					}
					if (open) {
						ImGui::Columns(4);
						for (u32 i = 0; i < m_resource->getFramesCount(); ++i) {
							const Vec3 r = radiansToDegrees(m_resource->getRotation(i, track).toEuler());
							ImGui::Text("%d:", i);
							ImGui::NextColumn();
							ImGui::Text("%f", r.x);
							ImGui::NextColumn();
							ImGui::Text("%f", r.y);
							ImGui::NextColumn();
							ImGui::Text("%f", r.z);
							ImGui::NextColumn();
						}
						ImGui::Columns();
						ImGui::TreePop();
					}
				}
				for (const Animation::ConstRotationTrack& track : const_rotations) {
					const Model::Bone& bone = m_model->getBone(track.bone_index);
					ImGuiTreeNodeFlags flags = m_selected_bone == track.bone_index ? ImGuiTreeNodeFlags_Selected : 0;
					flags |= ImGuiTreeNodeFlags_OpenOnArrow;

					bool open = ImGui::TreeNodeEx(&bone, flags, "%s (constant)", bone.name.c_str());
					if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
						m_selected_bone = track.bone_index;
					}
					if (open) {
						Vec3 e = track.value.toEuler();
						ImGui::Text("%f; %f; %f", e.x, e.y, e.z);
						ImGui::TreePop();
					}
				}
				ImGui::TreePop();
			}

			ImGui::TableNextColumn();
			previewGUI();

			ImGui::EndTable();
		}
		
		void previewGUI() {
			auto* render_module = static_cast<RenderModule*>(m_viewer.m_world->getModule(types::model_instance));
			auto* anim_module = static_cast<AnimationModule*>(m_viewer.m_world->getModule(types::animable));
			if (ImGuiEx::IconButton(ICON_FA_COG, "Settings")) ImGui::OpenPopup("Settings");
			ImGui::SameLine();
			if (ImGui::BeginPopup("Settings")) {
				Path model_path = m_model ? m_model->getPath() : Path();
				if (m_app.getAssetBrowser().resourceInput("Preview model", model_path, ResourceType("model"), -1)) {
					if (m_model) m_model->decRefCount();
					m_model = m_app.getEngine().getResourceManager().load<Model>(model_path);
					render_module->setModelInstancePath(*m_viewer.m_mesh, m_model ? m_model->getPath() : Path());
				}

				bool show_mesh = render_module->isModelInstanceEnabled(*m_viewer.m_mesh);
				if (ImGui::Checkbox("Show mesh", &show_mesh)) {
					render_module->enableModelInstance(*m_viewer.m_mesh, show_mesh);
				}
			
				ImGui::Checkbox("Show skeleton", &m_show_skeleton);
			
				ImGui::DragFloat("Playback speed", &m_playback_speed, 0.01f, -FLT_MAX, FLT_MAX);
				ImGui::EndPopup();
			}

			if (ImGuiEx::IconButton(ICON_FA_STEP_BACKWARD, "Step back", !m_play)) {
				anim_module->updateAnimable(*m_viewer.m_mesh, -1 / 30.f);
			}
			ImGui::SameLine();
			if (m_play) {
				if (ImGuiEx::IconButton(ICON_FA_PAUSE, "Pause")) m_play = false;
			}
			else {
				if (ImGuiEx::IconButton(ICON_FA_PLAY, "Play")) m_play = true;
			}
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_STEP_FORWARD, "Step", !m_play)) {
				anim_module->updateAnimable(*m_viewer.m_mesh, 1 / 30.f);
			}

			Animable& animable = anim_module->getAnimable(*m_viewer.m_mesh);
			float t = animable.time.seconds();
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1);
			if (ImGui::SliderFloat("##time", &t, 0, m_resource->getLength().seconds())) {
				animable.time = Time::fromSeconds(t);
				anim_module->updateAnimable(*m_viewer.m_mesh, 0);
			}

			if (m_show_skeleton) m_viewer.drawSkeleton(m_selected_bone);
			if (m_play) {
				anim_module->updateAnimable(*m_viewer.m_mesh, m_app.getEngine().getLastTimeDelta() * m_playback_speed);
			}

			if (!m_init) {
				m_viewer.resetCamera(*m_model);
				m_init = true;
			}

			m_viewer.gui();
		}

		const Path& getPath() override { return m_resource->getPath(); }
		const char* getName() const override { return "animation editor"; }

		StudioApp& m_app;
		Animation* m_resource;
		Model* m_model = nullptr;
		bool m_init = false;
		bool m_show_skeleton = true;
		bool m_play = true;
		float m_playback_speed = 1.f;
		WorldViewer m_viewer;
		ModelMeta m_parent_meta;
		i32 m_selected_bone = -1;
	};
	
	explicit AnimationAssetBrowserPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("ani", Animation::TYPE);
	}

	const char* getLabel() const override { return "Animation"; }
	ResourceType getResourceType() const override { return Animation::TYPE; }
	
	void openEditor(const Path& path) override {
		IAllocator& allocator = m_app.getAllocator();
		UniquePtr<EditorWindow> win = UniquePtr<EditorWindow>::create(allocator, path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	StudioApp& m_app;
	Animation* m_resource;
};


struct PropertyAnimationPlugin : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	struct EditorWindow : AssetEditorWindow, SimpleUndoRedo {
		EditorWindow(const Path& path, StudioApp& app)
			: AssetEditorWindow(app)
			, SimpleUndoRedo(app.getAllocator())
			, m_app(app)
		{
			m_resource = app.getEngine().getResourceManager().load<PropertyAnimation>(path);
		}

		~EditorWindow() {
			m_resource->decRefCount();
		}

		void deserialize(InputMemoryStream& blob) override {
			blob.read(m_resource->length);
			const u32 count = blob.read<u32>();
			m_resource->curves.clear();
			m_resource->curves.reserve(count);
			for (u32 i = 0; i < count; ++i) {
				PropertyAnimation::Curve& curve = m_resource->curves.emplace(m_app.getAllocator());
				blob.read(curve.type);
				blob.read(curve.cmp_type);
				blob.read(curve.property);
				const u32 frames_count = blob.read<u32>();
				curve.frames.resize(frames_count);
				curve.values.resize(frames_count);
				blob.read(curve.frames.begin(), curve.frames.byte_size());
				blob.read(curve.values.begin(), curve.values.byte_size());
			}
		}

		void serialize(OutputMemoryStream& blob) override {
			blob.write(m_resource->length);
			blob.write((u32)m_resource->curves.size());
			for (PropertyAnimation::Curve& curve : m_resource->curves) {
				blob.write(curve.type);
				blob.write(curve.cmp_type);
				blob.write(curve.property);
				blob.write((u32)curve.frames.size());
				blob.write(curve.frames.begin(), curve.frames.byte_size());
				blob.write(curve.values.begin(), curve.values.byte_size());
			}
		}

		void save() {
			OutputMemoryStream blob(m_app.getAllocator());
			ASSERT(m_resource->isReady());

			for (PropertyAnimation::Curve& curve : m_resource->curves) {
				blob << "{\n";
				blob << "\tversion = 1,\n";
				blob << "\ttype = \"" << toString(curve.type) << "\",\n";
				if (curve.type == PropertyAnimation::CurveType::PROPERTY) {
					blob << "\t component = \"" << reflection::getComponent(curve.cmp_type)->name << "\",\n";
					blob << "\t property = \"" << curve.property->name << "\",\n";
				}
				blob << "\tkeyframes = [ ";
				for (int i = 0; i < curve.frames.size(); ++i) {
					if (i != 0) blob << ", ";
					// we store the time in seconds, so it's easy to edit the file manually, or see the change in diff
					blob << curve.frames[i].seconds();
				}
				blob << " ],\n";
				blob << "\tvalues = [ ";
				for (int i = 0; i < curve.values.size(); ++i) {
					if (i != 0) blob << ", ";
					blob << curve.values[i];
				}
				blob << " ]\n},\n\n";
			}
			m_app.getAssetBrowser().saveResource(*m_resource, blob);
			m_dirty = false;
		}
		
		void windowGUI() override {
			CommonActions& actions = m_app.getCommonActions();

			if (ImGui::BeginMenuBar()) {
				if (actions.save.iconButton(m_dirty, &m_app)) save();
				if (actions.open_externally.iconButton(true, &m_app)) m_app.getAssetBrowser().openInExternalEditor(m_resource);
				if (actions.view_in_browser.iconButton(true, &m_app)) m_app.getAssetBrowser().locate(*m_resource);
				if (actions.undo.iconButton(canUndo(), &m_app)) undo();
				if (actions.redo.iconButton(canRedo(), &m_app)) redo();
				ImGui::EndMenuBar();
			}

			if (m_resource->isEmpty()) {
				ImGui::TextUnformatted("Loading...");
				return;
			}

			if (!m_resource->isReady()) return;

			if (!SimpleUndoRedo::isReady()) pushUndo(NO_MERGE_UNDO);

			ShowAddCurveMenu(m_resource);

			if (m_resource->curves.empty()) return;

			ImGui::SameLine();
			float length = m_resource->length.seconds();
			ImGuiEx::Label("Length (s)");
			if (ImGui::DragFloat("##len", &length, 0.01f, 0, FLT_MAX)) {
				m_resource->length = Time::fromSeconds(length);
				saveUndo(true);
			}

			if (!ImGui::BeginTable("main", 2, ImGuiTableFlags_Resizable)) return;
			ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 200);
			ImGui::TableNextColumn();
			if (ImGui::BeginTable("left_col", 2, ImGuiTableFlags_Resizable)) {
				ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 30);
				for (int i = 0, n = m_resource->curves.size(); i < n; ++i) {
					ImGui::TableNextColumn();
					ImGui::PushID(i);
					if (ImGuiEx::IconButton(ICON_FA_TRASH, "Remove curve")) {
						m_resource->curves.erase(i);
						saveUndo(true);
						--n;
						ImGui::PopID();
						ImGui::TableNextColumn();
						continue;
					}
					ImGui::TableNextColumn();
					PropertyAnimation::Curve& curve = m_resource->curves[i];
					switch (curve.type) {
						case PropertyAnimation::CurveType::PROPERTY: {
							const char* cmp_name = m_app.getComponentTypeName(curve.cmp_type);
							StaticString<64> tmp(cmp_name, " - ", curve.property->name);
							if (ImGui::Selectable(tmp, m_selected_curve == i)) m_selected_curve = i;
							break;
						}
						case PropertyAnimation::CurveType::LOCAL_POS_X:
							if (ImGui::Selectable("Local position X", m_selected_curve == i)) m_selected_curve = i;
							break;
						case PropertyAnimation::CurveType::LOCAL_POS_Y:
							if (ImGui::Selectable("Local position Y", m_selected_curve == i)) m_selected_curve = i;
							break;
						case PropertyAnimation::CurveType::LOCAL_POS_Z:
							if (ImGui::Selectable("Local position Z", m_selected_curve == i)) m_selected_curve = i;
							break;
						case PropertyAnimation::CurveType::POS_X:
							if (ImGui::Selectable("Position X", m_selected_curve == i)) m_selected_curve = i;
							break;
						case PropertyAnimation::CurveType::POS_Y:
							if (ImGui::Selectable("Position Y", m_selected_curve == i)) m_selected_curve = i;
							break;
						case PropertyAnimation::CurveType::POS_Z:
							if (ImGui::Selectable("Position Z", m_selected_curve == i)) m_selected_curve = i;
							break;
						case PropertyAnimation::CurveType::SCALE_X:
							if (ImGui::Selectable("Scale X", m_selected_curve == i)) m_selected_curve = i;
							break;
						case PropertyAnimation::CurveType::SCALE_Y:
							if (ImGui::Selectable("Scale Y", m_selected_curve == i)) m_selected_curve = i;
							break;
						case PropertyAnimation::CurveType::SCALE_Z:
							if (ImGui::Selectable("Scale Z", m_selected_curve == i)) m_selected_curve = i;
							break;
						case PropertyAnimation::CurveType::NOT_SET: ASSERT(false); break;
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}

			ImGui::TableNextColumn();

			if (m_selected_curve >= m_resource->curves.size()) m_selected_curve = -1;
			if (m_selected_curve < 0) {
				ImGui::EndTable();
				return;
			}

			static ImVec2 size(-1, 200);
			
			PropertyAnimation::Curve& curve = m_resource->curves[m_selected_curve];
			ImVec2 points[64];
			ASSERT((u32)curve.frames.size() < lengthOf(points));
			for (int i = 0; i < curve.frames.size(); ++i) {
				points[i].x = curve.frames[i].seconds();
				points[i].y = curve.values[i];
			}

			int new_count;
			int flags = (int)ImGuiEx::CurveEditorFlags::NO_TANGENTS | (int)ImGuiEx::CurveEditorFlags::SHOW_GRID;
			if (m_fit_curve_in_editor)
			{
				flags |= (int)ImGuiEx::CurveEditorFlags::RESET;
				m_fit_curve_in_editor = false;
			}
			ImGui::SetNextItemWidth(-1);
			int changed_idx = ImGuiEx::CurveEditor("##curve", (float*)points, curve.frames.size(), lengthOf(points), size, flags, &new_count, &m_selected_point);
			if (changed_idx >= 0) {
				curve.frames[changed_idx] = Time::fromSeconds(points[changed_idx].x);
				curve.values[changed_idx] = points[changed_idx].y;
				curve.frames.back() = m_resource->length;
				curve.frames[0] = Time(0);
				saveUndo(true);
			}
			if (new_count != curve.frames.size())
			{
				curve.frames.resize(new_count);
				curve.values.resize(new_count);
				for (int i = 0; i < new_count; ++i)
				{
					curve.frames[i] = Time::fromSeconds(points[i].x);
					curve.values[i] = points[i].y;
				}
				saveUndo(true);
			}

			if (ImGui::BeginPopupContextItem("curve"))
			{
				if (ImGui::Selectable("Fit data")) m_fit_curve_in_editor = true;

				ImGui::EndPopup();
			}

			if (ImGui::BeginTable("curves_table", 2)) {
				ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
				ImGui::TableNextColumn();
				ImGui::Text("Time");
				ImGui::TableNextColumn();
				ImGui::Text("Value");
				for (u32 i = 0; i < (u32)curve.frames.size(); ++i) {
					ImGui::PushID(i);
					ImGui::TableNextRow();
					if (m_selected_point == i) {
						ImU32 row_bg_color = ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_TabSelected]);
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, row_bg_color);
					}
					ImGui::TableNextColumn();
					float f = curve.frames[i].seconds();
					ImGui::SetNextItemWidth(-1);
					if (ImGui::DragFloat("##f", &f)) {
						curve.frames[i] = Time::fromSeconds(f);
						saveUndo(true);
					}
					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(-1);
					saveUndo(ImGui::InputFloat("##v", &curve.values[i]));
					ImGui::PopID();
				}
				
				ImGui::EndTable();
			}
			
			ImGui::EndTable();
		}

		void saveUndo(bool changed) {
			if (!changed) return;

			pushUndo(ImGui::GetItemID());
			m_dirty = true;
		}

		void ShowAddCurveMenu(PropertyAnimation* animation) {
			if (ImGuiEx::IconButton(ICON_FA_PLUS_CIRCLE, "Add curve")) ImGui::OpenPopup("add_curve_popup");

			if (!ImGui::BeginPopup("add_curve_popup")) return;

			if (ImGui::BeginMenu("Transform")) {
				for (auto& v : g_transform_descs) {
					if (ImGui::MenuItem(v.label)) {
						PropertyAnimation::Curve& curve = animation->addCurve();
						curve.type = v.type;
						curve.frames.push(Time(0));
						curve.frames.push(animation->length);
						curve.values.push(0);
						curve.values.push(1);
					}
				}
				ImGui::EndMenu();
			}

			for (const reflection::RegisteredComponent& cmp_type : reflection::getComponents()) {
				const char* cmp_type_name = cmp_type.cmp->name;
				if (!hasFloatProperty(cmp_type.cmp)) continue;
				if (!ImGui::BeginMenu(cmp_type_name)) continue;

				const reflection::ComponentBase* component = cmp_type.cmp;
				struct : reflection::IEmptyPropertyVisitor
				{
					void visit(const reflection::Property<float>& prop) override
					{
						int idx = animation->curves.find([&](PropertyAnimation::Curve& rhs) {
							return rhs.cmp_type == cmp_type && rhs.property == &prop;
						});
						if (idx < 0 &&ImGui::MenuItem(prop.name))
						{
							PropertyAnimation::Curve& curve = animation->addCurve();
							curve.cmp_type = cmp_type;
							curve.property = &prop;
							curve.frames.push(Time(0));
							curve.frames.push(animation->length);
							curve.values.push(0);
							curve.values.push(1);
						}
					}
					PropertyAnimation* animation;
					ComponentType cmp_type;
				} visitor;

				visitor.animation = animation;
				visitor.cmp_type = cmp_type.cmp->component_type;
				component->visit(visitor);

				ImGui::EndMenu();
			}

			ImGui::EndPopup();
		}

		static bool hasFloatProperty(const reflection::ComponentBase* cmp) {
			struct : reflection::IEmptyPropertyVisitor {
				void visit(const reflection::Property<float>& prop) override { result = true; }
				bool result = false;
			} visitor;
			cmp->visit(visitor);
			return visitor.result;
		}

		const Path& getPath() override { return m_resource->getPath(); }
		const char* getName() const override { return "property animation editor"; }

		StudioApp& m_app;
		PropertyAnimation* m_resource;
		int m_selected_point = -1;
		int m_selected_curve = -1;
		bool m_fit_curve_in_editor = false;
	};

	explicit PropertyAnimationPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("anp", PropertyAnimation::TYPE);
	}

	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return "anp"; }
	void createResource(OutputMemoryStream& blob) override {}
	bool compile(const Path& src) override {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream src_data(m_app.getAllocator());
		if (!fs.getContentSync(src, src_data)) return false;
		
		StringView sv((const char*)src_data.data(), (u32)src_data.size());
		Tokenizer tokenizer(sv, src.c_str());
		Array<PropertyAnimation::Curve> curves(m_app.getAllocator());
		Time length;

		for (;;) {
			Tokenizer::Token token = tokenizer.tryNextToken();
			switch (token.type) {
				case Tokenizer::Token::EOF: goto tokenizer_finished;
				case Tokenizer::Token::ERROR: return false;
				case Tokenizer::Token::SYMBOL:
					if (token != "{") {
						logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected '{', got ", token.value);
						tokenizer.logErrorPosition(token.value.begin);
						return false;
					}
					break;
				default:
					logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected '{', got ", token.value);
					tokenizer.logErrorPosition(token.value.begin);
					return false;
			}

			// single curve
			PropertyAnimation::Curve curve(m_app.getAllocator());
			bool first = true;
			u32 version = 0;
			for (;;) {
				Tokenizer::Token key = tokenizer.nextToken();
				if (!key) return false;
				if (key == "}") {
					curves.push(static_cast<PropertyAnimation::Curve&&>(curve));
					continue;
				}
				
				if (!tokenizer.consume("=")) return false;

				if (key == "version") {
					if (!first) {
						logError(tokenizer.filename, "(", tokenizer.getLine(), "): 'version' must be first");
						tokenizer.logErrorPosition(key.value.begin);
						return false;
					}
					if (!tokenizer.consume(version)) return false;
					if (version > 1) {
						logError(tokenizer.filename, "(", tokenizer.getLine(), "): Unsupported version ", version);
						tokenizer.logErrorPosition(key.value.begin);
						return false;
					}
				}
				else if (key == "length") {
					u32 raw;
					if (!tokenizer.consume(raw)) return false;
					length = Time(raw);
				}
				else if (key == "type") {
					StringView value;
					if (!tokenizer.consume(value)) return false;
					curve.type = toCurveType(value);
				}
				else if (key == "component") {
					StringView value;
					if (!tokenizer.consume(value)) return false;
					curve.cmp_type = reflection::getComponentType(value);
				}
				else if (key == "property") {
					StringView value;
					if (!tokenizer.consume(value)) return false;
					curve.property = static_cast<const reflection::Property<float>*>(reflection::getProperty(curve.cmp_type, value));
				}
				else if (key == "keyframes") {
					if (version == 0) {
						if (!consumeNumberArray<true>(tokenizer, curve.frames)) return false;
					}
					else {
						if (!consumeNumberArray<false>(tokenizer, curve.frames)) return false;
					}
				}
				else if (key == "values") {
					if (!consumeNumberArray<false>(tokenizer, curve.values)) return false;
				}
				else {
					logError(tokenizer.filename, "(", tokenizer.getLine(), "): Unknown identifier ", key.value);
					tokenizer.logErrorPosition(key.value.begin);
					return false;
				}

				Tokenizer::Token next = tokenizer.nextToken();
				if (!next) return false;
				if (next == "}") {
					curves.push(static_cast<PropertyAnimation::Curve&&>(curve));
					break;
				}
				if (next != ",") {
					logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected ',' or '}', got ", next.value);
					tokenizer.logErrorPosition(next.value.begin);
					return false;
				}
				first = false;
			}
			token = tokenizer.tryNextToken();
			switch (token.type) {
				case Tokenizer::Token::EOF: goto tokenizer_finished;
				case Tokenizer::Token::ERROR: return false;
				case Tokenizer::Token::SYMBOL:
					if (!equalStrings(token.value, ",")) {
						logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected ',', got ", token.value);
						tokenizer.logErrorPosition(token.value.begin);
						return false;
					}
					break;
				default:
					logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected ',', got ", token.value);
					tokenizer.logErrorPosition(token.value.begin);
					return false;
			}
		}
		
		tokenizer_finished:

		OutputMemoryStream compiled(m_app.getAllocator());
		PropertyAnimation::Header header;
		compiled.write(header);
		if (length.raw() == 0) {
			for (PropertyAnimation::Curve& curve : curves) {
				if (curve.frames.empty()) continue;
				length = maximum(length, curve.frames.back());
			}
		}
		compiled.write(length);
		compiled.write((u32)curves.size());
		for (PropertyAnimation::Curve& curve : curves) {
			compiled.write(curve.type);
			if (curve.type == PropertyAnimation::CurveType::PROPERTY) {
				const char* cmp_typename = reflection::getComponent(curve.cmp_type)->name;
				compiled.writeString(cmp_typename);
				compiled.writeString(curve.property->name);
			}
			compiled.write((u32)curve.frames.size());
			for (Time frame : curve.frames) compiled.write(frame);
			for (float value : curve.values) compiled.write(value);
		}

		return m_app.getAssetCompiler().writeCompiledResource(src, compiled);
	}
	const char* getIcon() const override { return ICON_FA_CHART_LINE; }
	const char* getLabel() const override { return "Property animation"; }
	ResourceType getResourceType() const override { return PropertyAnimation::TYPE; }
	
	void openEditor(const Path& path) override {
		IAllocator& allocator = m_app.getAllocator();
		UniquePtr<EditorWindow> win = UniquePtr<EditorWindow>::create(allocator, path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	StudioApp& m_app;
};

struct AnimablePropertyGridPlugin final : PropertyGrid::IPlugin {
	explicit AnimablePropertyGridPlugin(StudioApp& app)
		: m_app(app)
	{
		m_is_playing = false;
	}


	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, const TextFilter& filter, WorldEditor& editor) override
	{
		if (filter.isActive()) return;
		if (cmp_type != types::animable) return;
		if (entities.length() != 1) return;

		const EntityRef entity = entities[0];
		auto* module = (AnimationModule*)editor.getWorld()->getModule(cmp_type);
		auto* animation = module->getAnimation(entity);
		if (!animation) return;
		if (!animation->isReady()) return;

		ImGui::Checkbox("Preview", &m_is_playing);
		float time = module->getAnimable(entity).time.seconds();
		if (ImGui::SliderFloat("Time", &time, 0, animation->getLength().seconds()))
		{
			module->getAnimable(entity).time = Time::fromSeconds(time);
			module->updateAnimable(entity, 0);
		}

		if (m_is_playing)
		{
			float time_delta = m_app.getEngine().getLastTimeDelta();
			module->updateAnimable(entity, time_delta);
		}

		if (ImGui::CollapsingHeader("Transformation"))
		{
			auto* render_module = (RenderModule*)module->getWorld().getModule(types::model_instance);
			if (module->getWorld().hasComponent(entity, types::model_instance))
			{
				const Pose* pose = render_module->lockPose(entity);
				Model* model = render_module->getModelInstanceModel(entity);
				if (pose && model)
				{
					ImGui::Columns(3);
					for (u32 i = 0; i < pose->count; ++i)
					{
						ImGuiEx::TextUnformatted(model->getBone(i).name);
						ImGui::NextColumn();
						ImGui::Text("%f; %f; %f", pose->positions[i].x, pose->positions[i].y, pose->positions[i].z);
						ImGui::NextColumn();
						ImGui::Text("%f; %f; %f; %f", pose->rotations[i].x, pose->rotations[i].y, pose->rotations[i].z, pose->rotations[i].w);
						ImGui::NextColumn();
					}
					ImGui::Columns();
				}
				if (pose) render_module->unlockPose(entity, false);
			}
		}
	}


	StudioApp& m_app;
	bool m_is_playing;
};


struct StudioAppPlugin : StudioApp::IPlugin {
	explicit StudioAppPlugin(StudioApp& app)
		: m_app(app)
		, m_animable_plugin(app)
		, m_animation_plugin(app)
		, m_prop_anim_plugin(app)
	{}

	const char* getName() const override { return "animation"; }

	void init() override {
		PROFILE_FUNCTION();
		AssetCompiler& compiler = m_app.getAssetCompiler();
		const char* anp_exts[] = { "anp" };
		const char* ani_exts[] = { "ani" };
		compiler.addPlugin(m_prop_anim_plugin, Span(anp_exts));

		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		asset_browser.addPlugin(m_animation_plugin, Span(ani_exts));
		asset_browser.addPlugin(m_prop_anim_plugin, Span(anp_exts));

		m_app.getPropertyGrid().addPlugin(m_animable_plugin);

		m_anim_editor = anim_editor::ControllerEditor::create(m_app);
	}

	bool showGizmo(WorldView&, ComponentUID) override { return false; }
	
	~StudioAppPlugin() {
		AssetCompiler& compiler = m_app.getAssetCompiler();
		compiler.removePlugin(m_prop_anim_plugin);

		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		asset_browser.removePlugin(m_animation_plugin);
		asset_browser.removePlugin(m_prop_anim_plugin);
		m_app.getPropertyGrid().removePlugin(m_animable_plugin);
	}


	StudioApp& m_app;
	AnimablePropertyGridPlugin m_animable_plugin;
	AnimationAssetBrowserPlugin m_animation_plugin;
	PropertyAnimationPlugin m_prop_anim_plugin;
	UniquePtr<anim_editor::ControllerEditor> m_anim_editor;
};


} // anonymous namespace


BLACK_STUDIO_ENTRY(animation) {
	PROFILE_FUNCTION();
	IAllocator& allocator = app.getAllocator();
	return BLACK_NEW(allocator, StudioAppPlugin)(app);
}

