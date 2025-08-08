#include <imgui/imgui.h>

#include "asset_browser.h"
#include "core/defer.h"
#include "core/hash.h"
#include "core/log.h"
#include "core/os.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/sort.h"
#include "core/string.h"
#include "editor/action.h"
#include "editor/asset_compiler.h"
#include "editor/editor_asset.h"
#include "editor/prefab_system.h"
#include "editor/render_interface.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/text_filter.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/resource.h"
#include "engine/world.h"
#include "utils.h"


namespace Lumix {


static void clampText(char* text, int width) {
	char* end = text + stringLength(text);
	ImVec2 size = ImGui::CalcTextSize(text);
	if (size.x <= width) return;

	do {
		*(end - 1) = '\0';
		*(end - 2) = '.';
		*(end - 3) = '.';
		*(end - 4) = '.';
		--end;

		size = ImGui::CalcTextSize(text);
	} while (size.x > width && end - text > 4);
}

bool AssetBrowser::IPlugin::createTile(const char* in_path, const char* out_path, ResourceType type) {
	return false;
}

static ResourceType WORLD_TYPE("world");

struct WorldAssetPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	explicit WorldAssetPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("unv", WORLD_TYPE);
	}

	void addSubresources(AssetCompiler& compiler, const Path& path) override {
		compiler.addResource(WORLD_TYPE, path);
	}
	
	void openEditor(const Path& path) override { m_app.tryLoadWorld(path, ImGui::GetIO().KeyCtrl); }

	bool compile(const Path& src) override { return true; }
	const char* getLabel() const override { return "World"; }
	ResourceType getResourceType() const override { return WORLD_TYPE; }

	StudioApp& m_app;
};

static volatile bool once = [](){
	LUMIX_GLOBAL_FUNC(AssetBrowser::openEditor);
	return true;
}();


struct AssetBrowserImpl : AssetBrowser {
	struct FileInfo {
		StaticString<MAX_PATH> clamped_filename;
		Path filepath;
		void* tex = nullptr;
		bool create_called = false;
		u64 extension = 0;
		u32 score = 0;
	};

	struct ImmediateTile : FileInfo {
		u32 gc_counter;
	};

	AssetBrowserImpl(StudioApp& app)
		: m_allocator(app.getAllocator(), "asset browser")
		, m_selected_resources(m_allocator)
		, m_dir_history(m_allocator)
		, m_plugins(m_allocator)
		, m_plugin_map(m_allocator)
		, m_app(app)
		, m_is_open(false)
		, m_show_thumbnails(true)
		, m_show_subresources(true)
		, m_file_infos(m_allocator)
		, m_immediate_tiles(m_allocator)
		, m_subdirs(m_allocator)
		, m_windows(m_allocator)
		, m_world_asset_plugin(app)
	{
		PROFILE_FUNCTION();

		onBasePathChanged();

		const char* world_exts[] = { "unv" };
		addPlugin(m_world_asset_plugin, Span(world_exts));
		m_app.getAssetCompiler().addPlugin(m_world_asset_plugin, Span(world_exts));
		Settings& settings = m_app.getSettings();
		settings.registerOption("asset_browser_open", &m_is_open);
		settings.registerOption("asset_browser_thumbnails", &m_show_thumbnails, "Asset browser", "Show thumbnails");
		settings.registerOption("asset_browser_thumbnail_size", &m_thumbnail_size, "Asset browser", "Thumbnail size");
	}

	void onBasePathChanged() override {
		const char* base_path = m_app.getEngine().getFileSystem().getBasePath();
		Path path(base_path, ".lumix");
		bool success = os::makePath(path.c_str());
		path.append("/asset_tiles");
		success = os::makePath(path.c_str()) && success;
		if (!success) logError("Could not create ", path);
	}

	void releaseResources() override {
		m_selected_resources.clear();
		RenderInterface* ri = m_app.getRenderInterface();
		for (FileInfo& info : m_file_infos) {
			ri->unloadTexture(info.tex);
		}
		m_file_infos.clear();
		for (FileInfo& info : m_immediate_tiles) {
			ri->unloadTexture(info.tex);
		}
		m_immediate_tiles.clear();

		while (!m_windows.empty()) {
			closeWindow(*m_windows.last());
		}
	}

	~AssetBrowserImpl() override {
		removePlugin(m_world_asset_plugin);
		m_app.getAssetCompiler().removePlugin(m_world_asset_plugin);
		m_app.getAssetCompiler().listChanged().unbind<&AssetBrowserImpl::onResourceListChanged>(this);
		m_app.getAssetCompiler().resourceCompiled().unbind<&AssetBrowserImpl::onResourceCompiled>(this);

		ASSERT(m_plugins.size() == 0);
	}

	void selectAll() {
		m_selected_resources.clear();
		for (const FileInfo& info : m_file_infos) {
			m_selected_resources.push(info.filepath);
		}
	}

	void onInitFinished() override {
		m_app.getAssetCompiler().listChanged().bind<&AssetBrowserImpl::onResourceListChanged>(this);
		m_app.getAssetCompiler().resourceCompiled().bind<&AssetBrowserImpl::onResourceCompiled>(this);
	}

