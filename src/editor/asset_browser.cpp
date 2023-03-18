#include <imgui/imgui.h>

#include "asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/prefab_system.h"
#include "editor/render_interface.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/hash.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/world.h"
#include "utils.h"


namespace Lumix {

		
static void clampText(char* text, int width)
{
	char* end = text + stringLength(text);
	ImVec2 size = ImGui::CalcTextSize(text);
	if (size.x <= width) return;

	do
	{
		*(end - 1) = '\0';
		*(end - 2) = '.';
		*(end - 3) = '.';
		*(end - 4) = '.';
		--end;

		size = ImGui::CalcTextSize(text);
	} while (size.x > width && end - text > 4);
}

static const char* getResourceFilePath(const char* str)
{
	const char* c = str;
	while (*c && *c != ':') ++c;
	return *c != ':' ? str : c + 1;
}

static Span<const char> getSubresource(const char* str)
{
	Span<const char> ret;
	ret.m_begin = str;
	ret.m_end = str;
	while(*ret.m_end && *ret.m_end != ':') ++ret.m_end;
	return ret;
}

bool AssetBrowser::Plugin::createTile(const char* in_path, const char* out_path, ResourceType type)
{
	return false;
}

void AssetBrowser::Plugin::gui(Span<Resource*> resources) {
	bool waiting = false;
	for (const Resource* r : resources) {
		if (r->isEmpty()) {
			ImGui::TextUnformatted("Waiting for load...");
			waiting = true;
			break;
		}
	}

	if (!waiting) {	
		RuntimeHash hash = RuntimeHash(resources.begin(), resources.length() * sizeof(resources[0]));
		if (hash != m_current_hash) {
			// we remember undo only for currently selected resources
			m_current_hash = hash;
			clearUndoStack();
			m_defer_push_undo = true;
		}

		if (onGUI(resources)) {
			m_defer_push_undo = true;
		}
		else if (m_defer_push_undo) {
			pushUndo(SimpleUndoRedo::NO_MERGE_UNDO);
			m_defer_push_undo = false;
		}
	}
}

struct AssetBrowserImpl : AssetBrowser {
	struct FileInfo {
		StaticString<LUMIX_MAX_PATH> clamped_filename;
		StaticString<LUMIX_MAX_PATH> filepath;
		FilePathHash file_path_hash;
		void* tex = nullptr;
		bool create_called = false;
	};

	struct ImmediateTile : FileInfo {
		u32 gc_counter;
	};

	AssetBrowserImpl(StudioApp& app)
		: m_selected_resources(app.getAllocator())
		, m_history(app.getAllocator())
		, m_dir_history(app.getAllocator())
		, m_plugins(app.getAllocator())
		, m_app(app)
		, m_is_open(false)
		, m_show_thumbnails(true)
		, m_show_subresources(true)
		, m_file_infos(app.getAllocator())
		, m_immediate_tiles(app.getAllocator())
		, m_subdirs(app.getAllocator())
	{
		m_filter[0] = '\0';

		onBasePathChanged();

		m_back_action.init("Back", "Back in asset history", "back", ICON_FA_ARROW_LEFT, false);
		m_back_action.func.bind<&AssetBrowserImpl::goBack>(this);
		m_forward_action.init("Forward", "Forward in asset history", "forward", ICON_FA_ARROW_RIGHT, false);
		m_forward_action.func.bind<&AssetBrowserImpl::goForward>(this);

		m_toggle_ui.init("Asset browser", "Toggle Asset Browser UI", "asset_browser", "", false);
		m_toggle_ui.func.bind<&AssetBrowserImpl::toggleUI>(this);
		m_toggle_ui.is_selected.bind<&AssetBrowserImpl::isOpen>(this);

		m_undo_action.init(ICON_FA_UNDO "Undo", "Asset browser undo", "asset_browser_undo", ICON_FA_UNDO, os::Keycode::Z, Action::Modifiers::CTRL, true);
		m_undo_action.func.bind<&AssetBrowserImpl::undo>(this);
		m_undo_action.plugin = this;
		
		m_redo_action.init(ICON_FA_REDO "Redo", "Asset browser redo", "asset_browser_redo", ICON_FA_UNDO, os::Keycode::Z, Action::Modifiers::CTRL | Action::Modifiers::SHIFT, true);
		m_redo_action.func.bind<&AssetBrowserImpl::redo>(this);
		m_redo_action.plugin = this;

		m_app.addAction(&m_undo_action);
		m_app.addAction(&m_redo_action);
		m_app.addAction(&m_back_action);
		m_app.addAction(&m_forward_action);
		m_app.addWindowAction(&m_toggle_ui);
	}

	void onBasePathChanged() {
		const char* base_path = m_app.getEngine().getFileSystem().getBasePath();
		StaticString<LUMIX_MAX_PATH> path(base_path, ".lumix");
		bool success = os::makePath(path);
		path << "/asset_tiles";
		success = os::makePath(path) && success;
		if (!success) logError("Could not create ", path);
	}

	void releaseResources() override {
		unloadResources();
		RenderInterface* ri = m_app.getRenderInterface();
		for (FileInfo& info : m_file_infos) {
			ri->unloadTexture(info.tex);
		}
		m_file_infos.clear();
		for (FileInfo& info : m_immediate_tiles) {
			ri->unloadTexture(info.tex);
		}
		m_immediate_tiles.clear();
	}


	~AssetBrowserImpl() override
	{
		m_app.removeAction(&m_undo_action);
		m_app.removeAction(&m_redo_action);
		m_app.removeAction(&m_toggle_ui);
		m_app.removeAction(&m_back_action);
		m_app.removeAction(&m_forward_action);
		m_app.getAssetCompiler().listChanged().unbind<&AssetBrowserImpl::onResourceListChanged>(this);
		m_app.getAssetCompiler().resourceCompiled().unbind<&AssetBrowserImpl::onResourceCompiled>(this);

		ASSERT(m_plugins.size() == 0);
	}

