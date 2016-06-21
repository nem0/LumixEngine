#include "asset_browser.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/fs/disk_file_device.h"
#include "engine/log.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/string.h"
#include "engine/engine.h"
#include "file_system_watcher.h"
#include "metadata.h"
#include "imgui/imgui.h"
#include "platform_interface.h"
#include "utils.h"


static const Lumix::uint32 UNIVERSE_HASH = Lumix::crc32("universe");
static const Lumix::uint32 SOURCE_HASH = Lumix::crc32("source");


Lumix::uint32 AssetBrowser::getResourceType(const char* path) const
{
	char ext[10];
	Lumix::PathUtils::getExtension(ext, sizeof(ext), path);

	for (auto* plugin : m_plugins)
	{
		auto type = plugin->getResourceType(ext);
		if (type != 0) return type;
	}
	if (Lumix::equalStrings(ext, "unv")) return UNIVERSE_HASH;

	return 0;
}


AssetBrowser::AssetBrowser(StudioApp& app)
	: m_editor(*app.getWorldEditor())
	, m_metadata(*app.getMetadata())
	, m_resources(app.getWorldEditor()->getAllocator())
	, m_selected_resource(nullptr)
	, m_autoreload_changed_resource(true)
	, m_changed_files(app.getWorldEditor()->getAllocator())
	, m_is_focus_requested(false)
	, m_changed_files_mutex(false)
	, m_history(app.getWorldEditor()->getAllocator())
	, m_plugins(app.getWorldEditor()->getAllocator())
	, m_on_resource_changed(app.getWorldEditor()->getAllocator())
	, m_app(app)
	, m_is_update_enabled(true)
	, m_current_type(0)
	, m_is_opened(false)
	, m_activate(false)
	, m_history_index(-1)
{
	auto& editor = *app.getWorldEditor();
	auto& allocator = editor.getAllocator();
	m_filter[0] = '\0';
	m_resources.emplace(allocator);

	findResources();

	const char* base_path = editor.getEngine().getDiskFileDevice()->getBasePath();
	m_watchers[0] = FileSystemWatcher::create(base_path, allocator);
	m_watchers[0]->getCallback().bind<AssetBrowser, &AssetBrowser::onFileChanged>(this);
	if (editor.getEngine().getPatchFileDevice())
	{
		base_path = editor.getEngine().getPatchFileDevice()->getBasePath();
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
	Lumix::uint32 resource_type = getResourceType(path);
	if (resource_type == 0) return;

	Lumix::MT::SpinLock lock(m_changed_files_mutex);
	m_changed_files.push(Lumix::Path(path));
}


void AssetBrowser::unloadResource()
{
	if (!m_selected_resource) return;

	for (auto* plugin : m_plugins)
	{
		plugin->onResourceUnloaded(m_selected_resource);
	}
	m_selected_resource->getResourceManager()
		.get(getResourceType(m_selected_resource->getPath().c_str()))
		->unload(*m_selected_resource);

	m_selected_resource = nullptr;
}


int AssetBrowser::getTypeIndexFromManagerType(Lumix::uint32 type) const
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

	auto* patch = m_editor.getEngine().getPatchFileDevice();
	if ((patch && !Lumix::equalStrings(patch->getBasePath(), m_patch_base_path)) ||
		(!patch && m_patch_base_path[0] != '\0'))
	{
		findResources();
	}
	if (!m_is_update_enabled) return;
	bool is_empty;
	{
		Lumix::MT::SpinLock lock(m_changed_files_mutex);
		is_empty = m_changed_files.empty();
	}

	while (!is_empty)
	{
		Lumix::Path path;
		{
			Lumix::MT::SpinLock lock(m_changed_files_mutex);
			
			path = m_changed_files.back();
			m_changed_files.pop();
			is_empty = m_changed_files.empty();
		}

		char ext[10];
		Lumix::PathUtils::getExtension(ext, Lumix::lengthOf(ext), path.c_str());
		m_on_resource_changed.invoke(path, ext);

		Lumix::uint32 resource_type = getResourceType(path.c_str());
		if (resource_type == 0) continue;

		if (m_autoreload_changed_resource) m_editor.getEngine().getResourceManager().reload(path);

		char tmp_path[Lumix::MAX_PATH_LENGTH];
		if (m_editor.getEngine().getPatchFileDevice())
		{
			Lumix::copyString(tmp_path, m_editor.getEngine().getPatchFileDevice()->getBasePath());
			Lumix::catString(tmp_path, path.c_str());
		}

		if (!m_editor.getEngine().getPatchFileDevice() || !PlatformInterface::fileExists(tmp_path))
		{
			Lumix::copyString(tmp_path, m_editor.getEngine().getDiskFileDevice()->getBasePath());
			Lumix::catString(tmp_path, path.c_str());

			if (!PlatformInterface::fileExists(tmp_path))
			{
				int index = getTypeIndexFromManagerType(resource_type);
				m_resources[index].eraseItemFast(path);
				continue;
			}
		}

		char dir[Lumix::MAX_PATH_LENGTH];
		char filename[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getDir(dir, sizeof(dir), path.c_str());
		Lumix::PathUtils::getFilename(filename, sizeof(filename), path.c_str());
		addResource(dir, filename);
	}
	m_changed_files.clear();
}


void AssetBrowser::onToolbar()
{
	auto pos = ImGui::GetCursorScreenPos();
	if (ImGui::BeginToolbar("asset_browser_toolbar", pos, ImVec2(0, 24)))
	{
		if (m_history_index > 0) m_back_action->toolbarButton();
		if (m_history_index < m_history.size() - 1) m_forward_action->toolbarButton();
		m_auto_reload_action->toolbarButton();
		m_refresh_action->toolbarButton();
	}
	ImGui::EndToolbar();
}


void AssetBrowser::onGUI()
{
	if (m_wanted_resource.isValid())
	{
		selectResource(m_wanted_resource, true);
		m_wanted_resource = "";
	}

	if (!ImGui::BeginDock("Asset Browser", &m_is_opened))
	{
		if (m_activate) ImGui::SetDockActive();
		m_activate = false;
		ImGui::EndDock();
		return;
	}

	onToolbar();

	if (ImGui::BeginChild("content"))
	{
		if (m_activate) ImGui::SetDockActive();
		m_activate = false;

		if (m_is_focus_requested)
		{
			m_is_focus_requested = false;
			ImGui::SetWindowFocus();
		}

		auto getter = [](void* data, int idx, const char** out) -> bool
		{
			auto& browser = *static_cast<AssetBrowser*>(data);
			*out = browser.m_plugins[idx]->getName();
			return true;
		};

		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
		ImGui::Combo("Type", &m_current_type, getter, this, m_plugins.size());
		ImGui::FilterInput("Filter", m_filter, sizeof(m_filter));

		static ImVec2 size(0, 200);
		ImGui::ListBoxHeader("Resources", size);
		auto& resources = m_resources[m_current_type + 1];

		for (auto& resource : resources)
		{
			if (m_filter[0] != '\0' && strstr(resource.c_str(), m_filter) == nullptr) continue;

			bool is_selected = m_selected_resource ? m_selected_resource->getPath() == resource : false;
			if (ImGui::Selectable(resource.c_str(), is_selected))
			{
				selectResource(resource, true);
			}
			if (ImGui::IsMouseDragging() && ImGui::IsItemActive())
			{
				m_app.startDrag(StudioApp::DragData::PATH, resource.c_str(), Lumix::stringLength(resource.c_str()) + 1);
			}
		}
		ImGui::ListBoxFooter();
		ImGui::HSplitter("splitter", &size);

		ImGui::PopItemWidth();
		onGUIResource();
	}
	ImGui::EndChild();
	ImGui::EndDock();
}


void AssetBrowser::selectResource(Lumix::Resource* resource, bool record_history)
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


void AssetBrowser::addPlugin(IPlugin& plugin)
{
	m_plugins.push(&plugin);
	m_resources.emplace(m_editor.getAllocator());
	findResources();
}


void AssetBrowser::selectResource(const Lumix::Path& resource, bool record_history)
{
	m_activate = true;
	char ext[30];
	Lumix::PathUtils::getExtension(ext, Lumix::lengthOf(ext), resource.c_str());
	if (Lumix::equalStrings(ext, "unv")) return;

	auto& manager = m_editor.getEngine().getResourceManager();
	auto* resource_manager = manager.get(getResourceType(resource.c_str()));
	if (resource_manager) selectResource(resource_manager->load(resource), record_history);
}


bool AssetBrowser::acceptExtension(const char* ext, Lumix::uint32 type)
{
	for (int i = 0; i < m_plugins.size(); ++i)
	{
		if (m_plugins[i]->acceptExtension(ext, type)) return true;
	}

	return false;
}


bool AssetBrowser::resourceInput(const char* label, const char* str_id, char* buf, int max_size, Lumix::uint32 type)
{
	float item_w = ImGui::CalcItemWidth();
	auto& style = ImGui::GetStyle();
	float text_width = Lumix::Math::maximum(
		50.0f, item_w - ImGui::CalcTextSize("...View").x - style.FramePadding.x * 4 - style.ItemSpacing.x * 2);

	char* c = buf + Lumix::stringLength(buf);
	while (c > buf && *c != '/' && *c != '\\') --c;
	if (*c == '/' || *c == '\\') ++c;
	
	auto pos = ImGui::GetCursorPos();
	pos.x += text_width;
	ImGui::BeginGroup();
	ImGui::AlignFirstTextHeightToWidgets();
	ImGui::PushTextWrapPos(text_width);
	ImGui::Text("%s", c);
	ImGui::PopTextWrapPos();
	ImGui::SameLine();
	ImGui::SetCursorPos(pos);
	Lumix::StaticString<50> popup_name("pu", str_id);
	if (ImGui::Button(Lumix::StaticString<30>("...###browse", str_id)))
	{
		ImGui::OpenPopup(popup_name);
	}
	ImGui::EndGroup();
	if (ImGui::IsItemHoveredRect())
	{
		if (ImGui::IsMouseReleased(0) && m_app.getDragData().type == StudioApp::DragData::PATH)
		{
			char ext[10];
			const char* path = (const char*)m_app.getDragData().data;
			Lumix::PathUtils::getExtension(ext, Lumix::lengthOf(ext), path);
			if (acceptExtension(ext, type))
			{
				Lumix::copyString(buf, max_size, path);
				return true;
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(Lumix::StaticString<30>("View###go", str_id)))
	{
		m_is_focus_requested = true;
		m_is_opened = true;
		m_wanted_resource = buf;
	}
	ImGui::SameLine();
	ImGui::Text("%s", label);

	if (ImGui::BeginResizablePopup(popup_name, ImVec2(300, 300)))
	{
		if (resourceList(buf, max_size, type, 0))
		{
			ImGui::EndPopup();
			return true;
		}
		ImGui::EndPopup();
	}
	return false;
}


bool AssetBrowser::resourceList(char* buf, int max_size, Lumix::uint32 type, float height)
{
	static char filter[128] = "";
	ImGui::FilterInput("Filter", filter, sizeof(filter));

	ImGui::BeginChild("Resources", ImVec2(0, height));
	for (auto& unv : getResources(getTypeIndexFromManagerType(type)))
	{
		if (filter[0] != '\0' && strstr(unv.c_str(), filter) == nullptr) continue;

		if (ImGui::Selectable(unv.c_str(), false))
		{
			Lumix::copyString(buf, max_size, unv.c_str());
			ImGui::EndChild();
			return true;
		}
	}
	ImGui::EndChild();
	return false;
}


void AssetBrowser::openInExternalEditor(Lumix::Resource* resource)
{
	openInExternalEditor(resource->getPath().c_str());
}


void AssetBrowser::openInExternalEditor(const char* path)
{
	if (m_editor.getEngine().getPatchFileDevice())
	{
		Lumix::StaticString<Lumix::MAX_PATH_LENGTH> full_path(m_editor.getEngine().getPatchFileDevice()->getBasePath());
		full_path << path;
		if (PlatformInterface::fileExists(full_path))
		{
			PlatformInterface::shellExecuteOpen(full_path);
			return;
		}
	}

	Lumix::StaticString<Lumix::MAX_PATH_LENGTH> full_path(m_editor.getEngine().getDiskFileDevice()->getBasePath());
	full_path << path;
	PlatformInterface::shellExecuteOpen(full_path);
}


void AssetBrowser::goBack()
{
	if (m_history_index < 1) return;
	m_history_index = Lumix::Math::maximum(0, m_history_index - 1);
	selectResource(m_history[m_history_index], false);
}


void AssetBrowser::goForward()
{
	m_history_index = Lumix::Math::minimum(m_history_index + 1, m_history.size() - 1);
	selectResource(m_history[m_history_index], false);
}


void AssetBrowser::onGUIResource()
{
	if (!m_selected_resource) return;

	const char* path = m_selected_resource->getPath().c_str();
	ImGui::Separator();
	ImGui::LabelText("Selected resource", "%s", path);
	ImGui::Separator();

	if (!m_selected_resource->isReady() && !m_selected_resource->isFailure())
	{
		ImGui::Text("Not ready");
		return;
	}

	char source[Lumix::MAX_PATH_LENGTH];
	if (m_metadata.getString(m_selected_resource->getPath().getHash(), SOURCE_HASH, source, Lumix::lengthOf(source)))
	{
		ImGui::LabelText("Source", "%s", source);
	}

	auto resource_type = getResourceType(path);
	for (auto* plugin : m_plugins)
	{
		if (plugin->onGUI(m_selected_resource, resource_type)) return;
	}
	ASSERT(resource_type == UNIVERSE_HASH); // unimplemented resource
}


const Lumix::Array<Lumix::Path>& AssetBrowser::getResources(int type) const
{
	return m_resources[type];
}


int AssetBrowser::getResourceTypeIndex(const char* ext)
{
	for (int i = 0; i < m_plugins.size(); ++i)
	{
		if (m_plugins[i]->getResourceType(ext) != 0) return 1 + i;
	}


	if (Lumix::equalStrings(ext, "unv")) return 0;
	return -1;
}


void AssetBrowser::addResource(const char* path, const char* filename)
{
	char ext[10];
	Lumix::PathUtils::getExtension(ext, sizeof(ext), filename);

	char fullpath[Lumix::MAX_PATH_LENGTH];
	Lumix::copyString(fullpath, path);
	Lumix::catString(fullpath, "/");
	Lumix::catString(fullpath, filename);
	
	int index = getResourceTypeIndex(ext);

	if (Lumix::startsWith(path, "/render_tests") != 0) return;
	if (Lumix::startsWith(path, "/unit_tests") != 0) return;
	if (index < 0) return;

	Lumix::Path path_obj(fullpath);
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
			char child_path[Lumix::MAX_PATH_LENGTH];
			Lumix::copyString(child_path, dir);
			Lumix::catString(child_path, "/");
			Lumix::catString(child_path, info.filename);
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
	processDir(base_path, Lumix::stringLength(base_path));
	auto* patch_device = m_editor.getEngine().getPatchFileDevice();
	if (patch_device)
	{
		processDir(patch_device->getBasePath(), Lumix::stringLength(patch_device->getBasePath()));
		Lumix::copyString(m_patch_base_path, patch_device->getBasePath());
	}
	else
	{
		m_patch_base_path[0] = '\0';
	}
}
