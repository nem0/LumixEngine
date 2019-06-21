#include "asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/render_interface.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/path_utils.h"
#include "engine/os.h"
#include "engine/profiler.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "imgui/imgui.h"
#include "utils.h"


namespace Lumix
{


static const u32 SOURCE_HASH = crc32("source");


bool AssetBrowser::IPlugin::createTile(const char* in_path, const char* out_path, ResourceType type)
{
	return false;
}


AssetBrowser::AssetBrowser(StudioApp& app)
	: m_editor(app.getWorldEditor())
	, m_selected_resource(nullptr)
	, m_is_focus_requested(false)
	, m_history(app.getWorldEditor().getAllocator())
	, m_plugins(app.getWorldEditor().getAllocator())
	, m_app(app)
	, m_current_type(0)
	, m_is_open(false)
	, m_show_thumbnails(true)
	, m_history_index(-1)
	, m_file_infos(app.getWorldEditor().getAllocator())
	, m_filtered_file_infos(app.getWorldEditor().getAllocator())
	, m_subdirs(app.getWorldEditor().getAllocator())
{
	IAllocator& allocator = m_editor.getAllocator();
	m_filter[0] = '\0';

	const char* base_path = m_editor.getEngine().getFileSystem().getBasePath();

	StaticString<MAX_PATH_LENGTH> path(base_path, ".lumix");
	OS::makePath(path);
	path << "/asset_tiles";
	OS::makePath(path);

	m_back_action = LUMIX_NEW(allocator, Action)("Back", "Back in asset history", "back");
	m_back_action->is_global = false;
	m_back_action->func.bind<AssetBrowser, &AssetBrowser::goBack>(this);
	m_forward_action = LUMIX_NEW(allocator, Action)("Forward", "Forward in asset history", "forward");
	m_forward_action->is_global = false;
	m_forward_action->func.bind<AssetBrowser, &AssetBrowser::goForward>(this);
	m_app.addAction(m_back_action);
	m_app.addAction(m_forward_action);
}


AssetBrowser::~AssetBrowser()
{
	unloadResource();
	RenderInterface* ri = m_app.getWorldEditor().getRenderInterface();
	for (FileInfo& info : m_file_infos)
	{
		ri->unloadTexture(info.tex);
	}
	m_file_infos.clear();

	ASSERT(m_plugins.size() == 0);
}


void AssetBrowser::unloadResource()
{
	if (!m_selected_resource) return;

	for (auto* plugin : m_plugins)
	{
		plugin->onResourceUnloaded(m_selected_resource);
	}
	m_selected_resource->getResourceManager().unload(*m_selected_resource);

	m_selected_resource = nullptr;
}


void AssetBrowser::update()
{
	PROFILE_FUNCTION();

	for (auto* plugin : m_plugins) plugin->update();
}


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


void AssetBrowser::changeDir(const char* path)
{
	RenderInterface* ri = m_app.getWorldEditor().getRenderInterface();
	for (FileInfo& info : m_file_infos)
	{
		ri->unloadTexture(info.tex);
	}
	m_file_infos.clear();

	m_dir = path;
	int len = stringLength(m_dir);
	if (len > 0 && (m_dir[len - 1] == '/' || m_dir[len - 1] == '\\'))
	{
		m_dir.data[len - 1] = '\0';
	}


	IAllocator& allocator = m_app.getWorldEditor().getAllocator();
	FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
	OS::FileIterator* iter = fs.createFileIterator(m_dir);
	OS::FileInfo info;

	const AssetCompiler& compiler = m_app.getAssetCompiler();
	m_subdirs.clear();
	while (OS::getNextFile(iter, &info))
	{
		if (info.is_directory)
		{
			if(info.filename[0] != '.') m_subdirs.emplace(info.filename);
			continue;
		}

		StaticString<MAX_PATH_LENGTH> file_path_str(m_dir, "/", info.filename);
		Path filepath(file_path_str);
		ResourceType type = compiler.getResourceType(filepath.c_str());

		if (type == INVALID_RESOURCE_TYPE) continue;

		FileInfo tile;
		char filename[MAX_PATH_LENGTH];
		PathUtils::getBasename(filename, lengthOf(filename), filepath.c_str());
		clampText(filename, TILE_SIZE);

		tile.file_path_hash = filepath.getHash();
		tile.filepath = filepath.c_str();
		tile.clamped_filename = filename;

		m_file_infos.push(tile);
	}

	doFilter();

	OS::destroyFileIterator(iter);
}


void AssetBrowser::breadcrumbs()
{
	const char* c = m_dir.data;
	char tmp[MAX_PATH_LENGTH];
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
			char new_dir[MAX_PATH_LENGTH];
			copyNString(new_dir, lengthOf(new_dir), m_dir, int(c - m_dir.data));
			changeDir(new_dir);
		}
		ImGui::SameLine(0, 1);
		ImGui::Text("%s", "/");
		ImGui::SameLine(0, 1);
	}
	ImGui::NewLine();
}


