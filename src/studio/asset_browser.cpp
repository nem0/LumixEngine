#include "asset_browser.h"
#include "core/crc32.h"
#include "core/FS/file_iterator.h"
#include "core/FS/file_system.h"
#include "core/FS/ifile.h"
#include "core/json_serializer.h"
#include "core/path_utils.h"
#include "core/profiler.h"
#include "core/resource.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/string.h"
#include "core/system.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "file_system_watcher.h"
#include "lua_script/lua_script_manager.h"
#include "ocornut-imgui/imgui.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "utils.h"


static const uint32_t UNIVERSE_HASH = Lumix::crc32("universe");
static const uint32_t LUA_SCRIPT_HASH = Lumix::crc32("lua_script");


static uint32_t getResourceType(const char* path)
{
	char ext[10];
	Lumix::PathUtils::getExtension(ext, sizeof(ext), path);

	if (strcmp(ext, "mat") == 0) return Lumix::ResourceManager::MATERIAL;
	if (strcmp(ext, "msh") == 0) return Lumix::ResourceManager::MODEL;
	if (strcmp(ext, "dds") == 0) return Lumix::ResourceManager::TEXTURE;
	if (strcmp(ext, "tga") == 0) return Lumix::ResourceManager::TEXTURE;
	if (strcmp(ext, "shd") == 0) return Lumix::ResourceManager::SHADER;
	if (strcmp(ext, "unv") == 0) return UNIVERSE_HASH;
	if (strcmp(ext, "lua") == 0) return LUA_SCRIPT_HASH;

	return 0;
}


AssetBrowser::AssetBrowser(Lumix::WorldEditor& editor)
	: m_editor(editor)
	, m_resources(editor.getAllocator())
	, m_selected_resource(nullptr)
	, m_autoreload_changed_resource(true)
	, m_changed_files(editor.getAllocator())
{
	m_filter[0] = '\0';
	m_current_type = 0;
	m_text_buffer[0] = '\0';
	m_is_opened = false;
	for (int i = 0; i < Count; ++i)
	{
		m_resources.emplace(editor.getAllocator());
	}

	findResources();

	m_watcher = FileSystemWatcher::create(editor.getBasePath(), editor.getAllocator());
	m_watcher->getCallback().bind<AssetBrowser, &AssetBrowser::onFileChanged>(this);
}


AssetBrowser::~AssetBrowser()
{
	unloadResource();
	FileSystemWatcher::destroy(m_watcher);
}


void AssetBrowser::onFileChanged(const char* path)
{
	uint32_t resource_type = getResourceType(path);
	if (resource_type == 0) return;

	m_changed_files.push(Lumix::Path(path));
}


void AssetBrowser::unloadResource()
{
	if (!m_selected_resource) return;

	m_selected_resource->getResourceManager()
		.get(getResourceType(m_selected_resource->getPath().c_str()))
		->unload(*m_selected_resource);

	m_selected_resource = nullptr;
}


AssetBrowser::Type AssetBrowser::getTypeFromResourceManagerType(uint32_t type) const
{
	switch (type)
	{
		case Lumix::ResourceManager::MODEL: return MODEL;
		case Lumix::ResourceManager::SHADER: return SHADER;
		case Lumix::ResourceManager::TEXTURE: return TEXTURE;
		case Lumix::ResourceManager::MATERIAL: return MATERIAL;
	}
	if (type == UNIVERSE_HASH) return UNIVERSE;
	if (type == LUA_SCRIPT_HASH) return LUA_SCRIPT;

	return MODEL;
}


