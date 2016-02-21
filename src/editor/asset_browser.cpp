#include "asset_browser.h"
#include "core/crc32.h"
#include "core/fs/disk_file_device.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/profiler.h"
#include "core/resource.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/world_editor.h"
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
	if (Lumix::compareString(ext, "unv") == 0) return UNIVERSE_HASH;

	return 0;
}


AssetBrowser::AssetBrowser(Lumix::WorldEditor& editor, Metadata& metadata)
	: m_editor(editor)
	, m_metadata(metadata)
	, m_resources(editor.getAllocator())
	, m_selected_resource(nullptr)
	, m_autoreload_changed_resource(true)
	, m_changed_files(editor.getAllocator())
	, m_is_focus_requested(false)
	, m_changed_files_mutex(false)
	, m_history(editor.getAllocator())
	, m_plugins(editor.getAllocator())
{
	m_is_update_enabled = true;
	m_filter[0] = '\0';
	m_current_type = 0;
	m_is_opened = false;
	m_activate = false;
	m_resources.emplace(editor.getAllocator());

	findResources();

	const char* base_path = editor.getEngine().getDiskFileDevice()->getBasePath(0);
	m_watchers[0] = FileSystemWatcher::create(base_path, editor.getAllocator());
	m_watchers[0]->getCallback().bind<AssetBrowser, &AssetBrowser::onFileChanged>(this);
	base_path = editor.getEngine().getDiskFileDevice()->getBasePath(1);
	if (Lumix::stringLength(base_path) > 1)
	{
		m_watchers[1] = FileSystemWatcher::create(base_path, editor.getAllocator());
		m_watchers[1]->getCallback().bind<AssetBrowser, &AssetBrowser::onFileChanged>(this);
	}
	else
	{
		m_watchers[1] = nullptr;
	}
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

		Lumix::uint32 resource_type = getResourceType(path.c_str());
		if (resource_type == 0) continue;

		if (m_autoreload_changed_resource) m_editor.getEngine().getResourceManager().reload(path);

		if (!PlatformInterface::fileExists(path.c_str()))
		{
			int index = getTypeIndexFromManagerType(resource_type);
			m_resources[index].eraseItemFast(path);
			continue;
		}

		char dir[Lumix::MAX_PATH_LENGTH];
		char filename[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getDir(dir, sizeof(dir), path.c_str());
		Lumix::PathUtils::getFilename(filename, sizeof(filename), path.c_str());
		addResource(dir, filename);
	}
	m_changed_files.clear();
}


void AssetBrowser::onGUI()
{
	if (m_wanted_resource.isValid())
	{
		selectResource(m_wanted_resource);
		m_wanted_resource = "";
	}

	if (!ImGui::BeginDock("Asset Browser", &m_is_opened))
	{
		if (m_activate) ImGui::SetDockActive();
		m_activate = false;
		ImGui::EndDock();
		return;
	}

	if (m_activate) ImGui::SetDockActive();
	m_activate = false;

	if (m_is_focus_requested)
	{
		m_is_focus_requested = false;
		ImGui::SetWindowFocus();
	}

	if (ImGui::Button("Refresh")) findResources();
	ImGui::SameLine();
	ImGui::Checkbox("Autoreload", &m_autoreload_changed_resource);

	auto getter = [](void* data, int idx, const char** out) -> bool
	{
		auto& browser = *static_cast<AssetBrowser*>(data);
		*out = idx == 0 ? "Universe" : browser.m_plugins[idx - 1]->getName();
		return true;
	};

	ImGui::Combo("Type", &m_current_type, getter, this, 1 + m_plugins.size());
	ImGui::InputText("Filter", m_filter, sizeof(m_filter));

	ImGui::ListBoxHeader("Resources");
	auto& resources = m_resources[m_current_type];

	for (auto& resource : resources)
	{
		if (m_filter[0] != '\0' && strstr(resource.c_str(), m_filter) == nullptr) continue;

		bool is_selected = m_selected_resource ? m_selected_resource->getPath() == resource : false;
		if (ImGui::Selectable(resource.c_str(), is_selected))
		{
			selectResource(resource);
		}
	}
	ImGui::ListBoxFooter();
	onGUIResource();
	ImGui::EndDock();
}


