#include "asset_browser.h"
#include "editor/render_interface.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/fs/disk_file_device.h"
#include "engine/log.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/string.h"
#include "file_system_watcher.h"
#include "imgui/imgui.h"
#include "metadata.h"
#include "platform_interface.h"
#include "utils.h"


namespace Lumix
{


static const u32 SOURCE_HASH = crc32("source");


bool AssetBrowser::IPlugin::createTile(const char* in_path, const char* out_path, ResourceType type)
{
	return false;
}


ResourceType AssetBrowser::getResourceType(const char* path) const
{
	char ext[10];
	PathUtils::getExtension(ext, sizeof(ext), path);

	for (auto* plugin : m_plugins)
	{
		ResourceType type = plugin->getResourceType(ext);
		if (isValid(type)) return type;
	}

	return INVALID_RESOURCE_TYPE;
}


AssetBrowser::AssetBrowser(StudioApp& app)
	: m_editor(app.getWorldEditor())
	, m_metadata(app.getMetadata())
	, m_resources(app.getWorldEditor().getAllocator())
	, m_selected_resource(nullptr)
	, m_autoreload_changed_resource(true)
	, m_changed_files(app.getWorldEditor().getAllocator())
	, m_is_focus_requested(false)
	, m_changed_files_mutex(false)
	, m_history(app.getWorldEditor().getAllocator())
	, m_plugins(app.getWorldEditor().getAllocator())
	, m_on_resource_changed(app.getWorldEditor().getAllocator())
	, m_app(app)
	, m_is_update_enabled(true)
	, m_current_type(0)
	, m_is_open(false)
	, m_activate(false)
	, m_is_init_finished(false)
	, m_show_thumbnails(true)
	, m_history_index(-1)
	, m_file_infos(app.getWorldEditor().getAllocator())
	, m_filtered_file_infos(app.getWorldEditor().getAllocator())
	, m_subdirs(app.getWorldEditor().getAllocator())
{
	IAllocator& allocator = m_editor.getAllocator();
	m_filter[0] = '\0';
	m_resources.emplace(allocator);

	const char* base_path = m_editor.getEngine().getDiskFileDevice()->getBasePath();

	StaticString<MAX_PATH_LENGTH> path(base_path, ".lumix");
	PlatformInterface::makePath(path);
	path << "/asset_tiles";
	PlatformInterface::makePath(path);

	m_watchers[0] = FileSystemWatcher::create(base_path, allocator);
	m_watchers[0]->getCallback().bind<AssetBrowser, &AssetBrowser::onFileChanged>(this);
	if (m_editor.getEngine().getPatchFileDevice())
	{
		base_path = m_editor.getEngine().getPatchFileDevice()->getBasePath();
		m_watchers[1] = FileSystemWatcher::create(base_path, allocator);
		m_watchers[1]->getCallback().bind<AssetBrowser, &AssetBrowser::onFileChanged>(this);
	}
	else
	{
		m_watchers[1] = nullptr;
	}

	m_auto_reload_action = LUMIX_NEW(allocator, Action)("Auto-reload", "autoReload");
	m_auto_reload_action->is_global = false;
	m_auto_reload_action->func.bind<AssetBrowser, &AssetBrowser::toggleAutoreload>(this);
	m_auto_reload_action->is_selected.bind<AssetBrowser, &AssetBrowser::isAutoreload>(this);
	m_back_action = LUMIX_NEW(allocator, Action)("Back", "back");
	m_back_action->is_global = false;
	m_back_action->func.bind<AssetBrowser, &AssetBrowser::goBack>(this);
	m_forward_action = LUMIX_NEW(allocator, Action)("Forward", "forward");
	m_forward_action->is_global = false;
	m_forward_action->func.bind<AssetBrowser, &AssetBrowser::goForward>(this);
	m_refresh_action = LUMIX_NEW(allocator, Action)("Refresh", "refresh");
	m_refresh_action->is_global = false;
	m_refresh_action->func.bind<AssetBrowser, &AssetBrowser::findResources>(this);
	m_app.addAction(m_auto_reload_action);
	m_app.addAction(m_back_action);
	m_app.addAction(m_forward_action);
	m_app.addAction(m_refresh_action);
}


void AssetBrowser::toggleAutoreload()
{
	m_autoreload_changed_resource = !m_autoreload_changed_resource;
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

	for (auto* plugin : m_plugins)
	{
		LUMIX_DELETE(m_editor.getAllocator(), plugin);
	}
	m_plugins.clear();

	FileSystemWatcher::destroy(m_watchers[0]);
	FileSystemWatcher::destroy(m_watchers[1]);
}


void AssetBrowser::onFileChanged(const char* path)
{
	ResourceType resource_type = getResourceType(path);
	if (!isValid(resource_type)) return;

	MT::SpinLock lock(m_changed_files_mutex);
	m_changed_files.push(Path(path));
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


int AssetBrowser::getTypeIndex(ResourceType type) const
{
	for (int i = 0; i < m_plugins.size(); ++i)
	{
		if (m_plugins[i]->hasResourceManager(type)) return 1 + i;
	}

	return 0;
}


void AssetBrowser::update()
{
	PROFILE_FUNCTION();

	for (auto* plugin : m_plugins) plugin->update();

	auto* patch = m_editor.getEngine().getPatchFileDevice();
	if ((patch && !equalStrings(patch->getBasePath(), m_patch_base_path)) ||
		(!patch && m_patch_base_path[0] != '\0'))
	{
		findResources();
	}
	if (!m_is_update_enabled) return;
	bool is_empty;
	{
		MT::SpinLock lock(m_changed_files_mutex);
		is_empty = m_changed_files.empty();
	}

	while (!is_empty)
	{
		Path path;
		{
			MT::SpinLock lock(m_changed_files_mutex);
			
			path = m_changed_files.back();
			m_changed_files.pop();
			is_empty = m_changed_files.empty();
		}

		char ext[10];
		PathUtils::getExtension(ext, lengthOf(ext), path.c_str());
		m_on_resource_changed.invoke(path, ext);

		ResourceType resource_type = getResourceType(path.c_str());
		if (!isValid(resource_type)) continue;

		if (m_autoreload_changed_resource) m_editor.getEngine().getResourceManager().reload(path);

		char tmp_path[MAX_PATH_LENGTH];
		if (m_editor.getEngine().getPatchFileDevice())
		{
			copyString(tmp_path, m_editor.getEngine().getPatchFileDevice()->getBasePath());
			catString(tmp_path, path.c_str());
		}

		if (!m_editor.getEngine().getPatchFileDevice() || !PlatformInterface::fileExists(tmp_path))
		{
			copyString(tmp_path, m_editor.getEngine().getDiskFileDevice()->getBasePath());
			catString(tmp_path, path.c_str());

			if (!PlatformInterface::fileExists(tmp_path))
			{
				int index = getTypeIndex(resource_type);
				m_resources[index].eraseItemFast(path);
				continue;
			}
		}

		char dir[MAX_PATH_LENGTH];
		char filename[MAX_PATH_LENGTH];
		PathUtils::getDir(dir, sizeof(dir), path.c_str());
		PathUtils::getFilename(filename, sizeof(filename), path.c_str());
		addResource(dir, filename);
	}
	m_changed_files.clear();
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
	PlatformInterface::FileIterator* iter = PlatformInterface::createFileIterator(m_dir, allocator);
	PlatformInterface::FileInfo info;

	m_subdirs.clear();
	while (PlatformInterface::getNextFile(iter, &info))
	{
		if (info.is_directory)
		{
			if(info.filename[0] != '.') m_subdirs.emplace(info.filename);
			continue;
		}

		StaticString<MAX_PATH_LENGTH> file_path_str(m_dir, "/", info.filename);
		Path filepath(file_path_str);
		ResourceType type = getResourceType(filepath.c_str());

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

	PlatformInterface::destroyFileIterator(iter);
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
	for (IPlugin* plugin : m_plugins)
	{
		ResourceType type = getResourceType(tile.filepath);
		if (plugin->createTile(tile.filepath, out_path, type)) break;
	}
}


void AssetBrowser::thumbnail(FileInfo& tile)
{
	ImGui::BeginGroup();
	ImVec2 img_size((float)TILE_SIZE, (float)TILE_SIZE);
	if (tile.tex)
	{
		ImGui::Image(tile.tex, img_size);
	}
	else
	{
		ImGui::Rect(img_size.x, img_size.y, 0xffffFFFF);
		StaticString<MAX_PATH_LENGTH> path(".lumix/asset_tiles/", tile.file_path_hash, ".dds");
		if (PlatformInterface::fileExists(path))
		{
			RenderInterface* ri = m_app.getWorldEditor().getRenderInterface();
			if (PlatformInterface::getLastModified(path) >= PlatformInterface::getLastModified(tile.filepath))
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

	IAllocator& allocator = m_app.getWorldEditor().getAllocator();

	float w = ImGui::GetContentRegionAvailWidth();
	int columns = m_show_thumbnails ? (int)w / TILE_SIZE : 1;
	columns = Math::maximum(columns, 1);
	int tile_count = m_filtered_file_infos.empty() ? m_file_infos.size() : m_filtered_file_infos.size();
	int row_count = m_show_thumbnails ? (tile_count + columns - 1) / columns : tile_count;
	ImGuiListClipper clipper(row_count);
	
	auto callbacks = [this](FileInfo& tile) {
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tile.filepath.data);
		if (ImGui::IsMouseDragging() && ImGui::IsItemRectHovered())
		{
			if (m_app.getDragData().type == StudioApp::DragData::NONE)
			{
				m_app.startDrag(StudioApp::DragData::PATH, tile.filepath, stringLength(tile.filepath) + 1);
			}
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
	return;
}


void AssetBrowser::detailsGUI()
{
	if (ImGui::BeginDock("Asset properties", &m_is_open))
	{
		ImVec2 pos = ImGui::GetCursorScreenPos();
		if (ImGui::BeginToolbar("asset_browser_toolbar", pos, ImVec2(0, 24)))
		{
			if (m_history_index > 0) m_back_action->toolbarButton();
			if (m_history_index < m_history.size() - 1) m_forward_action->toolbarButton();
			m_auto_reload_action->toolbarButton();
			m_refresh_action->toolbarButton();
		}
		ImGui::EndToolbar();

		if (!m_selected_resource) goto end;

		const char* path = m_selected_resource->getPath().c_str();
		ImGui::Separator();
		ImGui::LabelText("Selected resource", "%s", path);
		ImGui::Separator();

		if (!m_selected_resource->isReady() && !m_selected_resource->isFailure())
		{
			ImGui::Text("Not ready");
			goto end;
		}

		char source[MAX_PATH_LENGTH];
		if (m_metadata.getString(m_selected_resource->getPath().getHash(), SOURCE_HASH, source, lengthOf(source)))
		{
			ImGui::LabelText("Source", "%s", source);
		}

		auto resource_type = getResourceType(path);
		for (auto* plugin : m_plugins)
		{
			if (plugin->onGUI(m_selected_resource, resource_type)) goto end;
		}
		ASSERT(false); // unimplemented resource
	}

	end:
		ImGui::EndDock();
}


void AssetBrowser::onGUI()
{
	if (m_dir.data[0] == '\0') changeDir(".");
	
	if (m_wanted_resource.isValid())
	{
		selectResource(m_wanted_resource, true);
		m_wanted_resource = "";
	}

	m_is_open = m_is_open || m_activate;

	if (!ImGui::BeginDock("Assets", &m_is_open))
	{
		if (m_activate) ImGui::SetDockActive();
		m_activate = false;
		ImGui::EndDock();
		detailsGUI();
		return;
	}
	if (m_is_focus_requested)
	{
		m_is_focus_requested = false;
		ImGui::SetWindowFocus();
	}
	if (m_activate) ImGui::SetDockActive();
	m_activate = false;

	float checkbox_w = ImGui::GetCursorPosX();
	ImGui::Checkbox("Thumbnails", &m_show_thumbnails);
	ImGui::SameLine();
	checkbox_w = ImGui::GetCursorPosX() - checkbox_w;
	if (ImGui::LabellessInputText("Filter", m_filter, sizeof(m_filter), 100)) doFilter();
	ImGui::SameLine(130 + checkbox_w);
	breadcrumbs();
	ImGui::Separator();

	float content_w = ImGui::GetContentRegionAvailWidth();
	ImVec2 main_size(content_w - m_left_column_width, 0);
	ImVec2 left_size(m_left_column_width, 0);
	if (left_size.x < 10) left_size.x = 10;
	if (left_size.x > content_w - 10) left_size.x = content_w - 10;
	if (main_size.x < 10) main_size.x = 10;
	if (content_w - left_size.x - main_size.x < 60) main_size.x = content_w - 60 - left_size.x;
	
	dirColumn();

	ImGui::SameLine();
	ImGui::VSplitter("vsplit1", &left_size);
	m_left_column_width = left_size.x;
	ImGui::SameLine();

	fileColumn();

	ImGui::EndDock();
	
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


void AssetBrowser::onInitFinished()
{
	m_is_init_finished = true;
	findResources();
}


void AssetBrowser::addPlugin(IPlugin& plugin)
{
	m_plugins.push(&plugin);
	m_resources.emplace(m_editor.getAllocator());
	if(m_is_init_finished) findResources();
}


void AssetBrowser::selectResource(const Path& resource, bool record_history)
{
	m_activate = true;
	char ext[30];
	PathUtils::getExtension(ext, lengthOf(ext), resource.c_str());

	auto& manager = m_editor.getEngine().getResourceManager();
	auto* resource_manager = manager.get(getResourceType(resource.c_str()));
	if (resource_manager) selectResource(resource_manager->load(resource), record_history);
}


bool AssetBrowser::acceptExtension(const char* ext, ResourceType type)
{
	for (int i = 0; i < m_plugins.size(); ++i)
	{
		if (m_plugins[i]->acceptExtension(ext, type)) return true;
	}

	return false;
}


bool AssetBrowser::resourceInput(const char* label, const char* str_id, char* buf, int max_size, ResourceType type)
{
	ImGui::PushID(str_id);
	float item_w = ImGui::CalcItemWidth();
	auto& style = ImGui::GetStyle();
	float text_width = Math::maximum(
		50.0f, item_w - ImGui::CalcTextSize("...").x - style.FramePadding.x * 2);

	char* c = buf + stringLength(buf);
	while (c > buf && *c != '/' && *c != '\\') --c;
	if (*c == '/' || *c == '\\') ++c;
	
	auto pos = ImGui::GetCursorPos();
	pos.x += text_width;
	ImGui::BeginGroup();
	ImGui::AlignTextToFramePadding();
	ImGui::PushTextWrapPos(pos.x);
	ImGui::Text("%s", c);
	ImGui::PopTextWrapPos();
	ImGui::SameLine();
	ImGui::SetCursorPos(pos);
	if (ImGui::Button("..."))
	{
		ImGui::OpenPopup("popup");
	}
	ImGui::EndGroup();
	if (ImGui::IsItemRectHovered())
	{
		if (ImGui::IsMouseReleased(0) && m_app.getDragData().type == StudioApp::DragData::PATH)
		{
			char ext[10];
			const char* path = (const char*)m_app.getDragData().data;
			PathUtils::getExtension(ext, lengthOf(ext), path);
			if (acceptExtension(ext, type))
			{
				copyString(buf, max_size, path);
				ImGui::PopID();
				return true;
			}
		}
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


bool AssetBrowser::resourceList(char* buf, int max_size, ResourceType type, float height)
{
	static char filter[128] = "";
	ImGui::LabellessInputText("Filter", filter, sizeof(filter));

	ImGui::BeginChild("Resources", ImVec2(0, height));
	for (auto& res : getResources(getTypeIndex(type)))
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


void AssetBrowser::openInExternalEditor(Resource* resource)
{
	openInExternalEditor(resource->getPath().c_str());
}


void AssetBrowser::openInExternalEditor(const char* path)
{
	if (m_editor.getEngine().getPatchFileDevice())
	{
		StaticString<MAX_PATH_LENGTH> full_path(m_editor.getEngine().getPatchFileDevice()->getBasePath());
		full_path << path;
		if (PlatformInterface::fileExists(full_path))
		{
			PlatformInterface::shellExecuteOpen(full_path, nullptr);
			return;
		}
	}

	StaticString<MAX_PATH_LENGTH> full_path(m_editor.getEngine().getDiskFileDevice()->getBasePath());
	full_path << path;
	PlatformInterface::shellExecuteOpen(full_path, nullptr);
}


void AssetBrowser::goBack()
{
	if (m_history_index < 1) return;
	m_history_index = Math::maximum(0, m_history_index - 1);
	selectResource(m_history[m_history_index], false);
}


void AssetBrowser::goForward()
{
	m_history_index = Math::minimum(m_history_index + 1, m_history.size() - 1);
	selectResource(m_history[m_history_index], false);
}


const Array<Path>& AssetBrowser::getResources(int type) const
{
	return m_resources[type];
}


int AssetBrowser::getResourceTypeIndex(const char* ext)
{
	for (int i = 0; i < m_plugins.size(); ++i)
	{
		if (isValid(m_plugins[i]->getResourceType(ext))) return 1 + i;
	}

	return -1;
}


void AssetBrowser::addResource(const char* path, const char* filename)
{
	char ext[10];
	PathUtils::getExtension(ext, sizeof(ext), filename);
	makeLowercase(ext, lengthOf(ext), ext);

	char fullpath[MAX_PATH_LENGTH];
	copyString(fullpath, path);
	catString(fullpath, "/");
	catString(fullpath, filename);
	
	int index = getResourceTypeIndex(ext);

	if (startsWith(path, "/unit_tests") != 0) return;
	if (index < 0) return;

	Path path_obj(fullpath);
	if (m_resources[index].indexOf(path_obj) == -1)
	{
		m_resources[index].push(path_obj);
	}
}


void AssetBrowser::processDir(const char* dir, int base_length)
{
	auto* iter = PlatformInterface::createFileIterator(dir, m_editor.getAllocator());
	PlatformInterface::FileInfo info;
	while (getNextFile(iter, &info))
	{
		if (info.filename[0] == '.') continue;

		if (info.is_directory)
		{
			char child_path[MAX_PATH_LENGTH];
			copyString(child_path, dir);
			catString(child_path, "/");
			catString(child_path, info.filename);
			processDir(child_path, base_length);
		}
		else
		{
			addResource(dir + base_length, info.filename);
		}
	}

	destroyFileIterator(iter);
}


void AssetBrowser::findResources()
{
	for (auto& resources : m_resources)
	{
		resources.clear();
	}

	const char* base_path = m_editor.getEngine().getDiskFileDevice()->getBasePath();
	processDir(base_path, stringLength(base_path));
	auto* patch_device = m_editor.getEngine().getPatchFileDevice();
	if (patch_device)
	{
		processDir(patch_device->getBasePath(), stringLength(patch_device->getBasePath()));
		copyString(m_patch_base_path, patch_device->getBasePath());
	}
	else
	{
		m_patch_base_path[0] = '\0';
	}
}


} // namespace Lumix