	void onInitFinished() override {
		m_app.getAssetCompiler().listChanged().bind<&AssetBrowserImpl::onResourceListChanged>(this);
		m_app.getAssetCompiler().resourceCompiled().bind<&AssetBrowserImpl::onResourceCompiled>(this);
	}

	void onResourceCompiled(Resource& resource) {
		Span<const char> dir = Path::getDir(resource.getPath().c_str());
		if (!Path::isSame(dir, Span<const char>(m_dir, (u32)strlen(m_dir)))) return;
		
		RenderInterface* ri = m_app.getRenderInterface();
		Engine& engine = m_app.getEngine();
		FileSystem& fs = engine.getFileSystem();

		for (i32 i = 0; i < m_file_infos.size(); ++i) {
			FileInfo& info = m_file_infos[i];
			if (info.filepath != resource.getPath().c_str()) continue;
			
			switch (getState(info, fs)) {
				case TileState::DELETED:
					ri->unloadTexture(info.tex);
					info.create_called = false;
					m_file_infos.erase(i);
					return;
				case TileState::NOT_CREATED:
				case TileState::OUTDATED:
					ri->unloadTexture(info.tex);
					info.create_called = false;
					info.tex = nullptr;
					return;
				case TileState::OK: return;
			}
		}

		addTile(resource.getPath());
		sortTiles();
	}

	void onResourceListChanged(const Path& path) {
		Engine& engine = m_app.getEngine();
		FileSystem& fs = engine.getFileSystem();
		StaticString<LUMIX_MAX_PATH> fullpath(fs.getBasePath(), path.c_str());
		if (os::dirExists(fullpath)) {
			changeDir(m_dir, false);
			return;
		}

		Span<const char> dir = Path::getDir(path.c_str());
		if (!Path::isSame(dir, Span<const char>(m_dir, (u32)strlen(m_dir)))) return;

		RenderInterface* ri = m_app.getRenderInterface();

		for (i32 i = 0; i < m_file_infos.size(); ++i) {
			FileInfo& info = m_file_infos[i];
			if (info.filepath != path.c_str()) continue;
			
			switch (getState(info, fs)) {
				case TileState::DELETED:
					ri->unloadTexture(info.tex);
					info.create_called = false;
					m_file_infos.erase(i);
					return;
				case TileState::NOT_CREATED:
				case TileState::OUTDATED:
					ri->unloadTexture(info.tex);
					info.create_called = false;
					info.tex = nullptr;
					return;
				case TileState::OK: return;
			}
		}

		addTile(path);
		sortTiles();
	}

	void unloadResources()
	{
		if (m_selected_resources.empty()) return;

		for (Resource* res : m_selected_resources) {
			for (auto* plugin : m_plugins) {
				plugin->onResourceUnloaded(res);
			}
			res->decRefCount();
		}

		m_selected_resources.clear();
	}
	
	bool hasFocus() override { return m_has_focus; }

	void update(float) override
	{
		PROFILE_FUNCTION();

		RenderInterface* ri = m_app.getRenderInterface();
		for (i32 i = m_immediate_tiles.size() - 1; i >= 0; --i) {
			u32& counter = m_immediate_tiles[i].gc_counter;
			--counter;
			if (counter == 0) {
				ri->unloadTexture(m_immediate_tiles[i].tex);
				m_immediate_tiles.swapAndPop(i);
			}
		}

		for (auto* plugin : m_plugins) plugin->update();
	}

	void addTile(const Path& path) {
		if (!m_show_subresources && contains(path.c_str(), ':')) return;
		if (m_filter[0] && !stristr(path.c_str(), m_filter)) return;

		FileInfo tile;
		char filename[LUMIX_MAX_PATH];
		Span<const char> subres = getSubresource(path.c_str());
		if (*subres.end()) {
			copyNString(Span(filename), subres.begin(), subres.length());
			catString(filename, ":");
			catString(Span(filename), Path::getBasename(path.c_str()));
		}
		else {
			copyString(Span(filename), Path::getBasename(path.c_str()));
		}
		clampText(filename, int(TILE_SIZE * m_thumbnail_size));

		tile.file_path_hash = path.getHash();
		tile.filepath = path.c_str();
		tile.clamped_filename = filename;

		m_file_infos.push(tile);
	}

	void changeDir(const char* path, bool push_history)
	{
		Engine& engine = m_app.getEngine();
		RenderInterface* ri = m_app.getRenderInterface();
		for (FileInfo& info : m_file_infos) {
			ri->unloadTexture(info.tex);
		}
		m_file_infos.clear();

		Path::normalize(path, Span(m_dir.data));
		if (push_history) pushDirHistory(m_dir);
		int len = stringLength(m_dir);
		if (len > 0 && (m_dir[len - 1] == '/' || m_dir[len - 1] == '\\')) {
			m_dir.data[len - 1] = '\0';
		}

		FileSystem& fs = engine.getFileSystem();
		os::FileIterator* iter = fs.createFileIterator(m_dir);
		os::FileInfo info;
		m_subdirs.clear();
		while (os::getNextFile(iter, &info)) {
			if (!info.is_directory) continue;
			if (info.filename[0] != '.') m_subdirs.emplace(info.filename);
		}
		os::destroyFileIterator(iter);

		AssetCompiler& compiler = m_app.getAssetCompiler();
		char tmp[LUMIX_MAX_PATH];
		makeLowercase(Span(tmp), m_dir.data);
		const RuntimeHash dir_hash(equalStrings(".", tmp) ? "" : tmp);
		auto& resources = compiler.lockResources();
		if (m_filter[0]) {
			for (const AssetCompiler::ResourceItem& res : resources) {
				if (tmp[0] != '.' && tmp[1] != '\'' && !startsWithInsensitive(res.path.c_str(), tmp)) continue;
				addTile(res.path);
			}
		}
		else {
			for (const AssetCompiler::ResourceItem& res : resources) {
				if (res.dir_hash != dir_hash) continue;
				addTile(res.path);
			}
		}
		sortTiles();
		compiler.unlockResources();
	}