void AssetBrowser::dirColumn()
{
	ImVec2 size(m_left_column_width, 0);
	ImGui::BeginChild("left_col", size);
	ImGui::PushItemWidth(120);
	bool b = false;
	if (ImGui::Selectable("..", &b))
	{
		char dir[MAX_PATH_LENGTH];
		PathUtils::getDir(dir, lengthOf(dir), m_dir);
		changeDir(dir);
	}

	for (auto& subdir : m_subdirs)
	{
		if (ImGui::Selectable(subdir, &b))
		{
			StaticString<MAX_PATH_LENGTH> new_dir(m_dir, "/", subdir);
			changeDir(new_dir);
		}
	}

	ImGui::PopItemWidth();
	ImGui::EndChild();
}


void AssetBrowser::doFilter()
{
	m_filtered_file_infos.clear();
	if (!m_filter[0]) return;

	for (int i = 0, c = m_file_infos.size(); i < c; ++i)
	{
		if (stristr(m_file_infos[i].filepath, m_filter)) m_filtered_file_infos.push(i);
	}
}


int AssetBrowser::getThumbnailIndex(int i, int j, int columns) const
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


void AssetBrowser::createTile(FileInfo& tile, const char* out_path)
{
	if (tile.create_called) return;
	tile.create_called = true;
	const AssetCompiler& compiler = m_app.getAssetCompiler();
	for (IPlugin* plugin : m_plugins) {
		ResourceType type = compiler.getResourceType(tile.filepath);
		if (plugin->createTile(tile.filepath, out_path, type)) break;
	}
}


void AssetBrowser::thumbnail(FileInfo& tile)
{
	ImGui::BeginGroup();
	ImVec2 img_size((float)TILE_SIZE, (float)TILE_SIZE);
	RenderInterface* ri = m_app.getWorldEditor().getRenderInterface();
	if (tile.tex)
	{
		if(ri->isValid(tile.tex)) {
			int* th = (int*)tile.tex;
			ImGui::Image((void*)(uintptr_t)*th, img_size);
		}
	}
	else
	{
		ImGui::Rect(img_size.x, img_size.y, 0xffffFFFF);
		StaticString<MAX_PATH_LENGTH> path(".lumix/asset_tiles/", tile.file_path_hash, ".dds");
		FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
		if (fs.fileExists(path))
		{
			if (fs.getLastModified(path) >= fs.getLastModified(tile.filepath))
			{
				tile.tex = ri->loadTexture(Path(path));
			}
			else
			{
				createTile(tile, path);
			}
		}
		else
		{
			createTile(tile, path);
		}
	}
	ImVec2 text_size = ImGui::CalcTextSize(tile.clamped_filename);
	ImVec2 pos = ImGui::GetCursorPos();
	pos.x += (TILE_SIZE - text_size.x) * 0.5f;
	ImGui::SetCursorPos(pos);
	ImGui::Text("%s", tile.clamped_filename.data);
	ImGui::EndGroup();
}


