#include <imgui/imgui.h>

#include "asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/prefab_system.h"
#include "editor/render_interface.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/universe.h"
#include "utils.h"


namespace Lumix {

template<>
struct HashFunc<ResourceType>
{
	static u32 get(const ResourceType& key)
	{
		return HashFunc<u32>::get(key.type);
	}
};
		
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

bool AssetBrowser::IPlugin::createTile(const char* in_path, const char* out_path, ResourceType type)
{
	return false;
}

struct AssetBrowserImpl : AssetBrowser {
	struct FileInfo
	{
		StaticString<LUMIX_MAX_PATH> clamped_filename;
		StaticString<LUMIX_MAX_PATH> filepath;
		u32 file_path_hash;
		void* tex = nullptr;
		bool create_called = false;
	};

	struct ImmediateTile : FileInfo{
		u32 gc_counter;
	};

	AssetBrowserImpl(StudioApp& app)
		: m_selected_resources(app.getAllocator())
		, m_is_focus_requested(false)
		, m_history(app.getAllocator())
		, m_plugins(app.getAllocator())
		, m_app(app)
		, m_is_open(false)
		, m_show_thumbnails(true)
		, m_show_subresources(true)
		, m_history_index(-1)
		, m_file_infos(app.getAllocator())
		, m_immediate_tiles(app.getAllocator())
		, m_filtered_file_infos(app.getAllocator())
		, m_subdirs(app.getAllocator())
	{
		IAllocator& allocator = app.getAllocator();
		m_filter[0] = '\0';

		onBasePathChanged();

		m_back_action.init("Back", "Back in asset history", "back", ICON_FA_ARROW_LEFT, false);
		m_back_action.func.bind<&AssetBrowserImpl::goBack>(this);
		m_forward_action.init("Forward", "Forward in asset history", "forward", ICON_FA_ARROW_RIGHT, false);
		m_forward_action.func.bind<&AssetBrowserImpl::goForward>(this);
		m_app.addAction(&m_back_action);
		m_app.addAction(&m_forward_action);
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
		m_app.removeAction(&m_back_action);
		m_app.removeAction(&m_forward_action);
		m_app.getAssetCompiler().listChanged().unbind<&AssetBrowserImpl::onResourceListChanged>(this);

		ASSERT(m_plugins.size() == 0);
	}

	void onInitFinished() override {
		m_app.getAssetCompiler().listChanged().bind<&AssetBrowserImpl::onResourceListChanged>(this);
	}

	void onResourceListChanged(const Path& path) {
		Span<const char> dir = Path::getDir(path.c_str());
		if (dir.length() > 0 && (*(dir.m_end - 1) == '/' || *(dir.m_end - 1) == '\\')) {
			--dir.m_end;
		}
		if (!equalStrings(dir, Span<const char>(m_dir, (u32)strlen(m_dir)))) return;

		Engine& engine = m_app.getEngine();
		FileSystem& fs = engine.getFileSystem();
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


	void update() override
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

	void changeDir(const char* path)
	{
		Engine& engine = m_app.getEngine();
		RenderInterface* ri = m_app.getRenderInterface();
		for (FileInfo& info : m_file_infos) {
			ri->unloadTexture(info.tex);
		}
		m_file_infos.clear();

		Path::normalize(path, Span(m_dir.data));
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
		const u32 dir_hash = crc32(m_dir);
		auto& resources = compiler.lockResources();
		for (const AssetCompiler::ResourceItem& res : resources) {
			if (res.dir_hash != dir_hash) continue;
			addTile(res.path);
		}
		sortTiles();
		compiler.unlockResources();

		doFilter();
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
				changeDir(".");
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
				changeDir(new_dir);
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
			changeDir(dir);
		}

		for (auto& subdir : m_subdirs)
		{
			if (ImGui::Selectable(subdir, &b))
			{
				StaticString<LUMIX_MAX_PATH> new_dir(m_dir, "/", subdir);
				changeDir(new_dir);
			}
		}

		ImGui::PopItemWidth();
		ImGui::EndChild();
	}