	void sortTiles() {
		qsort(m_file_infos.begin(), m_file_infos.size(), sizeof(m_file_infos[0]), [](const void* a, const void* b){
			FileInfo* m = (FileInfo*)a;
			FileInfo* n = (FileInfo*)b;
			return strcmp(m->filepath.data, n->filepath.data);
		});
	}

	void breadcrumbs()
	{
		const char* c = m_dir.data;
		char tmp[LUMIX_MAX_PATH];
		if (m_dir[0] != '.' || m_dir[1] != 0) {
			if (ImGui::Button(".")) {
				changeDir(".", true);
			}
			ImGui::SameLine();
			ImGui::TextUnformatted("/");
			ImGui::SameLine();
		}

		while (*c)
		{
			char* c_out = tmp;
			while (*c && *c != '/')
			{
				*c_out = *c;
				++c_out;
				++c;
			}
			*c_out = '\0';
			if (*c == '/') ++c;
			if (ImGui::Button(tmp))
			{
				char new_dir[LUMIX_MAX_PATH];
				copyNString(Span(new_dir), m_dir, int(c - m_dir.data));
				changeDir(new_dir, true);
			}
			ImGui::SameLine();
			ImGui::TextUnformatted("/");
			ImGui::SameLine();
		}
		ImGui::NewLine();
	}


	void dirColumn()
	{
		ImVec2 size(maximum(120.f, m_left_column_width), 0);
		ImGui::BeginChild("left_col", size);
		ImGui::PushItemWidth(120);
		bool b = false;
		if ((m_dir[0] != '.' || m_dir[1] != 0) && ImGui::Selectable("..", &b))
		{
			char dir[LUMIX_MAX_PATH];
			copyString(Span(dir), Path::getDir(m_dir));
			changeDir(dir, true);
		}

		for (auto& subdir : m_subdirs)
		{
			if (ImGui::Selectable(subdir, &b))
			{
				StaticString<LUMIX_MAX_PATH> new_dir(m_dir, "/", subdir);
				changeDir(new_dir, true);
			}
		}

		ImGui::PopItemWidth();
		ImGui::EndChild();
	}


	int getThumbnailIndex(int i, int j, int columns) const
	{
		int idx = j * columns + i;
		if (idx >= m_file_infos.size()) {
			return -1;
		}
		return idx;
	}


	void createTile(FileInfo& tile, const char* out_path)
	{
		if (tile.create_called) return;
	
		tile.create_called = true;
		const AssetCompiler& compiler = m_app.getAssetCompiler();
		for (Plugin* plugin : m_plugins) {
			ResourceType type = compiler.getResourceType(tile.filepath);
			if (plugin->createTile(tile.filepath, out_path, type)) break;
		}
	}

	enum class TileState {
		OK,
		OUTDATED,
		DELETED,
		NOT_CREATED
	};
	
	static TileState getState(const FileInfo& info, FileSystem& fs) {
		StaticString<LUMIX_MAX_PATH> path(".lumix/asset_tiles/", info.file_path_hash, ".lbc");
		if (!fs.fileExists(info.filepath)) return TileState::DELETED;
		if (!fs.fileExists(path)) return TileState::NOT_CREATED;

		StaticString<LUMIX_MAX_PATH> compiled_path(".lumix/resources/", info.file_path_hash, ".res");
		const u64 last_modified = fs.getLastModified(path);
		if (last_modified < fs.getLastModified(info.filepath) || last_modified < fs.getLastModified(compiled_path)) {
			return TileState::OUTDATED;
		}

		StaticString<LUMIX_MAX_PATH> meta_path(info.filepath, ".meta");
		if (fs.getLastModified(meta_path) > last_modified) {
			return TileState::OUTDATED;
		}

		return TileState::OK;
	}