void AssetBrowser::fileColumn()
{
	ImGui::BeginChild("main_col");

	float w = ImGui::GetContentRegionAvailWidth();
	int columns = m_show_thumbnails ? (int)w / TILE_SIZE : 1;
	columns = maximum(columns, 1);
	int tile_count = m_filtered_file_infos.empty() ? m_file_infos.size() : m_filtered_file_infos.size();
	int row_count = m_show_thumbnails ? (tile_count + columns - 1) / columns : tile_count;
	ImGuiListClipper clipper(row_count);
	
	auto callbacks = [this](FileInfo& tile) {
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tile.filepath.data);
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			ImGui::Text("%s", (const char*)tile.filepath);
			ImGui::SetDragDropPayload("path", tile.filepath, stringLength(tile.filepath) + 1, ImGuiCond_Once);
			ImGui::EndDragDropSource();
		}
		else if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0))
		{
			selectResource(Path(tile.filepath), true);
		}
	};

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
					if (idx < 0) break;
					FileInfo& tile = m_file_infos[idx];
					thumbnail(tile);
					callbacks(tile);
				}
			}
			else
			{
				if (!m_filtered_file_infos.empty()) j = m_filtered_file_infos[j];
				FileInfo& tile = m_file_infos[j];
				bool b = m_selected_resource && m_selected_resource->getPath().getHash() == tile.file_path_hash;
				ImGui::Selectable(tile.filepath, b);
				
				callbacks(tile);
			}
		}
	}
	ImGui::EndChild();
}


void AssetBrowser::detailsGUI()
{
	if (!m_is_open) return;
	if (m_is_focus_requested) ImGui::SetNextWindowFocus();
	m_is_focus_requested = false;
	if (ImGui::Begin("Asset properties", &m_is_open))
	{
		ImVec2 pos = ImGui::GetCursorScreenPos();
		if (ImGui::BeginToolbar("asset_browser_toolbar", pos, ImVec2(0, 24)))
		{
			if (m_history_index > 0) m_back_action->toolbarButton();
			if (m_history_index < m_history.size() - 1) m_forward_action->toolbarButton();
		}
		ImGui::EndToolbar();

		if (!m_selected_resource)
		{
			ImGui::End();
			return;
		}

		const char* path = m_selected_resource->getPath().c_str();
		ImGui::Separator();
		ImGui::LabelText("Selected resource", "%s", path);
		ImGui::Separator();

		ImGui::LabelText("Status", "%s", m_selected_resource->isFailure() ? "failure" : (m_selected_resource->isReady() ? "Ready" : "Not ready"));

		const AssetCompiler& compiler = m_app.getAssetCompiler();
		ResourceType resource_type = compiler.getResourceType(path);
		IPlugin* plugin = m_plugins.get(resource_type);
		plugin->onGUI(m_selected_resource);
	}
	ImGui::End();
}


void AssetBrowser::onGUI()
{
	if (m_dir.data[0] == '\0') changeDir(".");
	
	if (m_wanted_resource.isValid())
	{
		selectResource(m_wanted_resource, true);
		m_wanted_resource = "";
	}

	m_is_open = m_is_open || m_is_focus_requested;

	if(m_is_open) {
		if (m_is_focus_requested) ImGui::SetNextWindowFocus();
		if (!ImGui::Begin("Assets", &m_is_open)) {
			ImGui::End();
			detailsGUI();
			return;
		}

		float checkbox_w = ImGui::GetCursorPosX();
		ImGui::Checkbox("Thumbnails", &m_show_thumbnails);
		ImGui::SameLine();
		checkbox_w = ImGui::GetCursorPosX() - checkbox_w;
		if (ImGui::LabellessInputText("Filter", m_filter, sizeof(m_filter), 100)) doFilter();
		ImGui::SameLine(130 + checkbox_w);
		breadcrumbs();
		ImGui::Separator();

		float content_w = ImGui::GetContentRegionAvailWidth();
		ImVec2 left_size(m_left_column_width, 0);
		if (left_size.x < 10) left_size.x = 10;
		if (left_size.x > content_w - 10) left_size.x = content_w - 10;
	
		dirColumn();

		ImGui::SameLine();
		ImGui::VSplitter("vsplit1", &left_size);
		m_left_column_width = left_size.x;
		ImGui::SameLine();

		fileColumn();

		ImGui::End();
	}
	
	detailsGUI();
}