void AssetBrowser::selectResource(Lumix::Resource* resource)
{
	if (m_selected_resource) m_history.push(m_selected_resource->getPath());
	if (m_history.size() > 20) m_history.erase(0);


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


void AssetBrowser::selectResource(const Lumix::Path& resource)
{
	m_activate = true;
	char ext[30];
	Lumix::PathUtils::getExtension(ext, Lumix::lengthOf(ext), resource.c_str());
	if (Lumix::compareString(ext, "unv") == 0) return;

	auto& manager = m_editor.getEngine().getResourceManager();
	auto* resource_manager = manager.get(getResourceType(resource.c_str()));
	if (resource_manager) selectResource(resource_manager->load(resource));
}


bool AssetBrowser::resourceInput(const char* label, const char* str_id, char* buf, int max_size, Lumix::uint32 type)
{
	float item_w = ImGui::CalcItemWidth();
	auto& style = ImGui::GetStyle();
	ImGui::PushItemWidth(item_w - ImGui::CalcTextSize("...View").x - style.FramePadding.x * 4 -
						 style.ItemSpacing.x * 2);

	if (ImGui::InputText(StringBuilder<30>("###", str_id), buf, max_size)) return true;

	ImGui::SameLine();
	StringBuilder<50> popup_name("pu", str_id);
	if (ImGui::Button(StringBuilder<30>("...###browse", str_id)))
	{
		ImGui::OpenPopup(popup_name);
	}
	ImGui::SameLine();
	if (ImGui::Button(StringBuilder<30>("View###go", str_id)))
	{
		m_is_focus_requested = true;
		m_is_opened = true;
		m_wanted_resource = buf;
	}
	ImGui::SameLine();
	ImGui::Text(label);
	ImGui::PopItemWidth();

	if (ImGui::BeginPopup(popup_name))
	{
		static char filter[128] = "";
		ImGui::InputText("Filter", filter, sizeof(filter));

		for (auto unv : getResources(getTypeIndexFromManagerType(type)))
		{
			if (filter[0] != '\0' && strstr(unv.c_str(), filter) == nullptr) continue;

			if (ImGui::Selectable(unv.c_str(), false))
			{
				Lumix::copyString(buf, max_size, unv.c_str());
				ImGui::EndPopup();
				return true;
			}
		}

		ImGui::EndPopup();
	}

	return false;
}


void AssetBrowser::openInExternalEditor(Lumix::Resource* resource)
{
	openInExternalEditor(resource->getPath().c_str());
}


void AssetBrowser::openInExternalEditor(const char* path)
{
	StringBuilder<Lumix::MAX_PATH_LENGTH> full_path(m_editor.getEngine().getDiskFileDevice()->getBasePath(0));
	full_path << path;
	if (!PlatformInterface::fileExists(path))
	{
		full_path.data[0] = 0;
		full_path << m_editor.getEngine().getDiskFileDevice()->getBasePath(0);
		full_path << path;
	}
	PlatformInterface::shellExecuteOpen(full_path);
}


void AssetBrowser::onGUIResource()
{
	if (!m_selected_resource) return;

	const char* path = m_selected_resource->getPath().c_str();
	ImGui::Separator();
	ImGui::LabelText("Selected resource", path);
	if (!m_history.empty() && ImGui::Button("Back"))
	{
		selectResource(m_history.back());
		m_history.pop();
		m_history.pop();
		return;
	}
	ImGui::Separator();

	if (!m_selected_resource->isReady() && !m_selected_resource->isFailure())
	{
		ImGui::Text("Not ready");
		return;
	}

	char source[Lumix::MAX_PATH_LENGTH];
	if (m_metadata.getString(
			m_selected_resource->getPath().getHash(), SOURCE_HASH, source, Lumix::lengthOf(source)))
	{
		ImGui::LabelText("Source", source);
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


	if (Lumix::compareString(ext, "unv") == 0) return 0;
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

	const char* base_path = m_editor.getEngine().getDiskFileDevice()->getBasePath(0);
	processDir(base_path, Lumix::stringLength(base_path));
	base_path = m_editor.getEngine().getDiskFileDevice()->getBasePath(1);
	if (base_path[0] != 0) processDir(base_path, Lumix::stringLength(base_path));
}