	void onResourceCompiled(Resource& resource, bool success) {
		if (!success) return;
		StringView dir = Path::getDir(resource.getPath());
		if (!Path::isSame(dir, m_dir)) return;
		
		RenderInterface* ri = m_app.getRenderInterface();
		Engine& engine = m_app.getEngine();
		FileSystem& fs = engine.getFileSystem();

		for (i32 i = 0; i < m_file_infos.size(); ++i) {
			FileInfo& info = m_file_infos[i];
			if (info.filepath != resource.getPath()) continue;
			
			const TileState state = getState(info, fs);
			switch (state) {
				case TileState::DELETED:
					ri->unloadTexture(info.tex);
					info.create_called = false;
					m_file_infos.erase(i);
					return;
				case TileState::OUTDATED:
				case TileState::NOT_CREATED:
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
		const Path fullpath(fs.getBasePath(), path);
		if (os::dirExists(fullpath)) {
			changeDir(m_dir, false);
			return;
		}

		StringView dir = Path::getDir(path);
		if (!Path::isSame(dir, m_dir) && m_dir != ".") return;

		RenderInterface* ri = m_app.getRenderInterface();

		for (i32 i = 0; i < m_file_infos.size(); ++i) {
			FileInfo& info = m_file_infos[i];
			if (info.filepath != path) continue;
			
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
		if (!m_show_subresources && contains(path, ':')) return;
		FileInfo tile;
		tile.score = m_filter.passWithScore(path);
		if (tile.score == 0) return;

		StaticString<MAX_PATH> filename;
		StringView subres = ResourcePath::getSubresource(path);
		if (*subres.end) {
			filename.append(subres, ":", Path::getBasename(path));
		}
		else {
			filename = Path::getBasename(path);
		}

		if (!m_show_thumbnails) {
			filename.append(".", Path::getExtension(path));
		}

		tile.filepath = path;
		tile.clamped_filename = filename;
		StringView ext = Path::getExtension(subres);
		tile.extension = 0;
		ASSERT(ext.size() <= sizeof(tile.extension));
		memcpy(&tile.extension, ext.begin, ext.size());

		m_file_infos.push(tile);
	}

	void changeDir(StringView path, bool push_history) {
		m_selected_resources.clear();

		Engine& engine = m_app.getEngine();
		RenderInterface* ri = m_app.getRenderInterface();
		for (FileInfo& info : m_file_infos) {
			ri->unloadTexture(info.tex);
		}
		m_file_infos.clear();

		if (!path.empty() && (path.back() == '\\' || path.back() == '/')) path.removeSuffix(1);
		m_dir = path;
		if (push_history) pushDirHistory(m_dir);

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
		const RuntimeHash dir_hash(equalStrings(".", m_dir) ? "" : m_dir.c_str());
		auto& resources = compiler.lockResources();
		if (m_filter.isActive()) {
			for (const AssetCompiler::ResourceItem& res : resources) {
				if (m_dir != "." && !startsWithInsensitive(ResourcePath::getResource(res.path), m_dir)) continue;
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
		if (m_filter.isActive()) {
			sort(m_file_infos.begin(), m_file_infos.end(), [](const FileInfo& a, const FileInfo& b){
				if (a.score < b.score) return true;
				return compareStringInsensitive(a.filepath, b.filepath) < 0;
			});
		}
		else {
			sort(m_file_infos.begin(), m_file_infos.end(), [](const FileInfo& a, const FileInfo& b){
				return compareStringInsensitive(a.filepath, b.filepath) < 0;
			});
		}
	}

	void breadcrumbs()
	{
		const char* c = m_dir.c_str();
		char tmp[MAX_PATH];
		if (m_dir != ".") {
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
			if (ImGui::Button(tmp)){
				changeDir(StringView(m_dir.c_str(), u32(c - m_dir.c_str())), true);
			}
			ImGui::SameLine();
			ImGui::TextUnformatted("/");
			ImGui::SameLine();
		}
		ImGui::NewLine();
	}


	void dirColumn() {
		ImGui::BeginChild("left_col");
		bool b = false;
		if (m_dir != "." && ImGui::Selectable("..", &b)) {
			changeDir(Path::getDir(m_dir), true);
		}

		for (const Path& subdir : m_subdirs) {
			if (ImGui::Selectable(subdir.c_str(), &b)) {
				Path new_dir(m_dir, "/", subdir);
				changeDir(new_dir, true);
			}
		}

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


	void createTile(FileInfo& tile, const char* out_path) {
		if (m_create_tile_cooldown > 0) return;
		if (tile.create_called) return;
	
		tile.create_called = true;
		const AssetCompiler& compiler = m_app.getAssetCompiler();
		const ResourceType type = compiler.getResourceType(tile.filepath);
		for (IPlugin* plugin : m_plugins) {
			if (plugin->createTile(tile.filepath.c_str(), out_path, type)) break;
		}
	}

	enum class TileState {
		OK,
		OUTDATED,
		DELETED,
		NOT_CREATED
	};
	
	static TileState getState(const FileInfo& info, FileSystem& fs) {
		const u64 file_path_hash = info.filepath.getHash().getHashValue();
		const Path path(".lumix/asset_tiles/", file_path_hash, ".lbc");
		if (!fs.fileExists(info.filepath)) {
			StringView master = ResourcePath::getResource(info.filepath);
			if (master.begin == info.filepath.c_str()) {
				return TileState::DELETED;
			}
		}
		if (!fs.fileExists(path)) return TileState::NOT_CREATED;

		const Path compiled_path(".lumix/resources/", file_path_hash, ".res");
		const u64 last_modified = fs.getLastModified(path);
		if (last_modified < fs.getLastModified(info.filepath) || last_modified < fs.getLastModified(compiled_path)) {
			return TileState::OUTDATED;
		}

		Path meta_path(info.filepath, ".meta");
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
			//ImGuiEx::Rect(img_size.x, img_size.y, 0xfff00FFF);

			ImDrawList* dl = ImGui::GetWindowDrawList();
			ImVec2 screen_pos = ImGui::GetCursorScreenPos();
			ImVec2 end_pos = screen_pos + img_size;
			dl->AddRectFilled(screen_pos, end_pos, 0xffFFffFF);
			
			auto iter = m_plugin_map.find(tile.extension);
			if (iter.isValid()) {
				const char* label = iter.value()->getLabel();
				ImGui::PushFont(m_app.getBoldFont());
				float wrap_width = img_size.x * 0.9f;
				const ImVec2 text_size = ImGui::CalcTextSize(label, nullptr, false, wrap_width);
				
				dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), screen_pos + (img_size - text_size) * 0.5f, IM_COL32(0, 0, 0, 0xff), label, nullptr, wrap_width);
				ImGui::PopFont();
			}

			ImGui::Dummy(img_size);

			const Path path(".lumix/asset_tiles/", tile.filepath.getHash().getHashValue(), ".lbc");
			FileSystem& fs = m_app.getEngine().getFileSystem();
			switch (getState(tile, fs)) {
				case TileState::OK:
					tile.tex = ri->loadTexture(path);
					break;
				case TileState::NOT_CREATED:
				case TileState::OUTDATED:
					createTile(tile, path.c_str());
					break;
				case TileState::DELETED:
					break;
			}
		}
		ImVec2 text_size = ImGui::CalcTextSize(tile.clamped_filename);
		if (text_size.x > int(TILE_SIZE * m_thumbnail_size)) {
			clampText(tile.clamped_filename.data, int(TILE_SIZE * m_thumbnail_size));
			text_size = ImGui::CalcTextSize(tile.clamped_filename);
		}
		ImVec2 pos = ImGui::GetCursorPos();
		pos.x += (size - text_size.x) * 0.5f;
		ImGui::SetCursorPos(pos);
		ImGuiEx::TextUnformatted(tile.clamped_filename);
		ImGui::EndGroup();
		if (selected) {
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const u32 color = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
			dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), color, 0, 0, 3.f);
		}
	}

	void deleteSelectedFiles() {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		for (const Path& path : m_selected_resources) {
			const Path res_path(".lumix/resources/", path.getHash().getHashValue(), ".res");
			fs.deleteFile(res_path);
			if (!fs.deleteFile(path)) {
				logError("Failed to delete ", path);
			}
		}
		m_selected_resources.clear();
	}

	void reloadTile(FilePathHash hash) override {
		for (FileInfo& fi : m_file_infos) {
			if (fi.filepath.getHash() == hash) {
				m_app.getRenderInterface()->unloadTexture(fi.tex);
				fi.tex = nullptr;
				break;
			}
		}
	}

	void recreateTiles() {
		for (FileInfo& fi : m_file_infos) {
			const Path path(".lumix/asset_tiles/", fi.filepath.getHash().getHashValue(), ".res");
			fi.create_called = false;
			createTile(fi, path.c_str());
		}
	}
	
	Span<const Path> getSelectedResources() override {
		return m_selected_resources;
	}

	IPlugin* getPluginFor(const Path& path) {
		StringView ext = Path::getExtension(ResourcePath::getSubresource(path));
		char ext_str[9];
		makeLowercase(ext_str, ext);
		u64 key = 0;
		ASSERT(ext.size() <= sizeof(key));
		memcpy(&key, ext_str, ext.size());
		auto iter = m_plugin_map.find(key);
		return iter.isValid() ? iter.value() : nullptr;
	}

	bool canMultiEdit() {
		if (m_selected_resources.size() < 2) return false;

		const AssetCompiler& compiler = m_app.getAssetCompiler();
		const ResourceType type = compiler.getResourceType(m_selected_resources[0]);

		IPlugin* plugin = getPluginFor(m_selected_resources[0]);
		if (!plugin) return false;
		if (!plugin->canMultiEdit()) return false;

		for (const Path& path : m_selected_resources) {
			const ResourceType other_type = compiler.getResourceType(path);
			if (other_type != type) return false;
		}
		return true;
	}

	void openMultiEdit() {
		ASSERT(m_selected_resources.size() > 1);
		IPlugin* plugin = getPluginFor(m_selected_resources[0]);
		ASSERT(plugin);
		ASSERT(plugin->canMultiEdit());
		plugin->openMultiEditor(m_selected_resources);
	}

	void fileColumn() {
		ImGui::BeginChild("main_col");

		float w = ImGui::GetContentRegionAvail().x;
		int columns = m_show_thumbnails ? (int)w / int(TILE_SIZE * m_thumbnail_size) : 1;
		columns = maximum(columns, 1);
		int tile_count = m_file_infos.size();
		int row_count = m_show_thumbnails ? (tile_count + columns - 1) / columns : tile_count;
	
		auto callbacks = [this](FileInfo& tile, int idx, bool is_selected) {
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tile.filepath.c_str());
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
			{
				if (m_selected_resources.size() > 1 && is_selected) {
					ImGui::Text("%d files", m_selected_resources.size());
					ImGui::SetDragDropPayload("asset_browser_selection", nullptr, 0, ImGuiCond_Once);
					ImGui::EndDragDropSource();
				}
				else {
					ImGuiEx::TextUnformatted(tile.filepath);
					ImGui::SetDragDropPayload("path", tile.filepath.c_str(), tile.filepath.length() + 1, ImGuiCond_Once);
					ImGui::EndDragDropSource();
				}
			}
			else if (ImGui::IsItemHovered()){
				if (ImGui::IsMouseDoubleClicked(0)) {
					openEditor(tile.filepath);
				}
				else if (ImGui::IsMouseReleased(0)) {
					if (os::isKeyDown(os::Keycode::SHIFT) && !m_selected_resources.empty()) {
						selectRange(m_selected_resources.back(), tile.filepath);
					}
					else {
						const bool additive = os::isKeyDown(os::Keycode::CTRL);
						selectResource(tile.filepath, additive);
					}
				}
				else if(ImGui::IsMouseReleased(1)) {
					if (m_selected_resources.indexOf(tile.filepath) < 0) m_selected_resources.clear();
					if (m_selected_resources.empty()) selectResource(tile.filepath, false);
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
						bool selected = m_selected_resources.find([&](const Path& p){ return p == tile.filepath; }) >= 0;
						thumbnail(tile, m_thumbnail_size * TILE_SIZE, selected);
						callbacks(tile, idx, selected);
					}
				}
				else
				{
					FileInfo& tile = m_file_infos[j];
					bool b = m_selected_resources.find([&](const Path& p){ return p == tile.filepath; }) >= 0;
					ImGui::PushID(&tile);
					ImGui::Selectable(tile.clamped_filename, b);
					callbacks(tile, j, b);
					ImGui::PopID();
				}
			}
		}