void AssetBrowser::selectResource(Resource* resource, bool record_history)
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
	unloadResource();
	m_selected_resource = resource;
	ASSERT(m_selected_resource->getRefCount() > 0);
}


void AssetBrowser::removePlugin(IPlugin& plugin)
{
	m_plugins.erase(plugin.getResourceType());
}


void AssetBrowser::addPlugin(IPlugin& plugin)
{
	m_plugins.insert(plugin.getResourceType(), &plugin);
}


void AssetBrowser::selectResource(const Path& path, bool record_history)
{
	m_is_focus_requested = true;
	char ext[30];
	PathUtils::getExtension(ext, lengthOf(ext), path.c_str());

	auto& manager = m_editor.getEngine().getResourceManager();
	const AssetCompiler& compiler = m_app.getAssetCompiler();
	const ResourceType type = compiler.getResourceType(path.c_str());
	Resource* res = manager.load(type, path);
	if (res) selectResource(res, record_history);
}


bool AssetBrowser::resourceInput(const char* label, const char* str_id, char* buf, int max_size, ResourceType type)
{
	ImGui::PushID(str_id);
	float item_w = ImGui::CalcItemWidth();
	auto& style = ImGui::GetStyle();
	float text_width = maximum(
		50.0f, item_w - ImGui::CalcTextSize(" ... ").x - style.FramePadding.x * 2);

	
	auto pos = ImGui::GetCursorPos();
	pos.x += text_width;
	ImGui::BeginGroup();
	ImGui::AlignTextToFramePadding();
	ImGui::PushTextWrapPos(pos.x);

	char* c = buf;
	while (*c && c - buf < max_size && *c != ':') ++c;
	if(*c == ':') {
		char tmp[64];
		copyString(tmp, minimum(lengthOf(tmp), int(c - buf) + 1), buf); 
		ImGui::Text("%s", tmp);
	}
	else {
		char* c = buf + stringLength(buf);
		while (c > buf && *c != '/' && *c != '\\') --c;
		if (*c == '/' || *c == '\\') ++c;
		ImGui::Text("%s", c);
	}
	ImGui::PopTextWrapPos();
	ImGui::SameLine();
	ImGui::SetCursorPos(pos);
	if (ImGui::Button(" ... "))
	{
		ImGui::OpenPopup("popup");
	}
	ImGui::EndGroup();
	if (ImGui::BeginDragDropTarget())
	{
		if (auto* payload = ImGui::AcceptDragDropPayload("path"))
		{
			char ext[10];
			const char* path = (const char*)payload->Data;
			PathUtils::getExtension(ext, lengthOf(ext), path);
			const AssetCompiler& compiler = m_app.getAssetCompiler();
			if (compiler.acceptExtension(ext, type))
			{
				copyString(buf, max_size, path);
				ImGui::EndDragDropTarget();
				ImGui::PopID();
				return true;
			}
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::SameLine();
	ImGui::Text("%s", label);

	if (ImGui::BeginResizablePopup("popup", ImVec2(300, 300)))
	{
		if (buf[0] != '\0' && ImGui::Button(StaticString<30>("View###go", str_id)))
		{
			m_is_focus_requested = true;
			m_is_open = true;
			m_wanted_resource = buf;
		}
		if (ImGui::Selectable("Empty", false))
		{
			*buf = '\0';
			ImGui::EndPopup();
			ImGui::PopID();
			return true;
		}
		if (resourceList(buf, max_size, type, 0))
		{
			ImGui::EndPopup();
			ImGui::PopID();
			return true;
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
	return false;
}


IOutputStream* AssetBrowser::beginSaveResource(Resource& resource)
{
	// use temporary because otherwise the resource is reloaded during saving
	StaticString<MAX_PATH_LENGTH> tmp_path(resource.getPath().c_str(), ".tmp");
	OS::OutputFile* f = LUMIX_NEW(m_app.getWorldEditor().getAllocator(), OS::OutputFile);
	FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
	if (!fs.open(tmp_path, f))
	{
		LUMIX_DELETE(m_app.getWorldEditor().getAllocator(), f);
		logError("Editor") << "Could not save file " << resource.getPath().c_str();
		return nullptr;
	}
	return f;
}


void AssetBrowser::endSaveResource(Resource& resource, IOutputStream& file, bool success)
{
	static_cast<OS::OutputFile&>(file).close();
	LUMIX_DELETE(m_app.getWorldEditor().getAllocator(), &file);

	if (!success) return;

	auto& engine = m_app.getWorldEditor().getEngine();
	StaticString<MAX_PATH_LENGTH> src_full_path;
	StaticString<MAX_PATH_LENGTH> dest_full_path;
	StaticString<MAX_PATH_LENGTH> tmp_path(resource.getPath().c_str(), ".tmp");
	src_full_path.data[0] = 0;
	dest_full_path.data[0] = 0;
	src_full_path << engine.getFileSystem().getBasePath() << tmp_path;
	dest_full_path << engine.getFileSystem().getBasePath() << resource.getPath().c_str();

	OS::deleteFile(dest_full_path);

	if (!OS::moveFile(src_full_path, dest_full_path))
	{
		logError("Editor") << "Could not save file " << resource.getPath().c_str();
	}
}


bool AssetBrowser::resourceList(char* buf, int max_size, ResourceType type, float height) const
{
	IPlugin* plugin = m_plugins.get(type);
	if (plugin->canCreateResource() && ImGui::Selectable("New"))
	{
		char path[MAX_PATH_LENGTH];
		if (plugin->createResource(path, lengthOf(path)))
		{
			copyString(buf, max_size, path);
			return true;
		}
	}

	static char filter[128] = "";
	ImGui::LabellessInputText("Filter", filter, sizeof(filter));

	ImGui::BeginChild("Resources", ImVec2(0, height), false, ImGuiWindowFlags_HorizontalScrollbar);
	const AssetCompiler& compiler = m_app.getAssetCompiler();
	for (const auto& res : compiler.getResources(type))
	{
		if (filter[0] != '\0' && strstr(res.c_str(), filter) == nullptr) continue;

		if (ImGui::Selectable(res.c_str(), false))
		{
			copyString(buf, max_size, res.c_str());
			ImGui::EndChild();
			return true;
		}
	}
	ImGui::EndChild();
	return false;
}


void AssetBrowser::openInExternalEditor(Resource* resource) const
{
	openInExternalEditor(resource->getPath().c_str());
}


void AssetBrowser::openInExternalEditor(const char* path) const
{
	StaticString<MAX_PATH_LENGTH> full_path(m_editor.getEngine().getFileSystem().getBasePath());
	full_path << path;
	const OS::ExecuteOpenResult res = OS::shellExecuteOpen(full_path);
	if (res == OS::ExecuteOpenResult::NO_ASSOCIATION) {
		logError("Editor") << full_path << " is not associated with any app.";
	}
	else if (res == OS::ExecuteOpenResult::OTHER_ERROR) {
		logError("Editor") << "Failed to open " << full_path << " in exeternal editor.";
	}
}


void AssetBrowser::goBack()
{
	if (m_history_index < 1) return;
	m_history_index = maximum(0, m_history_index - 1);
	selectResource(m_history[m_history_index], false);
}


void AssetBrowser::goForward()
{
	m_history_index = minimum(m_history_index + 1, m_history.size() - 1);
	selectResource(m_history[m_history_index], false);
}


} // namespace Lumix