void AssetBrowser::update()
{
	PROFILE_FUNCTION();
	for (const auto& path_obj : m_changed_files)
	{
		const char* path = path_obj.c_str();
		
		uint32_t resource_type = getResourceType(path);
		if (resource_type == 0) continue;

		if (m_autoreload_changed_resource) m_editor.getEngine().getResourceManager().reload(path);

		Lumix::Path path_obj(path);
		if (!Lumix::fileExists(path))
		{
			int index = getTypeFromResourceManagerType(resource_type);
			m_resources[index].eraseItemFast(path_obj);
			continue;
		}

		char dir[Lumix::MAX_PATH_LENGTH];
		char filename[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getDir(dir, sizeof(dir), path);
		Lumix::PathUtils::getFilename(filename, sizeof(filename), path);
		addResource(dir, filename);
	}
	m_changed_files.clear();
}


void AssetBrowser::onGui()
{
	if (!m_is_opened) return;

	if (!ImGui::Begin("AssetBrowser", &m_is_opened))
	{
		ImGui::End();
		return;
	}

	if (ImGui::Button("Refresh")) findResources();
	ImGui::SameLine();
	ImGui::Checkbox("Autoreload", &m_autoreload_changed_resource);

	const char* items = "Material\0Model\0Shader\0Texture\0Universe\0Lua Script\0";
	ImGui::Combo("Type", &m_current_type, items);
	ImGui::InputText("Filter", m_filter, sizeof(m_filter));

	ImGui::ListBoxHeader("Resources");
	auto* resources = &m_resources[m_current_type];

	for (auto& resource : *resources)
	{
		if (m_filter[0] != '\0' && strstr(resource.c_str(), m_filter) == nullptr) continue;

		bool is_selected = m_selected_resource ? m_selected_resource->getPath() == resource : false;
		if (ImGui::Selectable(resource.c_str(), is_selected))
		{
			selectResource(resource);
		}
	}
	ImGui::ListBoxFooter();
	onGuiResource();
	ImGui::End();
}


void AssetBrowser::selectResource(Lumix::Resource* resource)
{
	m_text_buffer[0] = '\0';
	unloadResource();
	m_selected_resource = resource;
	ASSERT(m_selected_resource->getRefCount() > 0);
}


void AssetBrowser::selectResource(const Lumix::Path& resource)
{
	selectResource(m_editor.getEngine()
					   .getResourceManager()
					   .get(getResourceType(resource.c_str()))
					   ->load(resource));
}


void AssetBrowser::saveMaterial(Lumix::Material* material)
{
	Lumix::FS::FileSystem& fs = m_editor.getEngine().getFileSystem();
	// use temporary because otherwise the material is reloaded during saving
	char tmp_path[Lumix::MAX_PATH_LENGTH];
	strcpy(tmp_path, material->getPath().c_str());
	strcat(tmp_path, ".tmp");
	Lumix::FS::IFile* file =
		fs.open(fs.getDefaultDevice(), tmp_path, Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);
	if (file)
	{
		Lumix::DefaultAllocator allocator;
		Lumix::JsonSerializer serializer(*file,
			Lumix::JsonSerializer::AccessMode::WRITE,
			material->getPath().c_str(),
			allocator);
		if (!material->save(serializer))
		{
			Lumix::g_log_error.log("Material manager") << "Error saving "
													   << material->getPath().c_str();
		}
		fs.close(*file);

		Lumix::deleteFile(material->getPath().c_str());
		Lumix::moveFile(tmp_path, material->getPath().c_str());
	}
	else
	{
		Lumix::g_log_error.log("Material manager") << "Could not save file "
												   << material->getPath().c_str();
	}
}


bool AssetBrowser::resourceInput(const char* label, char* buf, int max_size, Type type)
{
	if (ImGui::InputText(label, buf, max_size)) return true;

	ImGui::SameLine();
	if (ImGui::Button(StringBuilder<30>("...##", label)))
	{
		ImGui::OpenPopup(label);
	}

	if (ImGui::BeginPopup(label))
	{
		static char filter[128] = "";
		ImGui::InputText("Filter", filter, sizeof(filter));

		for (auto unv : getResources(type))
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


void AssetBrowser::onGuiMaterial()
{
	auto* material = static_cast<Lumix::Material*>(m_selected_resource);

	if (ImGui::Button("Save")) saveMaterial(material);

	bool b;
	if (material->hasAlphaCutoutDefine())
	{
		b = material->isAlphaCutout();
		if (ImGui::Checkbox("Is alpha cutout", &b)) material->enableAlphaCutout(b);
	}

	b = material->isBackfaceCulling();
	if (ImGui::Checkbox("Is backface culling", &b)) material->enableBackfaceCulling(b);

	if (material->hasShadowReceivingDefine())
	{
		b = material->isShadowReceiver();
		if (ImGui::Checkbox("Is shadow receiver", &b)) material->enableShadowReceiving(b);
	}

	b = material->isZTest();
	if (ImGui::Checkbox("Z test", &b)) material->enableZTest(b);

	Lumix::Vec3 specular = material->getSpecular();
	if (ImGui::ColorEdit3("Specular", &specular.x))
	{
		material->setSpecular(specular);
	}

	float shininess = material->getShininess();
	if (ImGui::DragFloat("Shininess", &shininess))
	{
		material->setShininess(shininess);
	}

	char buf[256];
	Lumix::copyString(
		buf, sizeof(buf), material->getShader() ? material->getShader()->getPath().c_str() : "");
	if (resourceInput("Shader", buf, sizeof(buf), SHADER))
	{
		material->setShader(Lumix::Path(buf));
	}
	ImGui::SameLine();
	if (ImGui::Button(StringBuilder<30>("->##", "shader")))
	{
		selectResource(Lumix::Path(buf));
		return;
	}

	for (int i = 0; i < material->getShader()->getTextureSlotCount(); ++i)
	{
		auto& slot = material->getShader()->getTextureSlot(i);
		auto* texture = material->getTexture(i);
		Lumix::copyString(buf, sizeof(buf), texture ? texture->getPath().c_str() : "");
		if (resourceInput(slot.m_name, buf, sizeof(buf), TEXTURE))
		{
			material->setTexturePath(i, Lumix::Path(buf));
		}
		ImGui::SameLine();
		if (ImGui::Button(StringBuilder<30>("->##", slot.m_name)))
		{
			selectResource(Lumix::Path(buf));
			return;
		}
	}

	for (int i = 0; i < material->getUniformCount(); ++i)
	{
		auto& uniform = material->getUniform(i);
		switch (uniform.m_type)
		{
			case Lumix::Material::Uniform::FLOAT: 
				ImGui::DragFloat(uniform.m_name, &uniform.m_float);
				break;
		}
	}
	ImGui::Columns(1);
}


void AssetBrowser::onGuiTexture()
{
	auto* texture = static_cast<Lumix::Texture*>(m_selected_resource);

	ASSERT(texture->isReady() && bgfx::isValid(texture->getTextureHandle()));

	ImGui::LabelText("Size", "%dx%d", texture->getWidth(), texture->getHeight());
	ImGui::LabelText("BPP", "%d", texture->getBytesPerPixel());
	m_texture_handle = texture->getTextureHandle();
	ImGui::Image(&m_texture_handle, ImVec2(200, 200));
}


void AssetBrowser::onGuiLuaScript()
{
	auto* script = static_cast<Lumix::LuaScript*>(m_selected_resource);

	if (m_text_buffer[0] == '\0')
	{
		Lumix::copyString(m_text_buffer, sizeof(m_text_buffer), script->getSourceCode());
	}
	ImGui::InputTextMultiline("Code", m_text_buffer, sizeof(m_text_buffer), ImVec2(0, 300));
	if (ImGui::Button("Save"))
	{
		auto& fs = m_editor.getEngine().getFileSystem();
		auto* file = fs.open(fs.getDiskDevice(),
			m_selected_resource->getPath().c_str(),
			Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);

		if (!file)
		{
			Lumix::g_log_warning.log("Asset browser") << "Could not save "
													  << m_selected_resource->getPath().c_str();
			return;
		}

		file->write(m_text_buffer, strlen(m_text_buffer));
		fs.close(*file);
	}
}


void AssetBrowser::onGuiModel()
{
	auto* model = static_cast<Lumix::Model*>(m_selected_resource);
	ImGui::LabelText("Bone count", "%d", model->getBoneCount());
	if (model->getBoneCount() > 0) ImGui::CollapsingHeader("Bones");
	for (int i = 0; i < model->getBoneCount(); ++i)
	{
		ImGui::Text(model->getBone(i).name.c_str());
	}

	ImGui::LabelText("Bounding radius", "%f", model->getBoundingRadius());
	for (int i = 0; i < model->getMeshCount(); ++i)
	{
		auto& mesh = model->getMesh(i);
		if (ImGui::TreeNode(&mesh, mesh.getName()))
		{
			ImGui::LabelText("Triangle count", "%d", mesh.getTriangleCount());
			ImGui::Text(mesh.getMaterial()->getPath().c_str());
			ImGui::SameLine();
			if (ImGui::Button("View material"))
			{
				selectResource(mesh.getMaterial()->getPath());
			}
			ImGui::TreePop();
		}
	}
}


void AssetBrowser::onGuiResource()
{
	if (!m_selected_resource) return;


	const char* path = m_selected_resource->getPath().c_str();
	if (!ImGui::CollapsingHeader(path, nullptr, true, true)) return;

	if (m_selected_resource->isFailure())
	{
		ImGui::Text("Failed to load the resource");
		return;
	}

	if (m_selected_resource->isLoading())
	{
		ImGui::Text("Loading...");
		return;
	}

	if (!m_selected_resource->isReady())
	{
		ImGui::Text("Not ready");
		return;
	}

	auto resource_type = getResourceType(path);
	switch (resource_type)
	{
		case Lumix::ResourceManager::MATERIAL: onGuiMaterial(); break;
		case Lumix::ResourceManager::TEXTURE: onGuiTexture(); break;
		case Lumix::ResourceManager::MODEL: onGuiModel(); break;
		case Lumix::ResourceManager::SHADER: break;
		default:
			if (resource_type == LUA_SCRIPT_HASH)
			{
				onGuiLuaScript();
			}
			else if (resource_type != UNIVERSE_HASH)
			{
				ASSERT(false); // unimplemented resource
			}
			break;
	}
}


const Lumix::Array<Lumix::Path>& AssetBrowser::getResources(Type type) const
{
	return m_resources[type];
}


void AssetBrowser::addResource(const char* path, const char* filename)
{
	char ext[10];
	Lumix::PathUtils::getExtension(ext, sizeof(ext), filename);

	char fullpath[Lumix::MAX_PATH_LENGTH];
	Lumix::copyString(fullpath, sizeof(fullpath), path);
	Lumix::catString(fullpath, sizeof(fullpath), "/");
	Lumix::catString(fullpath, sizeof(fullpath), filename);

	int index = -1;
	if (strcmp(ext, "dds") == 0 || strcmp(ext, "tga") == 0) index = TEXTURE;
	if (strcmp(ext, "msh") == 0) index = MODEL;
	if (strcmp(ext, "mat") == 0) index = MATERIAL;
	if (strcmp(ext, "unv") == 0) index = UNIVERSE;
	if (strcmp(ext, "shd") == 0) index = SHADER;
	if (strcmp(ext, "lua") == 0) index = LUA_SCRIPT;

	if (index >= 0)
	{
		Lumix::Path path_obj(fullpath);
		if (m_resources[index].indexOf(path_obj) == -1)
		{
			m_resources[index].push(path_obj);
		}
	}
}


void AssetBrowser::processDir(const char* dir)
{
	auto* iter = Lumix::FS::createFileIterator(dir, m_editor.getAllocator());
	Lumix::FS::FileInfo info;
	while (Lumix::FS::getNextFile(iter, &info))
	{

		if (info.filename[0] == '.') continue;

		if (info.is_directory)
		{
			char child_path[Lumix::MAX_PATH_LENGTH];
			Lumix::copyString(child_path, sizeof(child_path), dir);
			Lumix::catString(child_path, sizeof(child_path), "/");
			Lumix::catString(child_path, sizeof(child_path), info.filename);
			processDir(child_path);
		}
		else
		{
			addResource(dir, info.filename);
		}
	}

	Lumix::FS::destroyFileIterator(iter);
}


void AssetBrowser::findResources()
{
	for (auto& resources : m_resources)
	{
		resources.clear();
	}

	processDir(".");
}