		bool open_rename_popup = false;
		bool open_create_dir_popup = false;
		bool open_delete_popup = false;
		FileSystem& fs = m_app.getEngine().getFileSystem();
		static char tmp[MAX_PATH] = "";
		const char* base_path = fs.getBasePath();
		IPlugin* create_resource_plugin = nullptr;
		auto common_popup = [&](){
			if (ImGui::Checkbox("Subresources", &m_show_subresources)) {
				ImGui::CloseCurrentPopup();
				changeDir(m_dir, false);
			}
			
			if (ImGui::MenuItem("View in explorer")) {
				const Path dir_full_path(base_path, "/", m_dir);
				os::openExplorer(dir_full_path);
			}
			if (ImGui::MenuItem("Create directory")) {
				tmp[0] = 0;
				open_create_dir_popup = true;
			}

			if (canMultiEdit() && ImGui::MenuItem("Multiedit")) openMultiEdit();

			if (ImGui::MenuItem("Recreate tiles")) {
				recreateTiles();
			}
			if (ImGui::BeginMenu("Create")) {
				for (IPlugin* plugin : m_plugins) {
					if (!plugin->canCreateResource()) continue;
					if (ImGui::MenuItem(plugin->getLabel())) create_resource_plugin = plugin;
				}
				ImGui::EndMenu();
			}
		};