	void doFilter()
	{
		m_filtered_file_infos.clear();
		if (!m_filter[0]) return;

		for (int i = 0, c = m_file_infos.size(); i < c; ++i)
		{
			if (stristr(m_file_infos[i].filepath, m_filter)) m_filtered_file_infos.push(i);
		}
	}


	int getThumbnailIndex(int i, int j, int columns) const
	{
		int idx = j * columns + i;
		if (!m_filtered_file_infos.empty())
		{
			if (idx >= m_filtered_file_infos.size()) return -1;
			return m_filtered_file_infos[idx];
		}
		if (idx >= m_file_infos.size())
		{
			return -1;
		}
		return idx;
	}


	void createTile(FileInfo& tile, const char* out_path)
	{
		if (tile.create_called) return;
	
		tile.create_called = true;
		const AssetCompiler& compiler = m_app.getAssetCompiler();
		for (IPlugin* plugin : m_plugins) {
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
		StaticString<LUMIX_MAX_PATH> path(".lumix/asset_tiles/", info.file_path_hash, ".dds");
		if (!fs.fileExists(info.filepath)) return TileState::DELETED;
		if (!fs.fileExists(path)) return TileState::NOT_CREATED;

		StaticString<LUMIX_MAX_PATH> compiled_path(".lumix/assets/", info.file_path_hash, ".res");
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
			StaticString<LUMIX_MAX_PATH> compiled_asset_path(".lumix/assets/", tile.file_path_hash, ".res");
			StaticString<LUMIX_MAX_PATH> path(".lumix/asset_tiles/", tile.file_path_hash, ".dds");
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
		m_app.getAssetCompiler().removeResource(Path(m_file_infos[idx].filepath));
		StaticString<LUMIX_MAX_PATH> res_path(".lumix/assets/", m_file_infos[idx].file_path_hash, ".res");
		fs.deleteFile(res_path);
		if (!fs.deleteFile(m_file_infos[idx].filepath)) {
			logError("Failed to delete ", m_file_infos[idx].filepath);
		}
	}

	void reloadTile(u32 hash) override {
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
		int tile_count = m_filtered_file_infos.empty() ? m_file_infos.size() : m_filtered_file_infos.size();
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
					const int idx = (!m_filtered_file_infos.empty()) ? m_filtered_file_infos[j] : j;
					FileInfo& tile = m_file_infos[idx];
					bool b = m_selected_resources.find([&](Resource* res){ return res->getPath().getHash() == tile.file_path_hash; }) >= 0;
					ImGui::Selectable(tile.filepath, b);
					callbacks(tile, idx);
				}
			}
		}