	void thumbnail(FileInfo& tile, float size, bool selected)
	{
		ImGui::BeginGroup();
		ImVec2 img_size(size, size);
		RenderInterface* ri = m_app.getRenderInterface();
		if (tile.tex)
		{
			if(ri->isValid(tile.tex)) {
				ImGui::Image(*(void**)tile.tex, img_size);
			}
			else {
				ImGui::Dummy(img_size);
			}
		}
		else
		{
			ImGuiEx::Rect(img_size.x, img_size.y, 0xffffFFFF);
			StaticString<LUMIX_MAX_PATH> compiled_asset_path(".lumix/resources/", tile.file_path_hash, ".res");
			StaticString<LUMIX_MAX_PATH> path(".lumix/asset_tiles/", tile.file_path_hash, ".lbc");
			FileSystem& fs = m_app.getEngine().getFileSystem();
			switch (getState(tile, fs)) {
				case TileState::OK:
					tile.tex = ri->loadTexture(Path(path));
					break;
				case TileState::NOT_CREATED:
				case TileState::OUTDATED:
					createTile(tile, path);
					break;
				case TileState::DELETED:
					break;
			}
		}
		ImVec2 text_size = ImGui::CalcTextSize(tile.clamped_filename);
		ImVec2 pos = ImGui::GetCursorPos();
		pos.x += (size - text_size.x) * 0.5f;
		ImGui::SetCursorPos(pos);
		ImGui::Text("%s", tile.clamped_filename.data);
		ImGui::EndGroup();
		if (selected) {
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const u32 color = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
			dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), color, 0, 0, 3.f);
		}
	}

	void deleteTile(u32 idx) {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		StaticString<LUMIX_MAX_PATH> res_path(".lumix/resources/", m_file_infos[idx].file_path_hash, ".res");
		fs.deleteFile(res_path);
		if (!fs.deleteFile(m_file_infos[idx].filepath)) {
			logError("Failed to delete ", m_file_infos[idx].filepath);
		}
	}

	void reloadTile(FilePathHash hash) override {
		for (FileInfo& fi : m_file_infos) {
			if (fi.file_path_hash == hash) {
				m_app.getRenderInterface()->unloadTexture(fi.tex);
				fi.tex = nullptr;
				break;
			}
		}
	}

	void recreateTiles() {
		for (FileInfo& fi : m_file_infos) {
			StaticString<LUMIX_MAX_PATH> path(".lumix/asset_tiles/", fi.file_path_hash, ".res");
			createTile(fi, path);
		}
	}

	void fileColumn()
	{
		ImGui::BeginChild("main_col");

		float w = ImGui::GetContentRegionAvail().x;
		int columns = m_show_thumbnails ? (int)w / int(TILE_SIZE * m_thumbnail_size) : 1;
		columns = maximum(columns, 1);
		int tile_count = m_file_infos.size();
		int row_count = m_show_thumbnails ? (tile_count + columns - 1) / columns : tile_count;
	
		auto callbacks = [this](FileInfo& tile, int idx) {
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tile.filepath.data);
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
			{
				ImGui::Text("%s", (const char*)tile.filepath);
				ImGui::SetDragDropPayload("path", tile.filepath, stringLength(tile.filepath) + 1, ImGuiCond_Once);
				ImGui::EndDragDropSource();
			}
			else if (ImGui::IsItemHovered())
			{
				if (ImGui::IsMouseReleased(0)) {
					const bool additive = os::isKeyDown(os::Keycode::LSHIFT);
					selectResource(Path(tile.filepath), true, additive);
				}
				else if(ImGui::IsMouseReleased(1)) {
					m_context_resource = idx;
					ImGui::OpenPopup("item_ctx");
				}
			}
		};

		ImGuiListClipper clipper;
		clipper.Begin(row_count);
		while (clipper.Step())
		{
			for (int j = clipper.DisplayStart; j < clipper.DisplayEnd; ++j)
			{
				if (m_show_thumbnails)
				{
					for (int i = 0; i < columns; ++i)
					{
						if (i > 0) ImGui::SameLine();
						int idx = getThumbnailIndex(i, j, columns);
						if (idx < 0) {
							ImGui::NewLine();
							break;
						}
						FileInfo& tile = m_file_infos[idx];
						bool selected = m_selected_resources.find([&](Resource* res){ return res->getPath().getHash() == tile.file_path_hash; }) >= 0;
						thumbnail(tile, m_thumbnail_size * TILE_SIZE, selected);
						callbacks(tile, idx);
					}
				}
				else
				{
					FileInfo& tile = m_file_infos[j];
					bool b = m_selected_resources.find([&](Resource* res){ return res->getPath().getHash() == tile.file_path_hash; }) >= 0;
					ImGui::Selectable(tile.filepath, b);
					callbacks(tile, j);
				}
			}
		}

		bool open_delete_popup = false;
		FileSystem& fs = m_app.getEngine().getFileSystem();
		static char tmp[LUMIX_MAX_PATH] = "";
		auto common_popup = [&](){
			const char* base_path = fs.getBasePath();
			ImGui::Checkbox("Thumbnails", &m_show_thumbnails);
			if (ImGui::Checkbox("Subresources", &m_show_subresources)) changeDir(m_dir, false);
			if (ImGui::SliderFloat("Icon size", &m_thumbnail_size, 0.3f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
				refreshLabels();
			}
			
			if (ImGui::MenuItem("View in explorer")) {
				StaticString<LUMIX_MAX_PATH> dir_full_path(base_path, "/", m_dir);
				os::openExplorer(dir_full_path);
			}
			if (ImGui::BeginMenu("Create directory")) {
				ImGui::InputTextWithHint("##dirname", "New directory name", tmp, sizeof(tmp), ImGuiInputTextFlags_AutoSelectAll);
				ImGui::SameLine();
				if (ImGui::Button("Create")) {
					StaticString<LUMIX_MAX_PATH> path(base_path, "/", m_dir, "/", tmp);
					if (!os::makePath(path)) {
						logError("Failed to create ", path);
					}
					changeDir(m_dir, false);
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndMenu();
			}
			for (Plugin* plugin : m_plugins) {
				if (!plugin->canCreateResource()) continue;
				if (ImGui::BeginMenu(plugin->getName())) {
					bool input_entered = ImGui::InputTextWithHint("##name", "Name", tmp, sizeof(tmp), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
					ImGui::SameLine();
					if (ImGui::Button("Create") || input_entered) {
						StaticString<LUMIX_MAX_PATH> path(m_dir, "/", tmp, ".", plugin->getDefaultExtension());
						plugin->createResource(path);
						m_wanted_resource = path;
						ImGui::CloseCurrentPopup();
					}

					ImGui::EndMenu();
				}
			}
			if (ImGui::MenuItem("Select all")) {
				m_selected_resources.clear();
				m_selected_resources.reserve(m_file_infos.size());
				for (const FileInfo& fi : m_file_infos) {
					selectResource(Path(fi.filepath), false, true);
				}
			}
			if (ImGui::MenuItem("Recreate tiles")) {
				recreateTiles();
			}
		};
		if (ImGui::BeginPopup("item_ctx")) {
			ImGui::Text("%s", m_file_infos[m_context_resource].clamped_filename.data);
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_FA_EXTERNAL_LINK_ALT "Open externally")) {
				openInExternalEditor(m_file_infos[m_context_resource].filepath);
			}
			if (ImGui::BeginMenu("Rename")) {
				ImGui::InputTextWithHint("##New name", "New name", tmp, sizeof(tmp), ImGuiInputTextFlags_AutoSelectAll);
				if (ImGui::Button("Rename", ImVec2(100, 0))) {
					PathInfo fi(m_file_infos[m_context_resource].filepath);
					StaticString<LUMIX_MAX_PATH> new_path(fi.m_dir, tmp, ".", fi.m_extension);
					if (!fs.moveFile(m_file_infos[m_context_resource].filepath, new_path)) {
						logError("Failed to rename ", m_file_infos[m_context_resource].filepath, " to ", new_path);
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndMenu();
			}
			open_delete_popup = ImGui::MenuItem("Delete");
			ImGui::Separator();
			common_popup();
			ImGui::EndPopup();
		}
		else if (ImGui::BeginPopupContextWindow("context")) {
			common_popup();
			ImGui::EndPopup();
		}

		if (open_delete_popup) ImGui::OpenPopup("Delete file");

		if (ImGui::BeginPopupModal("Delete file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Are you sure? This can not be undone.");
			if (ImGui::Button("Yes, delete", ImVec2(100, 0))) {
				deleteTile(m_context_resource);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine(ImGui::GetWindowWidth() - 100 - ImGui::GetStyle().WindowPadding.x);
			if (ImGui::Button("Cancel", ImVec2(100, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::EndChild();
		if (ImGui::BeginDragDropTarget()) {
			if (auto* payload = ImGui::AcceptDragDropPayload("entity")) {
				const EntityRef e = *(EntityRef*)payload->Data;
				m_dropped_entity = e;
				ImGui::OpenPopup("Save as prefab");
				World* world = m_app.getWorldEditor().getWorld();
				const ComponentType model_inst_type = reflection::getComponentType("model_instance");
				IScene* scene = world->getScene(model_inst_type);
				if (scene && world->hasComponent(e, model_inst_type)) {
					Path source;
					if (reflection::getPropertyValue(*scene, e, model_inst_type, "Source", source)) {
						copyString(Span(m_prefab_name), Path::getBasename(source.c_str()));
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::SetNextWindowSizeConstraints(ImVec2(200, 100), ImVec2(FLT_MAX, FLT_MAX));
		if (ImGui::BeginPopupModal("Save as prefab")) {
			ImGuiEx::Label("Name");
			ImGui::InputText("##name", m_prefab_name, sizeof(m_prefab_name));
			if (ImGui::Selectable(ICON_FA_SAVE "Save")) {
				StaticString<LUMIX_MAX_PATH> path(m_dir, "/", m_prefab_name, ".fab");
				m_app.getWorldEditor().getPrefabSystem().savePrefab((EntityRef)m_dropped_entity, Path(path));
				m_dropped_entity = INVALID_ENTITY;
			}
			if (ImGui::Selectable(ICON_FA_TIMES "Cancel")) {
				m_dropped_entity = INVALID_ENTITY;
			}
			ImGui::EndPopup();
		}
	}

	void detailsGUI()
	{
		m_details_focused = false;
		if (!m_is_open) return;

		if (ImGui::Begin(INSPECTOR_NAME, &m_is_open, ImGuiWindowFlags_AlwaysVerticalScrollbar))
		{
			m_details_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			m_has_focus = m_has_focus || m_details_focused;

			ImVec2 pos = ImGui::GetCursorScreenPos();
			if (m_history.size() > 1) {
				if (ImGuiEx::BeginToolbar("asset_browser_toolbar", pos, ImVec2(0, 24)))
				{
					if (m_history_index > 0) m_back_action.toolbarButton(m_app.getBigIconFont());
					if (m_history_index < m_history.size() - 1) m_forward_action.toolbarButton(m_app.getBigIconFont());
				}
				ImGuiEx::EndToolbar();
			}

			if (m_selected_resources.empty())
			{
				ImGui::End();
				return;
			}

			if (m_selected_resources.size() == 1) {
				Resource* res = m_selected_resources[0];
				const char* path = res->getPath().c_str();
				ImGuiEx::Label("Selected resource");
				ImGui::TextUnformatted(path);
				ImGui::Separator();

				ImGuiEx::Label("Status");
				ImGui::TextUnformatted(res->isFailure() ? "failure" : (res->isReady() ? "Ready" : "Not ready"));
				ImGuiEx::Label("Compiled size");
				if (res->isReady()) {
					ImGui::Text("%.2f KB", res->size() / 1024.f);
				}
				else {
					ImGui::TextUnformatted("N/A");
				}
				const Span<const char> subres = getSubresource(m_selected_resources[0]->getPath().c_str());
				if (*subres.end()) {
					if (ImGui::Button("View parent")) {
						selectResource(Path(getResourceFilePath(m_selected_resources[0]->getPath().c_str())), true, false);
					}
				}
			}
			else {
				ImGui::Separator();
				ImGuiEx::Label("Selected resource");
				ImGui::TextUnformatted("multiple");
				ImGui::Separator();

				u32 ready = 0;
				u32 failed = 0;
				for (Resource* res : m_selected_resources) {
					ready += res->isReady() ? 1 : 0;
					failed += res->isFailure() ? 1 : 0;
				}

				ImGuiEx::Label("All");
				ImGui::Text("%d", m_selected_resources.size());
				ImGuiEx::Label("Ready");
				ImGui::Text("%d", ready);
				ImGuiEx::Label("Failed");
				ImGui::Text("%d", failed);
			}

			const ResourceType type = m_selected_resources[0]->getType();
			bool all_same_type = true;
			for (Resource* res : m_selected_resources) {
				all_same_type = all_same_type && res->getType() == type;
			}

			if (all_same_type) {
				auto iter = m_plugins.find(type);
				if (iter.isValid()) {
					ImGui::Separator();
					iter.value()->gui(m_selected_resources);
				}
			}
			else {
				ImGui::Text("Selected resources have different types.");
			}
		}
		ImGui::End();
	}

	void redo() {
		const ResourceType type = m_selected_resources[0]->getType();
		bool all_same_type = true;
		for (Resource* res : m_selected_resources) {
			all_same_type = all_same_type && res->getType() == type;
		}

		if (all_same_type) {
			auto iter = m_plugins.find(type);
			if (iter.isValid()) {
				iter.value()->redo();
			}
		}
	}
	
	void undo() {
		const ResourceType type = m_selected_resources[0]->getType();
		bool all_same_type = true;
		for (Resource* res : m_selected_resources) {
			all_same_type = all_same_type && res->getType() == type;
		}

		if (all_same_type) {
			auto iter = m_plugins.find(type);
			if (iter.isValid()) {
				iter.value()->undo();
			}
		}
	}

	void refreshLabels() {
		for (FileInfo& tile : m_file_infos) {
			char filename[LUMIX_MAX_PATH];
			Span<const char> subres = getSubresource(tile.filepath.data);
			if (*subres.end()) {
				copyNString(Span(filename), subres.begin(), subres.length());
				catString(filename, ":");
				catString(Span(filename), Path::getBasename(tile.filepath.data));
			} else {
				copyString(Span(filename), Path::getBasename(tile.filepath.data));
			}
			clampText(filename, int(TILE_SIZE * m_thumbnail_size));

			tile.clamped_filename = filename;
		}
	}

	const char* getName() const override { return "asset_browser"; }

	void checkExtendedMouseButtons() {
		if (!m_has_focus) return;
		
		for (const os::Event e : m_app.getEvents()) {
			if (e.type == os::Event::Type::MOUSE_BUTTON && !e.mouse_button.down) {
				switch (e.mouse_button.button) {
					case os::MouseButton::EXTENDED1: m_details_focused ? goBack() : goBackDir(); break;
					case os::MouseButton::EXTENDED2: m_details_focused ? goForward() : goForwardDir(); break;
					default: break;
				}
			}
		}
	}

	void onWindowGUI() override {
		m_has_focus = false;
		if (m_dir.data[0] == '\0') changeDir(".", true);

		if (!m_wanted_resource.isEmpty()) {
			selectResource(m_wanted_resource, true, false);
			m_wanted_resource = "";
		}

		if(m_is_open) {
			if (!ImGui::Begin(WINDOW_NAME, &m_is_open)) {
				ImGui::End();
				detailsGUI();
				return;
			}
			m_has_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || m_has_focus;

			ImGui::SetNextItemWidth(150);
			if (ImGui::InputTextWithHint("##search", ICON_FA_SEARCH " Search", m_filter, sizeof(m_filter), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) changeDir(m_dir, false);
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear search")) {
				m_filter[0] = '\0';
				changeDir(m_dir, false);
			}

			ImGui::SameLine();
			breadcrumbs();
			ImGui::Separator();

			float content_w = ImGui::GetContentRegionAvail().x;
			ImVec2 left_size(m_left_column_width, 0);
			if (left_size.x < 10) left_size.x = 10;
			if (left_size.x > content_w - 10) left_size.x = content_w - 10;
	
			dirColumn();

			ImGui::SameLine();
			ImGuiEx::VSplitter("vsplit1", &left_size);
			if (left_size.x >= 120) {
				m_left_column_width = left_size.x;
			}
			ImGui::SameLine();

			fileColumn();

			ImGui::End();
		}
	
		detailsGUI();
		checkExtendedMouseButtons();
	}

	void pushDirHistory(const char* path) {
		if (m_dir_history_index + 1 == m_dir_history.size() && !m_dir_history.empty()) {
			if (m_dir_history[m_dir_history_index] == path) return;
		}
		
		while (m_dir_history_index < m_dir_history.size() - 1) {
			m_dir_history.pop();
		}
		++m_dir_history_index;
		m_dir_history.push(Path(path));

		if (m_dir_history.size() > 20) {
			--m_dir_history_index;
			m_dir_history.erase(0);
		}
	}

	void selectResource(Resource* resource, bool record_history, bool additive)
	{
		if (record_history)
		{
			while (m_history_index < m_history.size() - 1)
			{
				m_history.pop();
			}
			m_history_index++;
			m_history.push(resource->getPath());

			if (m_history.size() > 20)
			{
				--m_history_index;
				m_history.erase(0);
			}
		}

		m_wanted_resource = "";
		if(additive) {
			if(m_selected_resources.indexOf(resource) >= 0) {
				m_selected_resources.swapAndPopItem(resource);
			}
			else {
				m_selected_resources.push(resource);
			}
		}
		else {
			unloadResources();
			m_selected_resources.push(resource);
		}
		const char* path = resource->getPath().c_str();
		ResourceLocator rl(Span(path, stringLength(path)));
		char dir[LUMIX_MAX_PATH];
		copyString(Span(dir), rl.dir);
		changeDir(dir, false);
		ASSERT(resource->getRefCount() > 0);
	}


	void removePlugin(Plugin& plugin) override
	{
		m_plugins.erase(plugin.getResourceType());
	}


	void addPlugin(Plugin& plugin) override
	{
		m_plugins.insert(plugin.getResourceType(), &plugin);
	}

	static void copyDir(const char* src, const char* dest, IAllocator& allocator)
	{
		PathInfo fi(src);
		StaticString<LUMIX_MAX_PATH> dst_dir(dest, "/", fi.m_basename);
		if (!os::makePath(dst_dir)) logError("Could not create ", dst_dir);
		os::FileIterator* iter = os::createFileIterator(src, allocator);

		os::FileInfo cfi;
		while(os::getNextFile(iter, &cfi)) {
			if (cfi.is_directory) {
				if (cfi.filename[0] != '.') {
					StaticString<LUMIX_MAX_PATH> tmp_src(src, "/", cfi.filename);
					StaticString<LUMIX_MAX_PATH> tmp_dst(dest, "/", fi.m_basename);
					copyDir(tmp_src, tmp_dst, allocator);
				}
			}
			else {
				StaticString<LUMIX_MAX_PATH> tmp_src(src, "/", cfi.filename);
				StaticString<LUMIX_MAX_PATH> tmp_dst(dest, "/", fi.m_basename, "/", cfi.filename);
				if(!os::copyFile(tmp_src, tmp_dst)) {
					logError("Failed to copy ", tmp_src, " to ", tmp_dst);
				}
			}
		}
		os::destroyFileIterator(iter);
	}

	bool onDropFile(const char* path)  override
	{
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (os::dirExists(path)) {
			StaticString<LUMIX_MAX_PATH> tmp(fs.getBasePath(), "/", m_dir, "/");
			IAllocator& allocator = m_app.getAllocator();
			copyDir(path, tmp, allocator);
		}
		PathInfo fi(path);
		StaticString<LUMIX_MAX_PATH> dest(fs.getBasePath(), "/", m_dir, "/", fi.m_basename, ".", fi.m_extension);
		return os::copyFile(path, dest);
	}

	static constexpr const char* WINDOW_NAME = ICON_FA_IMAGES "Assets##assets";
	static constexpr const char* INSPECTOR_NAME = ICON_FA_IMAGE "Asset inspector##asset_inspector";

	void selectResource(const Path& path, bool record_history, bool additive)  override
	{
		ImGui::SetWindowFocus(INSPECTOR_NAME);
		ImGui::SetWindowFocus(WINDOW_NAME);
		m_is_open = true;
		auto& manager = m_app.getEngine().getResourceManager();
		const AssetCompiler& compiler = m_app.getAssetCompiler();
		const ResourceType type = compiler.getResourceType(path.c_str());
		Resource* res = manager.load(type, path);
		if (res) selectResource(res, record_history, additive);
	}

	static StaticString<LUMIX_MAX_PATH> getImGuiLabelID(const ResourceLocator& rl, bool hash_id) {
		StaticString<LUMIX_MAX_PATH> res("");
		if (rl.full.length() > 0) {
			res << rl.subresource << (rl.subresource.length() > 0 ? ":" : "") << rl.basename << "." << rl.ext;
		}
		if (hash_id) {
			res << "##h" << RuntimeHash(rl.full.m_begin, rl.full.length()).getHashValue();
		}
		return res;
	}

	bool resourceInput(const char* str_id, Span<char> buf, ResourceType type, float width)  override
	{
		ImGui::PushID(str_id);

		const Span span(buf.m_begin, stringLength(buf.m_begin));
		const ResourceLocator rl(span);
	
		bool popup_opened = false;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
		if (span.length() == 0) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
			if (ImGui::Button("No resource (click to set)", ImVec2(width, 0))) {
				ImGui::OpenPopup("popup");
				popup_opened = true;
			}
		}
		else {
			float w = ImGui::CalcTextSize(ICON_FA_BULLSEYE ICON_FA_TRASH).x;
			if (ImGui::Button(getImGuiLabelID(rl, false).data, ImVec2(width < 0 ? -w : width - w, 0))) {
				ImGui::OpenPopup("popup");
				popup_opened = true;
			}
		}
		if (span.length() == 0) {
			ImGui::PopStyleColor();
		}
		if (span.length() > 0 && ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", buf.m_begin);
		}
	
		if (ImGui::BeginDragDropTarget()) {
			if (auto* payload = ImGui::AcceptDragDropPayload("path")) {
				char ext[10];
				const char* path = (const char*)payload->Data;
				Span<const char> subres = getSubresource(path);
				copyString(Span(ext), Path::getExtension(subres));
				const AssetCompiler& compiler = m_app.getAssetCompiler();
				if (compiler.acceptExtension(ext, type)) {
					copyString(buf, path);
					ImGui::EndDragDropTarget();
					ImGui::PopStyleVar();
					ImGui::PopID();
					return true;
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (span.length() > 0) {
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_BULLSEYE, "Go to")) {
				m_wanted_resource = buf.begin();
			}
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_TRASH, "Clear")) {
				copyString(buf, "");
				ImGui::PopStyleVar();
				ImGui::PopID();
				return true;
			}
		}
		ImGui::PopStyleVar();

		if (ImGuiEx::BeginResizablePopup("popup", ImVec2(200, 300))) {
			static FilePathHash selected_path_hash;
			if (popup_opened) ImGui::SetKeyboardFocusHere();
			if (resourceList(buf, selected_path_hash, type, true, true)) {
				ImGui::EndPopup();
				ImGui::PopID();
				return true;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
		return false;
	}


	void saveResource(Resource& resource, OutputMemoryStream& stream) override
	{
		FileSystem& fs = m_app.getEngine().getFileSystem();
		// use temporary because otherwise the resource is reloaded during saving
		StaticString<LUMIX_MAX_PATH> tmp_path(resource.getPath().c_str(), ".tmp");

		if (!fs.saveContentSync(Path(tmp_path), stream)) {
			logError("Could not save file ", resource.getPath());
			return;
		}

		Engine& engine = m_app.getEngine();
		StaticString<LUMIX_MAX_PATH> src_full_path;
		StaticString<LUMIX_MAX_PATH> dest_full_path;
		const char* base_path = engine.getFileSystem().getBasePath();
		src_full_path << base_path << tmp_path;
		dest_full_path << base_path << resource.getPath().c_str();

		os::deleteFile(dest_full_path);

		if (!os::moveFile(src_full_path, dest_full_path))
		{
			logError("Could not save file ", resource.getPath());
		}
	}

	void tile(const Path& path, bool selected) override {
		i32 idx = m_immediate_tiles.find([&path](const FileInfo& fi){
			return fi.file_path_hash == path.getHash();
		});
		if (idx < 0) {
			FileInfo& fi = m_immediate_tiles.emplace();
			fi.file_path_hash = path.getHash();
			fi.filepath = path.c_str();

			char filename[LUMIX_MAX_PATH];
			Span<const char> subres = getSubresource(path.c_str());
			if (*subres.end()) {
				copyNString(Span(filename), subres.begin(), subres.length());
				catString(filename, ":");
				catString(Span(filename), Path::getBasename(path.c_str()));
			}
			else {
				copyString(Span(filename), Path::getBasename(path.c_str()));
			}
			clampText(filename, 50);
			fi.clamped_filename = filename;
			fi.create_called = false;
			idx = m_immediate_tiles.size() - 1;
		}

		m_immediate_tiles[idx].gc_counter = 2;
		thumbnail(m_immediate_tiles[idx], 50.f, selected);
	}

	bool resourceList(Span<char> buf, FilePathHash& selected_path_hash, ResourceType type, bool can_create_new, bool enter_submit) const override {
		auto iter = m_plugins.find(type);
		if (!iter.isValid()) return false;

		Plugin* plugin = iter.value();

		static bool show_new_fs = false;
		if (can_create_new && plugin->canCreateResource() && ImGui::Selectable("New", false, ImGuiSelectableFlags_DontClosePopups)) {
			show_new_fs = true;
		}

		FileSelector& file_selector = m_app.getFileSelector();
		if (file_selector.gui("Save As", &show_new_fs, plugin->getDefaultExtension(), true)) {
			if (!plugin->createResource(file_selector.getPath())) {
				logError("Failed to create ", file_selector.getPath());
				return false;
			}
			copyString(buf, file_selector.getPath());
			return true;
		}

		static char filter[128] = "";
		ImGuiEx::filter("Filter", filter, sizeof(filter), 200);
		
		ImGui::BeginChild("Resources", ImVec2(0, 200), false, ImGuiWindowFlags_HorizontalScrollbar);
		AssetCompiler& compiler = m_app.getAssetCompiler();
	
		const auto& resources = compiler.lockResources();
		Path selected_path;
		for (const auto& res : resources) {
			if(res.type != type) continue;
			if (filter[0] != '\0' && stristr(res.path.c_str(), filter) == nullptr) continue;

			const bool selected = selected_path_hash == res.path.getHash();
			if(selected) selected_path = res.path;
			const Span span(res.path.c_str(), res.path.length());
			const ResourceLocator rl(span);
			StaticString<LUMIX_MAX_PATH> label = getImGuiLabelID(rl, true);
			const bool is_enter_submit = (enter_submit && ImGui::IsKeyPressed(ImGuiKey_Enter));
			if (is_enter_submit || ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
				selected_path_hash = res.path.getHash();
			
				if (selected || ImGui::IsMouseDoubleClicked(0) || is_enter_submit) {
					copyString(buf, res.path.c_str());
					ImGui::CloseCurrentPopup();
					ImGui::EndChild();
					compiler.unlockResources();
					return true;
				}
			}
		}
		ImGui::EndChild();
		ImGui::Separator();
		if (!selected_path.isEmpty()) {
			ImGui::TextWrapped("%s", selected_path.c_str());
		}
		compiler.unlockResources();
		return false;
	}


	void openInExternalEditor(Resource* resource) const override
	{
		openInExternalEditor(resource->getPath().c_str());
	}


	void openInExternalEditor(const char* path) const override
	{
		StaticString<LUMIX_MAX_PATH> full_path(m_app.getEngine().getFileSystem().getBasePath());
		full_path << path;
		const os::ExecuteOpenResult res = os::shellExecuteOpen(full_path);
		if (res == os::ExecuteOpenResult::NO_ASSOCIATION) {
			logError(full_path << " is not associated with any app.");
		}
		else if (res == os::ExecuteOpenResult::OTHER_ERROR) {
			logError("Failed to open ", full_path, " in exeternal editor.");
		}
	}

	void goBackDir() {
		if (m_dir_history_index < 1) return;
		m_dir_history_index = maximum(0, m_dir_history_index - 1);
		changeDir(m_dir_history[m_dir_history_index].c_str(), false);
	}

	void goForwardDir() {
		m_dir_history_index = minimum(m_dir_history_index + 1, m_dir_history.size() - 1);
		changeDir(m_dir_history[m_dir_history_index].c_str(), false);
	}

	void goBack() {
		if (m_history_index < 1) return;
		m_history_index = maximum(0, m_history_index - 1);
		selectResource(m_history[m_history_index], false, false);
	}

	void goForward() {
		m_history_index = minimum(m_history_index + 1, m_history.size() - 1);
		selectResource(m_history[m_history_index], false, false);
	}
	
	bool isOpen() const { return m_is_open; }
	void toggleUI() { m_is_open = !m_is_open; }
	
	void onSettingsLoaded() override { m_is_open = m_app.getSettings().m_is_asset_browser_open; }
	void onBeforeSettingsSaved() override { m_app.getSettings().m_is_asset_browser_open  = m_is_open; }

	bool copyTile(const char* from, const char* to) override {
		OutputMemoryStream img(m_app.getAllocator());
		if (!m_app.getEngine().getFileSystem().getContentSync(Path(from), img)) return false;
		
		os::OutputFile file;
		if (!m_app.getEngine().getFileSystem().open(to, file)) return false;
		Span<const char> ext = Path::getExtension(Span(from, stringLength(from)));
		if (ext.length() != 3) {
			file.close();
			return false;
		}
		char ext_tmp[4];
		makeLowercase(Span(ext_tmp), ext.begin());
		(void)file.write(ext_tmp, 3);
		(void)file.write(u32(0));
		(void)file.write(img.data(), img.size());
		file.close();
		return !file.isError();
	}

	bool m_is_open;
	float m_left_column_width = 120;
	StudioApp& m_app;
	StaticString<LUMIX_MAX_PATH> m_dir;
	Array<StaticString<LUMIX_MAX_PATH> > m_subdirs;
	Array<FileInfo> m_file_infos;
	Array<ImmediateTile> m_immediate_tiles;
	
	Array<Path> m_history;
	i32 m_history_index = -1;
	
	Array<Path> m_dir_history;
	i32 m_dir_history_index = -1;

	EntityPtr m_dropped_entity = INVALID_ENTITY;
	char m_prefab_name[LUMIX_MAX_PATH] = "";
	HashMap<ResourceType, Plugin*> m_plugins;
	Array<Resource*> m_selected_resources;
	int m_context_resource;
	char m_filter[128];
	Path m_wanted_resource;
	bool m_show_thumbnails;
	bool m_show_subresources;
	bool m_has_focus = false;
	bool m_details_focused = false;
	float m_thumbnail_size = 1.f;
	Action m_toggle_ui;
	Action m_back_action;
	Action m_forward_action;
	Action m_undo_action;
	Action m_redo_action;
};

UniquePtr<AssetBrowser> AssetBrowser::create(StudioApp& app) {
	return UniquePtr<AssetBrowserImpl>::create(app.getAllocator(), app);
}

} // namespace Lumix