		if (ImGui::BeginPopup("item_ctx")) {
			if (m_selected_resources.empty()) {
				ImGui::CloseCurrentPopup();
			}
			else {
				if (m_selected_resources.size() > 1) {
					ImGui::Text("%d files selected", m_selected_resources.size());
				}
				else {
					ImGuiEx::TextUnformatted(Path::getBasename(m_selected_resources[0]));
					ImGui::Separator();
					if (*ResourcePath::getSubresource(m_selected_resources[0]).end == 0 && ImGui::MenuItem(ICON_FA_EXTERNAL_LINK_ALT "Open externally")) {
						openInExternalEditor(m_selected_resources[0]);
					}
					if (ImGui::MenuItem("Rename")) {
						copyString(tmp, Path::getBasename(m_selected_resources[0]));
						open_rename_popup = true;
					}
				}
				open_delete_popup = ImGui::MenuItem("Delete");
				ImGui::Separator();
				common_popup();
			}
			ImGui::EndPopup();
		}
		else if (ImGui::BeginPopupContextWindow("context")) {
			common_popup();
			ImGui::EndPopup();
		}

		static IPlugin* plugin;
		if (create_resource_plugin) {
			plugin = create_resource_plugin;
			openCenterStrip("create_resource");
			tmp[0] = 0;
		}
		if (beginCenterStrip("create_resource")) {
			bool create = false;
			ImGui::NewLine();
			alignGUICenter([&](){
				if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
				create = ImGui::InputTextWithHint("##name", "Name", tmp, sizeof(tmp), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
			});
			alignGUICenter([&](){
				if (ImGui::Button("Create")) create = true;
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			});
			if (create) {
				OutputMemoryStream blob(m_allocator);
				plugin->createResource(blob);
				Path path(m_dir, "/", tmp, ".", plugin->getDefaultExtension());
				if (!fs.saveContentSync(path, blob)) {
					logError("Failed to write ", path);
				}
				selectResource(path, false);
				ImGui::CloseCurrentPopup();
			}
			endCenterStrip();
		}

		// rename
		if (open_rename_popup || m_request_rename) {
			if (m_request_rename) copyString(tmp, Path::getBasename(m_selected_resources[0]));
			openCenterStrip("rename");
			m_request_rename = false;
		}
		if (beginCenterStrip("rename")) {
			if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
			ImGui::NewLine();
			alignGUICenter([](){
				if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
				ImGui::InputTextWithHint("##name", "New name", tmp, sizeof(tmp), ImGuiInputTextFlags_AutoSelectAll);
			});
			bool rename_submit = ImGui::IsKeyPressed(ImGuiKey_Enter);
			alignGUICenter([&](){
				if (ImGui::Button("Rename")) rename_submit = true;
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			});
			if (rename_submit) {
				PathInfo fi(m_selected_resources[0]);
				const Path new_path(fi.dir, tmp, ".", fi.extension);
				if (!fs.moveFile(m_selected_resources[0], new_path)) {
					logError("Failed to rename ", m_selected_resources[0], " to ", new_path);
				}
				ImGui::CloseCurrentPopup();
			}
			endCenterStrip();
		}
		
		// create dir
		if (open_create_dir_popup) openCenterStrip("create_dir");
		if (beginCenterStrip("create_dir")) {
			if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
			ImGui::NewLine();
			bool input_entered = false;
			alignGUICenter([&](){
				if (open_create_dir_popup) ImGui::SetKeyboardFocusHere();
				input_entered = ImGui::InputTextWithHint("##dirname", "New directory name", tmp, sizeof(tmp), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
			});
			alignGUICenter([&](){
				if (input_entered || ImGui::Button("Create")) {
					input_entered = false;
					StaticString<MAX_PATH> path(base_path, "/", m_dir, "/", tmp);
					if (!os::makePath(path)) {
						logError("Failed to create ", path);
					}
					changeDir(StaticString<MAX_PATH>(m_dir, "/", tmp), false);
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			});
			endCenterStrip();
		}

		// delete
		if (open_delete_popup || m_request_delete) {
			openCenterStrip("Delete file");
			m_request_delete = false;
		}

		if (beginCenterStrip("Delete file", 7)) {
			ImGui::NewLine();
			if (m_selected_resources.size() > 1) {
				StaticString<128> txt(m_selected_resources.size(), " files will be deleted.");
				ImGuiEx::TextCentered(txt);
			}
			else {
				ImGuiEx::TextCentered(m_selected_resources[0].c_str());
			}
			ImGuiEx::TextCentered("Are you sure? This can not be undone.");
			ImGui::NewLine();
			alignGUICenter([&](){
				if (ImGui::Button("Delete", ImVec2(100, 0))) {
					deleteSelectedFiles();
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(100, 0))) {
					ImGui::CloseCurrentPopup();
				}
			});
			endCenterStrip();
		}

		ImGui::EndChild();
		if (ImGui::IsItemHovered() && ImGui::GetIO().KeyCtrl) {
			m_thumbnail_size = clamp(m_thumbnail_size + ImGui::GetIO().MouseWheel * 0.1f, 0.3f, 3.f);
		}

		if (ImGui::BeginDragDropTarget()) {
			if (auto* payload = ImGui::AcceptDragDropPayload("entity")) {
				const EntityRef e = *(EntityRef*)payload->Data;
				m_dropped_entity = e;
				ImGui::OpenPopup("Save as prefab");
				World* world = m_app.getWorldEditor().getWorld();
				const ComponentType model_inst_type = reflection::getComponentType("model_instance");
				IModule* module = world->getModule(model_inst_type);
				if (module && world->hasComponent(e, model_inst_type)) {
					Path source;
					if (reflection::getPropertyValue(*module, e, model_inst_type, "Source", source)) {
						copyString(Span(m_prefab_name), Path::getBasename(source));
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
				Path path(m_dir, "/", m_prefab_name, ".fab");
				m_app.getWorldEditor().getPrefabSystem().savePrefab((EntityRef)m_dropped_entity, path);
				m_dropped_entity = INVALID_ENTITY;
			}
			if (ImGui::Selectable(ICON_FA_TIMES "Cancel")) {
				m_dropped_entity = INVALID_ENTITY;
			}
			ImGui::EndPopup();
		}
	}

	void refreshLabels() {
		for (FileInfo& tile : m_file_infos) {
			StaticString<MAX_PATH> filename;
			StringView subres = ResourcePath::getSubresource(tile.filepath);
			if (*subres.end) {
				filename.append(subres, ":", Path::getBasename(tile.filepath));
			} else {
				filename = Path::getBasename(tile.filepath);
			}
			if (m_show_thumbnails) {
				clampText(filename.data, int(TILE_SIZE * m_thumbnail_size));
			}
			else {
				filename.append(".");
				filename.append(Path::getExtension(tile.filepath));
			}

			tile.clamped_filename = filename;
		}
	}

	const char* getName() const override { return "asset_browser"; }

	void checkExtendedMouseButtons() {
		for (const os::Event e : m_app.getEvents()) {
			if (e.type == os::Event::Type::MOUSE_BUTTON && !e.mouse_button.down) {
				switch (e.mouse_button.button) {
					case os::MouseButton::EXTENDED1: goBackDir(); break;
					case os::MouseButton::EXTENDED2: goForwardDir(); break;
					default: break;
				}
			}
		}
	}

	void onGUI() override {
		if (m_create_tile_cooldown > 0) {
			m_create_tile_cooldown -= m_app.getEngine().getLastTimeDelta();
		}
		if (m_dir.isEmpty()) changeDir(".", true);

		bool request_focus_search = false;
		if (m_app.checkShortcut(m_toggle_ui, true)) m_is_open = !m_is_open;
		if (m_app.checkShortcut(m_focus_search, true)) {
			request_focus_search = true;
			m_is_open = true;
		}

		if(!m_is_open) return;

		if (request_focus_search) ImGui::SetNextWindowFocus();
		if (!ImGui::Begin("Assets", &m_is_open)) {
			ImGui::End();
			return;
		}

		checkExtendedMouseButtons();
		CommonActions& common = m_app.getCommonActions();
		if (m_app.checkShortcut(m_back_action)) goBackDir();
		else if (m_app.checkShortcut(m_forward_action)) goForwardDir();
		else if (m_app.checkShortcut(common.del) && !m_selected_resources.empty()) m_request_delete = true;
		else if (m_app.checkShortcut(common.rename) && m_selected_resources.size() == 1) m_request_rename = true;
		else if (m_app.checkShortcut(common.select_all)) selectAll();

		if (request_focus_search) {
			ImGui::SetKeyboardFocusHere();
		}

		if (m_filter.gui("Search", 300, false, &m_focus_search, true)) {
			m_create_tile_cooldown = 0.2f;
			changeDir(m_dir, false);
		}
		if (ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter) && !m_file_infos.empty()) {
			openEditor(m_file_infos[0].filepath);
		}

		ImGui::SameLine();
		breadcrumbs();
		ImGui::Separator();

		if (ImGui::BeginTable("cols", 2, ImGuiTableFlags_Resizable)) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			dirColumn();
			ImGui::TableNextColumn();
			fileColumn();
			ImGui::EndTable();
		}

		ImGui::End();
	}

	void pushDirHistory(const Path& path) {
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

	void removePlugin(IPlugin& plugin) override {
		m_plugins.eraseItem(&plugin);
		m_plugin_map.eraseIf([&plugin](IPlugin* p){ return p == &plugin; });
	}

	IPlugin* getPlugin(StringView extension) const override {
		char ext_str[9];
		makeLowercase(ext_str, extension);
		u64 key = 0;
		ASSERT(extension.size() <= sizeof(key));
		memcpy(&key, ext_str, extension.size());
		auto iter = m_plugin_map.find(key);
		return iter.isValid() ? iter.value() : nullptr;
	}

	Span<IPlugin*> getPlugins() override {
		return m_plugins;
	}

	void addPlugin(IPlugin& plugin, Span<const char*> extensions) override {
		m_plugins.push(&plugin);
		for (const char* ext : extensions) {
			u64 key = 0;
			ASSERT(stringLength(ext) <= sizeof(key));
			memcpy(&key, ext, stringLength(ext));
			m_plugin_map.insert(key, &plugin);
		}
	}

	static void copyDir(const char* src, StringView dest, IAllocator& allocator) {
		StringView basename = Path::getBasename(src);
		StaticString<MAX_PATH> dst_dir(dest, "/", basename);
		if (!os::makePath(dst_dir)) logError("Could not create ", dst_dir);
		os::FileIterator* iter = os::createFileIterator(src, allocator);

		os::FileInfo cfi;
		while(os::getNextFile(iter, &cfi)) {
			if (cfi.is_directory) {
				if (cfi.filename[0] != '.') {
					StaticString<MAX_PATH> tmp_src(src, "/", cfi.filename);
					StaticString<MAX_PATH> tmp_dst(dest, "/", basename);
					copyDir(tmp_src, tmp_dst, allocator);
				}
			}
			else {
				StaticString<MAX_PATH> tmp_src(src, "/", cfi.filename);
				StaticString<MAX_PATH> tmp_dst(dest, "/", basename, "/", cfi.filename);
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
			const Path tmp(fs.getBasePath(), "/", m_dir, "/");
			copyDir(path, tmp, m_allocator);
		}
		PathInfo fi(path);
		const StaticString<MAX_PATH> dest(fs.getBasePath(), "/", m_dir, "/", fi.basename, ".", fi.extension);
		return os::copyFile(path, dest);
	}

	void openEditor(const Path& path) override {
		if (AssetEditorWindow* win = getWindow(path)) {
			win->m_focus_request = true;
			return;
		}

		IPlugin* plugin = getPluginFor(path);
		if (plugin) plugin->openEditor(path);
	}

	void selectRange(const Path& from, const Path& to) {
		m_selected_resources.clear();
		for (u32 i = 0, c = m_file_infos.size(); i < c; ++i) {
			if (m_file_infos[i].filepath == from) {
				for (u32 j = i, cj = m_file_infos.size(); j < cj; ++j) {
					m_selected_resources.push(m_file_infos[j].filepath);
					if (m_file_infos[j].filepath == to) return;
				}
			}
			
			if (m_file_infos[i].filepath == to) {
				for (u32 j = i, cj = m_file_infos.size(); j < cj; ++j) {
					m_selected_resources.push(m_file_infos[j].filepath);
					if (m_file_infos[j].filepath == from) return;
				}
			}
		}
	}

	void selectResource(const Path& path, bool additive) {
		if (additive) {
			i32 idx = m_selected_resources.indexOf(path);
			if (idx >= 0) {
				m_selected_resources.swapAndPop(idx);
			} else {
				m_selected_resources.push(path);
			}
			return;
		}

		m_selected_resources.clear();
		m_selected_resources.push(path);
	}

	static StaticString<MAX_PATH> getImGuiLabelID(const ResourceLocator& rl, bool hash_id) {
		StaticString<MAX_PATH> res;
		if (!rl.full.empty()) {
			res.append(rl.subresource, (rl.subresource.empty() ? "" : ":"), rl.basename, ".", rl.ext);
		}
		if (hash_id) {
			res.append("##h", RuntimeHash(rl.full.begin, rl.full.size()).getHashValue());
		}
		return res;
	}

	bool resourceInput(const char* str_id, Path& path, ResourceType type, float width)  override {
		ImGui::PushID(str_id);
		defer { ImGui::PopID(); };

		const ResourceLocator rl(path);
	
		ImVec2 pos = ImGui::GetCursorScreenPos();
		bool combo_open = false;
		if (path.isEmpty()) {
			ImGui::SetNextItemWidth(width);
			if (ImGui::BeginCombo("##cb", "No resource (click to set)", ImGuiComboFlags_HeightLargest)) {
				combo_open = true;
			}
		}
		else {
			float w = ImGui::CalcTextSize(ICON_FA_BULLSEYE ICON_FA_TRASH).x;
			ImGui::SetNextItemWidth(width < 0 ? -w : width - w);
			if (ImGui::BeginCombo("##cb", getImGuiLabelID(rl, false), ImGuiComboFlags_HeightLargest)) {
				combo_open = true;
			}
		}

		if (combo_open) {
			static FilePathHash selected_path_hash;
			if (resourceList(path, selected_path_hash, type, true, true, ImGui::IsWindowAppearing(), -1)) {
				ImGui::CloseCurrentPopup();
				ImGui::EndCombo();
				return true;
			}
			ImGui::EndCombo();
		}

		ImVec2 button_size = ImGui::GetItemRectSize();
		if (!path.isEmpty() && ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", path.c_str());
		}
	
		if (ImGui::BeginDragDropTarget()) {
			if (auto* payload = ImGui::AcceptDragDropPayload("path")) {
				const char* dropped_path = (const char*)payload->Data;
				StringView subres = ResourcePath::getSubresource(dropped_path);
				StringView ext = Path::getExtension(subres);
				const AssetCompiler& compiler = m_app.getAssetCompiler();
				if (compiler.acceptExtension(ext, type)) {
					path = dropped_path;
					ImGui::EndDragDropTarget();
					return true;
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (!path.isEmpty()) {
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_BULLSEYE, "Go to")) {
				openEditor(path);
			}
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear")) {
				path = Path();
				return true;
			}
		}

		return false;
	}

	void saveResource(Resource& resource, Span<const u8> data) override {
		saveResource(resource.getPath(), data);
	}

	void saveResource(const Path& path, Span<const u8> data) override {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		// use temporary because otherwise the resource is reloaded during saving
		Path tmp_path(path, ".tmp");

		if (!fs.saveContentSync(tmp_path, data)) {
			logError("Could not save file ", path);
			return;
		}

		Engine& engine = m_app.getEngine();
		const char* base_path = engine.getFileSystem().getBasePath();
		StaticString<MAX_PATH> src_full_path(base_path, tmp_path);
		StaticString<MAX_PATH> dest_full_path(base_path, path);

		os::deleteFile(dest_full_path);

		if (!os::moveFile(src_full_path, dest_full_path))
		{
			logError("Could not save file ", path);
		}
	}

	u32 getThumbnailWidth() const override {
		return u32(m_thumbnail_size * TILE_SIZE);
	}

	void tile(const Path& path, bool selected) override {
		i32 idx = m_immediate_tiles.find([&path](const FileInfo& fi){
			return fi.filepath == path;
		});
		if (idx < 0) {
			FileInfo& fi = m_immediate_tiles.emplace();
			fi.filepath = path;

			StaticString<MAX_PATH> filename;
			StringView subres = ResourcePath::getSubresource(path);
			if (*subres.end) {
				filename.append(subres, ":", Path::getBasename(path));
			}
			else {
				filename.append(Path::getBasename(path));
			}
			clampText(filename.data, 50);
			fi.clamped_filename = filename;
			fi.create_called = false;
			StringView ext = Path::getExtension(filename);
			fi.extension = 0;
			ASSERT(ext.size() <= sizeof(fi.extension));
			memcpy(&fi.extension, ext.begin, ext.size());

			idx = m_immediate_tiles.size() - 1;
		}

		m_immediate_tiles[idx].gc_counter = 2;
		thumbnail(m_immediate_tiles[idx], m_thumbnail_size * TILE_SIZE, selected);
	}

	bool resourceList(Path& path, FilePathHash& selected_path_hash, ResourceType type, bool can_create_new, bool enter_submit, bool focus, float width) override {
		static TextFilter filter;
		filter.gui("Filter", width, focus);
		ImGui::Separator();
		
		float h = maximum(200.f, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2);

		ImGui::BeginChild("Resources", ImVec2(0, h), false, ImGuiWindowFlags_HorizontalScrollbar);
		AssetCompiler& compiler = m_app.getAssetCompiler();
	
		const auto& resources = compiler.lockResources();
		Path selected_path;
		for (const auto& res : resources) {
			if(res.type != type) continue;
			if (!filter.pass(res.path)) continue;

			const bool selected = selected_path_hash == res.path.getHash();
			if(selected) selected_path = res.path;
			const ResourceLocator rl(res.path);
			StaticString<MAX_PATH> label = getImGuiLabelID(rl, true);
			const bool is_enter_submit = (enter_submit && ImGui::IsKeyPressed(ImGuiKey_Enter));
			if (is_enter_submit || ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
				selected_path_hash = res.path.getHash();
			
				if (selected || ImGui::IsMouseDoubleClicked(0) || is_enter_submit) {
					path = res.path;
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

	void locate(const Resource& resource) override {
		locate(resource.getPath());
	}

	void locate(const Path& path) override {
		m_is_open = true;
		m_filter.clear();
		StringView new_dir = Path::getDir(ResourcePath::getResource(path));
		if (!Path::isSame(new_dir, m_dir)) {
			changeDir(new_dir.empty() ? StringView(".") : new_dir, true);
		}
		selectResource(path, false);
	}

	void openInExternalEditor(Resource* resource) const override {
		openInExternalEditor(resource->getPath());
	}

	void openInExternalEditor(StringView path) const override {
		const char* base_path = m_app.getEngine().getFileSystem().getBasePath();
		StaticString<MAX_PATH> full_path(base_path, path);
		const os::ExecuteOpenResult res = os::shellExecuteOpen(full_path, {}, {});
		if (res == os::ExecuteOpenResult::NO_ASSOCIATION) {
			logError(full_path, " is not associated with any app.");
		}
		else if (res == os::ExecuteOpenResult::OTHER_ERROR) {
			logError("Failed to open ", full_path, " in exeternal editor.");
		}
	}

	void goBackDir() {
		if (m_dir_history_index < 1) return;
		--m_dir_history_index;
		changeDir(m_dir_history[m_dir_history_index], false);
	}

	void goForwardDir() {
		if (m_dir_history_index >= m_dir_history.size() - 1) return;
		++m_dir_history_index;
		changeDir(m_dir_history[m_dir_history_index], false);
	}

	bool isOpen() const { return m_is_open; }
	void toggleUI() { m_is_open = !m_is_open; }
	
	void addWindow(UniquePtr<AssetEditorWindow>&& window) override {
		if (!m_windows.empty()) window->m_dock_id = m_windows.last()->m_dock_id;
		m_app.addPlugin(*window);
		m_windows.push(window.move());
	}
	
	void closeWindow(AssetEditorWindow& window) override {
		m_app.removePlugin(window);
		m_windows.eraseItems([&](const UniquePtr<AssetEditorWindow>& win){ return win.get() == &window; });
	}

	AssetEditorWindow* getWindow(const Path& path) override {
		i32 idx = m_windows.find([&](const UniquePtr<AssetEditorWindow>& win){ return win->getPath() == path; });
		if (idx < 0) return nullptr;
		return m_windows[idx].get();
	}

	TagAllocator m_allocator;
	bool m_is_open;
	StudioApp& m_app;
	Path m_dir;
	Array<Path> m_subdirs;
	Array<FileInfo> m_file_infos;
	Array<ImmediateTile> m_immediate_tiles;
	Array<UniquePtr<AssetEditorWindow>> m_windows;
	
	Array<Path> m_dir_history;
	i32 m_dir_history_index = -1;

	EntityPtr m_dropped_entity = INVALID_ENTITY;
	char m_prefab_name[MAX_PATH] = "";
	Array<IPlugin*> m_plugins;
	HashMap<u64, IPlugin*> m_plugin_map;
	Array<Path> m_selected_resources;
	TextFilter m_filter;
	float m_create_tile_cooldown = 0.f;
	bool m_show_thumbnails;
	bool m_show_subresources;
	bool m_request_delete = false;
	bool m_request_rename = false;
	float m_thumbnail_size = 1.f;
	Action m_focus_search{"Asset browser", "Focus search", "Focus search", "asset_browser_focus_search", ICON_FA_SEARCH};
	Action m_back_action{"Asset browser", "Back", "Back in history", "asset_browser_back", ICON_FA_ARROW_LEFT};
	Action m_forward_action{"Asset browser", "Forward", "Forward in history", "asset_browser_forward", ICON_FA_ARROW_RIGHT};
	Action m_toggle_ui{"Asset browser", "Asset browser", "Toggle UI", "asset_browser_toggle_ui", "", Action::WINDOW};
	WorldAssetPlugin m_world_asset_plugin;
};

UniquePtr<AssetBrowser> AssetBrowser::create(StudioApp& app) {
	return UniquePtr<AssetBrowserImpl>::create(app.getAllocator(), app);
}

} // namespace Lumix