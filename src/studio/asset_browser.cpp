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
#include "editor/ieditor_command.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "file_system_watcher.h"
#include "lua_script/lua_script_manager.h"
#include "metadata.h"
#include "ocornut-imgui/imgui.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "universe/universe.h"
#include "utils.h"


static const uint32_t UNIVERSE_HASH = Lumix::crc32("universe");
static const uint32_t SOURCE_HASH = Lumix::crc32("source");
static const uint32_t LUA_SCRIPT_HASH = Lumix::crc32("lua_script");


static uint32_t getResourceType(const char* path)
{
	char ext[10];
	Lumix::PathUtils::getExtension(ext, sizeof(ext), path);

	if (strcmp(ext, "mat") == 0) return Lumix::ResourceManager::MATERIAL;
	if (strcmp(ext, "msh") == 0) return Lumix::ResourceManager::MODEL;
	if (strcmp(ext, "dds") == 0) return Lumix::ResourceManager::TEXTURE;
	if (strcmp(ext, "raw") == 0) return Lumix::ResourceManager::TEXTURE;
	if (strcmp(ext, "tga") == 0) return Lumix::ResourceManager::TEXTURE;
	if (strcmp(ext, "shd") == 0) return Lumix::ResourceManager::SHADER;
	if (strcmp(ext, "unv") == 0) return UNIVERSE_HASH;
	if (strcmp(ext, "lua") == 0) return LUA_SCRIPT_HASH;

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

	Lumix::MT::SpinLock lock(m_changed_files_mutex);
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
	bool is_empty;
	{
		Lumix::MT::SpinLock lock(m_changed_files_mutex);
		is_empty = m_changed_files.empty();
	}

	while (!is_empty)
	{
		Lumix::Path path_obj;
		{
			Lumix::MT::SpinLock lock(m_changed_files_mutex);
			
			path_obj = m_changed_files.back();
			m_changed_files.pop();
			is_empty = m_changed_files.empty();
		}

		const char* path = path_obj.c_str();
		
		uint32_t resource_type = getResourceType(path);
		if (resource_type == 0) continue;

		if (m_autoreload_changed_resource) m_editor.getEngine().getResourceManager().reload(path);

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


void AssetBrowser::onGUI()
{
	if (m_wanted_resource.isValid())
	{
		selectResource(m_wanted_resource);
		m_wanted_resource = "";
	}

	if (!m_is_opened) return;

	if (!ImGui::Begin("AssetBrowser", &m_is_opened))
	{
		ImGui::End();
		return;
	}

	if (m_is_focus_requested)
	{
		m_is_focus_requested = false;
		ImGui::SetWindowFocus();
	}

	if (ImGui::Button("Refresh")) findResources();
	ImGui::SameLine();
	ImGui::Checkbox("Autoreload", &m_autoreload_changed_resource);

	const char* items = "Material\0Model\0Shader\0Texture\0Universe\0Lua Script\0";
	ImGui::Combo("Type", &m_current_type, items);
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
	ImGui::End();
}


void AssetBrowser::selectResource(Lumix::Resource* resource)
{
	m_text_buffer[0] = '\0';
	m_wanted_resource = "";
	unloadResource();
	m_selected_resource = resource;
	ASSERT(m_selected_resource->getRefCount() > 0);
}


void AssetBrowser::selectResource(const Lumix::Path& resource)
{
	char ext[30];
	Lumix::PathUtils::getExtension(ext, Lumix::lengthOf(ext), resource.c_str());
	if (strcmp(ext, "unv") == 0) return;

	auto& manager = m_editor.getEngine().getResourceManager();
	auto* resource_manager = manager.get(getResourceType(resource.c_str()));
	if (resource_manager) selectResource(resource_manager->load(resource));
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


bool AssetBrowser::resourceInput(const char* label, const char* str_id, char* buf, int max_size, Type type)
{
	float item_w = ImGui::CalcItemWidth();
	auto& style = ImGui::GetStyle();
	ImGui::PushItemWidth(item_w - ImGui::CalcTextSize("...View").x - style.FramePadding.x * 4 -
						 style.ItemSpacing.x * 2);

	if (ImGui::InputText(StringBuilder<30>("##", str_id), buf, max_size)) return true;

	ImGui::SameLine();
	StringBuilder<50> popup_name("pu", str_id);
	if (ImGui::Button(StringBuilder<30>("...##browse", str_id)))
	{
		ImGui::OpenPopup(popup_name);
	}
	ImGui::SameLine();
	if (ImGui::Button(StringBuilder<30>("View##go", str_id)))
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


void AssetBrowser::onGUIMaterial()
{
	auto* material = static_cast<Lumix::Material*>(m_selected_resource);

	if (ImGui::Button("Save")) saveMaterial(material);
	ImGui::SameLine();
	if (ImGui::Button("Open in external editor")) openInExternalEditor(material);

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
	Lumix::copyString(buf, material->getShader() ? material->getShader()->getPath().c_str() : "");
	if (resourceInput("Shader", "shader", buf, sizeof(buf), SHADER))
	{
		material->setShader(Lumix::Path(buf));
	}

	for (int i = 0; i < material->getShader()->getTextureSlotCount(); ++i)
	{
		auto& slot = material->getShader()->getTextureSlot(i);
		auto* texture = material->getTexture(i);
		Lumix::copyString(buf, texture ? texture->getPath().c_str() : "");
		if (resourceInput(
				slot.m_name, StringBuilder<30>("", (uint64_t)&slot), buf, sizeof(buf), TEXTURE))
		{
			material->setTexturePath(i, Lumix::Path(buf));
		}
		if (!texture) continue;

		ImGui::SameLine();
		StringBuilder<100> popup_name("pu", (uint64_t)texture, slot.m_name);
		if (ImGui::Button(StringBuilder<100>("Advanced##adv", (uint64_t)texture, slot.m_name)))
		{
			ImGui::OpenPopup(popup_name);
		}

		if (ImGui::BeginPopup(popup_name))
		{
			bool u_clamp = (texture->getFlags() & BGFX_TEXTURE_U_CLAMP) != 0;
			if (ImGui::Checkbox("u clamp", &u_clamp))
			{
				texture->setFlag(BGFX_TEXTURE_U_CLAMP, u_clamp);
			}
			bool v_clamp = (texture->getFlags() & BGFX_TEXTURE_V_CLAMP) != 0;
			if (ImGui::Checkbox("v clamp", &v_clamp))
			{
				texture->setFlag(BGFX_TEXTURE_V_CLAMP, v_clamp);
			}
			bool min_point = (texture->getFlags() & BGFX_TEXTURE_MIN_POINT) != 0;
			if (ImGui::Checkbox("Min point", &min_point))
			{
				texture->setFlag(BGFX_TEXTURE_MIN_POINT, min_point);
			}
			bool mag_point = (texture->getFlags() & BGFX_TEXTURE_MAG_POINT) != 0;
			if (ImGui::Checkbox("Mag point", &mag_point))
			{
				texture->setFlag(BGFX_TEXTURE_MAG_POINT, mag_point);
			}
			if (slot.m_is_atlas)
			{
				int size = texture->getAtlasSize() - 2;
				const char values[] = { '2', 'x', '2', 0, '3', 'x', '3', 0, '4', 'x', '4', 0, 0 };
				if (ImGui::Combo(StringBuilder<30>("Atlas size##", i), &size, values))
				{
					texture->setAtlasSize(size + 2);
				}
			}
			ImGui::EndPopup();

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


void AssetBrowser::onGUITexture()
{
	auto* texture = static_cast<Lumix::Texture*>(m_selected_resource);
	if (texture->isFailure())
	{
		ImGui::Text("Texture failed to load");
		return;
	}

	ImGui::LabelText("Size", "%dx%d", texture->getWidth(), texture->getHeight());
	ImGui::LabelText("BPP", "%d", texture->getBytesPerPixel());
	m_texture_handle = texture->getTextureHandle();
	if (bgfx::isValid(m_texture_handle))
	{
		ImGui::Image(&m_texture_handle, ImVec2(200, 200));
		if (ImGui::Button("Open"))
		{
			openInExternalEditor(m_selected_resource);
		}
	}
}


void AssetBrowser::onGUILuaScript()
{
	auto* script = static_cast<Lumix::LuaScript*>(m_selected_resource);

	if (m_text_buffer[0] == '\0')
	{
		Lumix::copyString(m_text_buffer, script->getSourceCode());
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
	ImGui::SameLine();
	if (ImGui::Button("Open in external editor"))
	{
		openInExternalEditor(m_selected_resource);
	}
}


void AssetBrowser::openInExternalEditor(Lumix::Resource* resource)
{
	StringBuilder<Lumix::MAX_PATH_LENGTH> path(m_editor.getBasePath());
	path << "/" << resource->getPath().c_str();
	Lumix::shellExecuteOpen(path);
}


void AssetBrowser::onGUIShader()
{
	auto* shader = static_cast<Lumix::Shader*>(m_selected_resource);
	StringBuilder<Lumix::MAX_PATH_LENGTH> path(m_editor.getBasePath());
	char basename[Lumix::MAX_PATH_LENGTH];
	Lumix::PathUtils::getBasename(
		basename, Lumix::lengthOf(basename), m_selected_resource->getPath().c_str());
	path << "/shaders/" << basename;
	if (ImGui::Button("Open vertex shader"))
	{
		path << "_vs.sc";
		Lumix::shellExecuteOpen(path);
	}
	ImGui::SameLine();
	if (ImGui::Button("Open fragment shader"))
	{
		path << "_fs.sc";
		Lumix::shellExecuteOpen(path);
	}

	if (ImGui::CollapsingHeader("Texture slots", nullptr, true, true))
	{
		ImGui::Columns(2);
		ImGui::Text("name");
		ImGui::NextColumn();
		ImGui::Text("uniform");
		ImGui::NextColumn();
		ImGui::Separator();
		for (int i = 0; i < shader->getTextureSlotCount(); ++i)
		{
			auto& slot = shader->getTextureSlot(i);
			ImGui::Text(slot.m_name);
			ImGui::NextColumn();
			ImGui::Text(slot.m_uniform);
			ImGui::NextColumn();
		}
		ImGui::Columns(1);
	}
}


class InsertMeshCommand : public Lumix::IEditorCommand
{
public:
	InsertMeshCommand(Lumix::WorldEditor& editor);
	InsertMeshCommand(Lumix::WorldEditor& editor, const Lumix::Vec3& position, const Lumix::Path& mesh_path);

	virtual void serialize(Lumix::JsonSerializer& serializer) override;
	virtual void deserialize(Lumix::JsonSerializer& serializer) override;
	virtual bool execute() override;
	virtual void undo() override;
	virtual uint32_t getType() override;
	virtual bool merge(IEditorCommand&);
	Lumix::Entity getEntity() const { return m_entity; }

private:
	Lumix::Vec3 m_position;
	Lumix::Path m_mesh_path;
	Lumix::Entity m_entity;
	Lumix::WorldEditor& m_editor;
};



static const uint32_t RENDERABLE_HASH = Lumix::crc32("renderable");


InsertMeshCommand::InsertMeshCommand(Lumix::WorldEditor& editor)
	: m_editor(editor)
{
}


InsertMeshCommand::InsertMeshCommand(Lumix::WorldEditor& editor,
	const Lumix::Vec3& position,
	const Lumix::Path& mesh_path)
	: m_mesh_path(mesh_path)
	, m_position(position)
	, m_editor(editor)
{
}


void InsertMeshCommand::serialize(Lumix::JsonSerializer& serializer)
{
	serializer.serialize("path", m_mesh_path.c_str());
	serializer.beginArray("pos");
	serializer.serializeArrayItem(m_position.x);
	serializer.serializeArrayItem(m_position.y);
	serializer.serializeArrayItem(m_position.z);
	serializer.endArray();
}


void InsertMeshCommand::deserialize(Lumix::JsonSerializer& serializer)
{
	char path[Lumix::MAX_PATH_LENGTH];
	serializer.deserialize("path", path, sizeof(path), "");
	m_mesh_path = path;
	serializer.deserializeArrayBegin("pos");
	serializer.deserializeArrayItem(m_position.x, 0);
	serializer.deserializeArrayItem(m_position.y, 0);
	serializer.deserializeArrayItem(m_position.z, 0);
	serializer.deserializeArrayEnd();
}


bool InsertMeshCommand::execute()
{
	Lumix::Universe* universe = m_editor.getUniverse();
	m_entity = universe->createEntity(Lumix::Vec3(0, 0, 0), Lumix::Quat(0, 0, 0, 1));
	universe->setPosition(m_entity, m_position);
	const Lumix::Array<Lumix::IScene*>& scenes = m_editor.getScenes();
	Lumix::ComponentIndex cmp;
	Lumix::IScene* scene = nullptr;
	for (int i = 0; i < scenes.size(); ++i)
	{
		cmp = scenes[i]->createComponent(RENDERABLE_HASH, m_entity);

		if (cmp >= 0)
		{
			scene = scenes[i];
			break;
		}
	}
	if (cmp >= 0)
	{
		char rel_path[Lumix::MAX_PATH_LENGTH];
		m_editor.getRelativePath(rel_path, Lumix::MAX_PATH_LENGTH, m_mesh_path.c_str());
		static_cast<Lumix::RenderScene*>(scene)->setRenderablePath(cmp, rel_path);
	}
	return true;
}


void InsertMeshCommand::undo()
{
	const Lumix::WorldEditor::ComponentList& cmps =
		m_editor.getComponents(m_entity);
	for (int i = 0; i < cmps.size(); ++i)
	{
		cmps[i].scene->destroyComponent(cmps[i].index, cmps[i].type);
	}
	m_editor.getUniverse()->destroyEntity(m_entity);
	m_entity = Lumix::INVALID_ENTITY;
}


uint32_t InsertMeshCommand::getType()
{
	static const uint32_t type = Lumix::crc32("insert_mesh");
	return type;
}


bool InsertMeshCommand::merge(IEditorCommand&)
{
	return false;
}


static void insertInScene(Lumix::WorldEditor& editor, Lumix::Model* model)
{
	auto* command = LUMIX_NEW(editor.getAllocator(), InsertMeshCommand)(
		editor, editor.getCameraRaycastHit(), model->getPath());

	editor.executeCommand(command);
}


void AssetBrowser::onGUIModel()
{
	auto* model = static_cast<Lumix::Model*>(m_selected_resource);
	if (ImGui::Button("Insert in scene"))
	{
		insertInScene(m_editor, model);
	}

	ImGui::LabelText("Bone count", "%d", model->getBoneCount());
	if (model->getBoneCount() > 0) ImGui::CollapsingHeader("Bones");
	for (int i = 0; i < model->getBoneCount(); ++i)
	{
		ImGui::Text(model->getBone(i).name.c_str());
	}

	ImGui::LabelText("Bounding radius", "%f", model->getBoundingRadius());
	
	auto& lods = model->getLODs();
	if (!lods.empty())
	{
		ImGui::Separator();
		ImGui::Columns(3);
		ImGui::Text("LOD"); ImGui::NextColumn();
		ImGui::Text("Distance"); ImGui::NextColumn();
		ImGui::Text("# of meshes"); ImGui::NextColumn();
		ImGui::Separator();
		for (int i = 0; i < lods.size() - 1; ++i)
		{
			ImGui::Text("%d", i); ImGui::NextColumn();
			ImGui::DragFloat("", &lods[i].m_distance); ImGui::NextColumn();
			ImGui::Text("%d", lods[i].m_to_mesh - lods[i].m_from_mesh + 1); ImGui::NextColumn();
		}

		ImGui::Text("%d", lods.size() - 1); ImGui::NextColumn();
		ImGui::Text("INFINITE"); ImGui::NextColumn();
		ImGui::Text("%d", lods.back().m_to_mesh - lods.back().m_from_mesh + 1);
		ImGui::Columns(1);
	}

	ImGui::Separator();
	for (int i = 0; i < model->getMeshCount(); ++i)
	{
		auto& mesh = model->getMesh(i);
		if (ImGui::TreeNode(&mesh, mesh.getName()[0] != 0 ? mesh.getName() : "N/A"))
		{
			ImGui::LabelText("Triangle count", "%d", mesh.getTriangleCount());
			ImGui::LabelText("Material", mesh.getMaterial()->getPath().c_str());
			ImGui::SameLine();
			if (ImGui::Button("->"))
			{
				selectResource(mesh.getMaterial()->getPath());
			}
			ImGui::TreePop();
		}
	}
}


void AssetBrowser::onGUIResource()
{
	if (!m_selected_resource) return;


	const char* path = m_selected_resource->getPath().c_str();
	ImGui::Separator();
	ImGui::LabelText("Selected resource", path);
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
	switch (resource_type)
	{
		case Lumix::ResourceManager::MATERIAL: onGUIMaterial(); break;
		case Lumix::ResourceManager::TEXTURE: onGUITexture(); break;
		case Lumix::ResourceManager::MODEL: onGUIModel(); break;
		case Lumix::ResourceManager::SHADER: onGUIShader(); break;
		default:
			if (resource_type == LUA_SCRIPT_HASH)
			{
				onGUILuaScript();
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
	Lumix::copyString(fullpath, path);
	Lumix::catString(fullpath, "/");
	Lumix::catString(fullpath, filename);

	int index = -1;
	if (strcmp(ext, "dds") == 0 || strcmp(ext, "tga") == 0 || strcmp(ext, "raw") == 0) index = TEXTURE;
	if (strcmp(ext, "msh") == 0) index = MODEL;
	if (strcmp(ext, "mat") == 0) index = MATERIAL;
	if (strcmp(ext, "unv") == 0) index = UNIVERSE;
	if (strcmp(ext, "shd") == 0) index = SHADER;
	if (strcmp(ext, "lua") == 0) index = LUA_SCRIPT;

	if (Lumix::startsWith(path, "./render_tests") != 0 || Lumix::startsWith(path, "./unit_tests") != 0)
	{
		return;
	}

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
			Lumix::copyString(child_path, dir);
			Lumix::catString(child_path, "/");
			Lumix::catString(child_path, info.filename);
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