		bool open_delete_popup = false;
		FileSystem& fs = m_app.getEngine().getFileSystem();
		static char tmp[LUMIX_MAX_PATH] = "";
		auto common_popup = [&](){
			const char* base_path = fs.getBasePath();
			if (ImGui::MenuItem("View in explorer")) {
				StaticString<LUMIX_MAX_PATH> dir_full_path(base_path, "/", m_dir);
				os::openExplorer(dir_full_path);
			}
			if (ImGui::BeginMenu("Create directory")) {
				ImGui::InputTextWithHint("##dirname", "New directory name", tmp, sizeof(tmp));
				ImGui::SameLine();
				if (ImGui::Button("Create")) {
					StaticString<LUMIX_MAX_PATH> path(base_path, "/", m_dir, "/", tmp);
					if (!os::makePath(path)) {
						logError("Failed to create ", path);
					}
					changeDir(m_dir);
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndMenu();
			}
			for (IPlugin* plugin : m_plugins) {
				if (!plugin->canCreateResource()) continue;
				if (ImGui::BeginMenu(plugin->getName())) {
					ImGui::InputTextWithHint("", "Name", tmp, sizeof(tmp));
					ImGui::SameLine();
					if (ImGui::Button("Create")) {
						StaticString<LUMIX_MAX_PATH> rel_path(m_dir, "/", tmp, ".", plugin->getDefaultExtension());
						StaticString<LUMIX_MAX_PATH> full_path(base_path, rel_path);
						plugin->createResource(full_path);
						m_wanted_resource = rel_path;
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndMenu();
				}
			}
			if (ImGui::MenuItem("Select all")) {
				m_selected_resources.clear();
				m_selected_resources.reserve(m_file_infos.size());
				if(m_filtered_file_infos.empty()) {
					for (const FileInfo& fi : m_file_infos) {
						selectResource(Path(fi.filepath), false, true);
					}
				}
				else {
					for (int i : m_filtered_file_infos) {
						selectResource(Path(m_file_infos[i].filepath), false, true);
					}
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
				ImGui::InputTextWithHint("##New name", "New name", tmp, sizeof(tmp));
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
				Universe* universe = m_app.getWorldEditor().getUniverse();
				const ComponentType model_inst_type = reflection::getComponentType("model_instance");
				IScene* scene = universe->getScene(model_inst_type);
				if (scene && universe->hasComponent(e, model_inst_type)) {
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
		if (!m_is_open) return;

		if (m_is_focus_requested) ImGui::SetNextWindowFocus();
		m_is_focus_requested = false;
		
		if (ImGui::Begin(ICON_FA_IMAGE  "Asset inspector##asset_inspector", &m_is_open, ImGuiWindowFlags_AlwaysVerticalScrollbar))
		{
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
				if (res->isReady()) {
					ImGuiEx::Label("Compiled size");
					ImGui::Text("%.2f KB", res->size() / 1024.f);
				}
				const Span<const char> subres = getSubresource(m_selected_resources[0]->getPath().c_str());
				if (*subres.end()) {
					if (ImGui::Button("View parent")) {
						selectResource(Path(getResourceFilePath(m_selected_resources[0]->getPath().c_str())), true, false);
					}
				}

				const AssetCompiler& compiler = m_app.getAssetCompiler();
				ResourceType resource_type = compiler.getResourceType(path);
				auto iter = m_plugins.find(resource_type);
				if (iter.isValid()) {
					ImGui::Separator();
					iter.value()->onGUI(m_selected_resources);
				}
			}
			else {
				ImGui::Separator();
				ImGuiEx::Label("Selected resource");
				ImGui::TextUnformatted("multiple");
				ImGui::Separator();

				u32 ready = 0;
				u32 failed = 0;
				const ResourceType type = m_selected_resources[0]->getType();
				bool all_same_type = true;
				for (Resource* res : m_selected_resources) {
					ready += res->isReady() ? 1 : 0;
					failed += res->isFailure() ? 1 : 0;
					all_same_type = all_same_type && res->getType() == type;
				}

				ImGuiEx::Label("All");
				ImGui::Text("%d", m_selected_resources.size());
				ImGuiEx::Label("Ready");
				ImGui::Text("%d", ready);
				ImGuiEx::Label("Failed");
				ImGui::Text("%d", failed);

				if (all_same_type) {
					auto iter = m_plugins.find(type);
					if(iter.isValid()) {
						iter.value()->onGUI(m_selected_resources);
					}
				}
				else {
					ImGui::Text("Selected resources have different types.");
				}
			}
		}
		ImGui::End();
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


	void onGUI() override {
		if (m_dir.data[0] == '\0') changeDir(".");

		if (!m_wanted_resource.isEmpty()) {
			selectResource(m_wanted_resource, true, false);
			m_wanted_resource = "";
		}

		m_is_open = m_is_open || m_is_focus_requested;

		if(m_is_open) {
			if (m_is_focus_requested) ImGui::SetNextWindowFocus();
			if (!ImGui::Begin(ICON_FA_IMAGES "Assets##assets", &m_is_open)) {
				ImGui::End();
				detailsGUI();
				return;
			}

			float checkbox_w = ImGui::GetCursorPosX();
			if (ImGuiEx::IconButton(ICON_FA_COG, "Settings")) {
				ImGui::OpenPopup("ab_settings");
			}
			if (ImGui::BeginPopup("ab_settings")) {
				ImGui::Checkbox("Thumbnails", &m_show_thumbnails);
				if (ImGui::Checkbox("Subresources", &m_show_subresources)) changeDir(m_dir);
				ImGui::SetNextItemWidth(100);
				if (ImGui::SliderFloat("Icon size", &m_thumbnail_size, 0.3f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
					refreshLabels();
				}
				ImGui::EndPopup();
			}

			ImGui::SameLine();
			checkbox_w = ImGui::GetCursorPosX() - checkbox_w;
			ImGui::PushItemWidth(100);
			if (ImGui::InputTextWithHint("##filter", "Filter", m_filter, sizeof(m_filter))) doFilter();
			ImGui::PopItemWidth();
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_TIMES, "Reset filter")) {
				m_filter[0] = '\0';
				doFilter();
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
		ASSERT(resource->getRefCount() > 0);
	}


	void removePlugin(IPlugin& plugin) override
	{
		m_plugins.erase(plugin.getResourceType());
	}


	void addPlugin(IPlugin& plugin) override
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

	void selectResource(const Path& path, bool record_history, bool additive)  override
	{
		m_is_focus_requested = true;
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
			res << "##h" << crc32(rl.full.m_begin, rl.full.length());
		}
		return res;
	}

	bool resourceInput(const char* str_id, Span<char> buf, ResourceType type)  override
	{
		ImGui::PushID(str_id);

		const Span span(buf.m_begin, stringLength(buf.m_begin));
		const ResourceLocator rl(span);
	
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
		if (span.length() == 0) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
			if (ImGui::Button("No resource (click to set)", ImVec2(-1.f, 0))) {
				ImGui::OpenPopup("popup");
			}
		}
		else {
			if (ImGui::Button(getImGuiLabelID(rl, false).data, ImVec2(-32.f, 0))) {
				ImGui::OpenPopup("popup");
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
				m_is_focus_requested = true;
				m_is_open = true;
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

		if (ImGuiEx::BeginResizablePopup("popup", ImVec2(300, 300))) {
			static u32 selected_path_hash = 0;
			if (resourceList(buf, selected_path_hash, type, 0, true)) {
				ImGui::EndPopup();
				ImGui::PopID();
				return true;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
		return false;
	}


	OutputMemoryStream* beginSaveResource(Resource& resource) override
	{
		IAllocator& allocator = m_app.getAllocator();
		return LUMIX_NEW(allocator, OutputMemoryStream)(allocator);
	}


	void endSaveResource(Resource& resource, OutputMemoryStream& stream, bool success) override
	{
		if (!success) return;
	
		FileSystem& fs = m_app.getEngine().getFileSystem();
		// use temporary because otherwise the resource is reloaded during saving
		StaticString<LUMIX_MAX_PATH> tmp_path(resource.getPath().c_str(), ".tmp");
		os::OutputFile f;
		if (!fs.open(tmp_path, f))
		{
			LUMIX_DELETE(m_app.getAllocator(), &stream);
			logError("Could not save file ", resource.getPath());
			return;
		}
		if (!f.write(stream.data(), stream.size())) {
			f.close();
			LUMIX_DELETE(m_app.getAllocator(), &stream);
			logError("Could not write file ", resource.getPath());
			return;
		}
		f.close();
		LUMIX_DELETE(m_app.getAllocator(), &stream);

		auto& engine = m_app.getEngine();
		StaticString<LUMIX_MAX_PATH> src_full_path;
		StaticString<LUMIX_MAX_PATH> dest_full_path;
		src_full_path.data[0] = 0;
		dest_full_path.data[0] = 0;
		src_full_path << engine.getFileSystem().getBasePath() << tmp_path;
		dest_full_path << engine.getFileSystem().getBasePath() << resource.getPath().c_str();

		os::deleteFile(dest_full_path);

		if (!os::moveFile(src_full_path, dest_full_path))
		{
			logError("Could not save file ", resource.getPath());
		}
	}

	void tile(const Path& path, bool selected) {
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
			clampText(filename, int(TILE_SIZE * m_thumbnail_size));
			fi.clamped_filename = filename;
			fi.create_called = false;
			idx = m_immediate_tiles.size() - 1;
		}

		m_immediate_tiles[idx].gc_counter = 2;
		thumbnail(m_immediate_tiles[idx], 50.f, selected);
	}

	bool resourceList(Span<char> buf, u32& selected_path_hash, ResourceType type, float height, bool can_create_new) const override {
		auto iter = m_plugins.find(type);
		if (!iter.isValid()) return false;

		FileSystem& fs = m_app.getEngine().getFileSystem();
		IPlugin* plugin = iter.value();
		if (can_create_new && plugin->canCreateResource() && ImGui::Selectable("New")) {
			char full_path[LUMIX_MAX_PATH];
			if (os::getSaveFilename(Span(full_path), plugin->getFileDialogFilter(), plugin->getFileDialogExtensions())) {
				if (fs.makeRelative(buf, full_path)) {
					if (plugin->createResource(full_path)) {
						return true;
					}
				}
				else {
					logError("Can not create ", full_path, " because it's outside of root directory.");
				}
			}
		}

		static char filter[128] = "";
		const float w = ImGui::CalcTextSize(ICON_FA_TIMES).x + ImGui::GetStyle().ItemSpacing.x * 2;
		ImGui::SetNextItemWidth(-w);
		ImGui::InputTextWithHint("##filter", "Filter", filter, sizeof(filter));
		ImGui::SameLine();
		if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
			filter[0] = '\0';
		}

		ImGui::BeginChild("Resources", ImVec2(0, height - ImGui::GetTextLineHeight() * 3), false, ImGuiWindowFlags_HorizontalScrollbar);
		AssetCompiler& compiler = m_app.getAssetCompiler();
	
		const auto& resources = compiler.lockResources();
		Path selected_path;
		for (const auto& res : resources) {
			if(res.type != type) continue;
			if (filter[0] != '\0' && strstr(res.path.c_str(), filter) == nullptr) continue;

			const bool selected = selected_path_hash == res.path.getHash();
			if(selected) selected_path = res.path;
			const Span span(res.path.c_str(), res.path.length());
			const ResourceLocator rl(span);
			StaticString<LUMIX_MAX_PATH> label = getImGuiLabelID(rl, true);
			if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
				selected_path_hash = res.path.getHash();
			
				if (selected || ImGui::IsMouseDoubleClicked(0)) { //-V1051
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
			ImGui::Text("%s", selected_path.c_str());
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


	void goBack()
	{
		if (m_history_index < 1) return;
		m_history_index = maximum(0, m_history_index - 1);
		selectResource(m_history[m_history_index], false, false);
	}


	void goForward()
	{
		m_history_index = minimum(m_history_index + 1, m_history.size() - 1);
		selectResource(m_history[m_history_index], false, false);
	}
	
	bool isOpen() const override { return m_is_open; }
	void setOpen(bool open) override { m_is_open = open; }

	bool m_is_open;
	float m_left_column_width = 120;
	StudioApp& m_app;
	StaticString<LUMIX_MAX_PATH> m_dir;
	Array<StaticString<LUMIX_MAX_PATH> > m_subdirs;
	Array<FileInfo> m_file_infos;
	Array<ImmediateTile> m_immediate_tiles;
	Array<int> m_filtered_file_infos;
	Array<Path> m_history;
	EntityPtr m_dropped_entity = INVALID_ENTITY;
	char m_prefab_name[LUMIX_MAX_PATH] = "";
	int m_history_index;
	HashMap<ResourceType, IPlugin*> m_plugins;
	Array<Resource*> m_selected_resources;
	int m_context_resource;
	char m_filter[128];
	Path m_wanted_resource;
	bool m_is_focus_requested;
	bool m_show_thumbnails;
	bool m_show_subresources;
	float m_thumbnail_size = 1.f;
	Action m_back_action;
	Action m_forward_action;
};

UniquePtr<AssetBrowser> AssetBrowser::create(StudioApp& app) {
	return UniquePtr<AssetBrowserImpl>::create(app.getAllocator(), app);
}

} // namespace Lumix