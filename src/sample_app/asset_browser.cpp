#include "asset_browser.h"
#include "core/FS/file_iterator.h"
#include "core/FS/file_system.h"
#include "core/json_serializer.h"
#include "core/path_utils.h"
#include "core/resource.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/string.h"
#include "core/system.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "ocornut-imgui/imgui.h"
#include "string_builder.h"


AssetBrowser::AssetBrowser(Lumix::WorldEditor& editor)
	: m_editor(editor)
	, m_resources(editor.getAllocator())
	, m_selected_resouce(nullptr)
{
	TODO("detect changes");
	m_is_opened = false;
	for (int i = 0; i < Count; ++i)
	{
		m_resources.emplace(editor.getAllocator());
	}

	findResources();
}


static uint32_t getResourceType(const char* path)
{
	char ext[10];
	Lumix::PathUtils::getExtension(ext, sizeof(ext), path);
	
	if (strcmp(ext, "mat") == 0) return Lumix::ResourceManager::MATERIAL;
	if (strcmp(ext, "msh") == 0) return Lumix::ResourceManager::MODEL;
	if (strcmp(ext, "dds") == 0) return Lumix::ResourceManager::TEXTURE;
	if (strcmp(ext, "tga") == 0) return Lumix::ResourceManager::TEXTURE;
	if (strcmp(ext, "shd") == 0) return Lumix::ResourceManager::SHADER;

	ASSERT(false);
	return 0;
}


void AssetBrowser::onGui()
{
	if (!m_is_opened) return;

	if (!ImGui::Begin("AssetBrowser", &m_is_opened))
	{
		ImGui::End();
		return;
	}
	
	if (ImGui::Button("Refresh"))
	{
		findResources();
	}

	const char* items = "Material\0Model\0Shader\0Texture\0Universe\0";
	static int current_type = 0;
	static char filter[128] = "";
	ImGui::Combo("Type", &current_type, items);
	ImGui::InputText("Filter", filter, sizeof(filter));

	ImGui::ListBoxHeader("Resources");
	auto* resources = &m_resources[current_type];

	for (auto& resource : *resources)
	{
		if (filter[0] != '\0' && strstr(resource.c_str(), filter) == nullptr)
			continue;

		if (ImGui::Selectable(resource.c_str(),
							  m_selected_resouce
								  ? m_selected_resouce->getPath() == resource
								  : false))
		{
			m_selected_resouce = m_editor.getEngine()
									 .getResourceManager()
									 .get(getResourceType(resource.c_str()))
									 ->load(resource);
		}
	}
	ImGui::ListBoxFooter();
	onGuiResource();
	ImGui::End();
}


void AssetBrowser::saveMaterial(Lumix::Material* material)
{
	Lumix::FS::FileSystem& fs = m_editor.getEngine().getFileSystem();
	// use temporary because otherwise the material is reloaded during saving
	char tmp_path[Lumix::MAX_PATH_LENGTH];
	strcpy(tmp_path, material->getPath().c_str());
	strcat(tmp_path, ".tmp");
	Lumix::FS::IFile* file =
		fs.open(fs.getDefaultDevice(),
		tmp_path,
		Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);
	if (file)
	{
		Lumix::DefaultAllocator allocator;
		Lumix::JsonSerializer serializer(
			*file,
			Lumix::JsonSerializer::AccessMode::WRITE,
			material->getPath().c_str(),
			allocator);
		material->save(serializer);
		fs.close(*file);

		Lumix::deleteFile(material->getPath().c_str());
		Lumix::moveFile(tmp_path, material->getPath().c_str());
	}
	else
	{
		Lumix::g_log_error.log("Material manager")
			<< "Could not save file " << material->getPath().c_str();
	}
}


bool AssetBrowser::resourceInput(const char* label, char* buf, int max_size, Type type)
{
	if (ImGui::InputText(label, buf, max_size))
	{
		return true;
	}
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
	auto* material = static_cast<Lumix::Material*>(m_selected_resouce);

	if (ImGui::Button("Save"))
	{
		saveMaterial(material);
	}

	bool b;
	if (material->hasAlphaCutoutDefine())
	{
		b = material->isAlphaCutout();
		if (ImGui::Checkbox("Is alpha cutout", &b))
			material->enableAlphaCutout(b);
	}

	b = material->isBackfaceCulling();
	if (ImGui::Checkbox("Is backface culling", &b))
		material->enableBackfaceCulling(b);

	if (material->hasShadowReceivingDefine())
	{
		b = material->isShadowReceiver();
		if (ImGui::Checkbox("Is shadow receiver", &b))
			material->enableShadowReceiving(b);
	}

	b = material->isZTest();
	if (ImGui::Checkbox("Z test", &b))
		material->enableZTest(b);

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
		buf,
		sizeof(buf),
		material->getShader() ? material->getShader()->getPath().c_str() : "");
	if (resourceInput("Shader", buf, sizeof(buf), SHADER))
	{
		material->setShader(Lumix::Path(buf));
	}

	for (int i = 0; i < material->getShader()->getTextureSlotCount();
		++i)
	{
		auto& slot = material->getShader()->getTextureSlot(i);
		auto* texture = material->getTexture(i);
		Lumix::copyString(buf,
			sizeof(buf),
			texture ? texture->getPath().c_str() : "");
		if (resourceInput(slot.m_name, buf, sizeof(buf), TEXTURE))
		{
			material->setTexturePath(i, Lumix::Path(buf));
		}
	}
}


void AssetBrowser::onGuiTexture()
{
	auto* texture = static_cast<Lumix::Texture*>(m_selected_resouce);
	ImGui::LabelText("Size", "%dx%d", texture->getWidth(), texture->getHeight());
	ImGui::LabelText("BPP", "%d", texture->getBytesPerPixel());
	ImGui::Image(texture, ImVec2(200, 200));
}


void AssetBrowser::onGuiModel()
{
	auto* model = static_cast<Lumix::Model*>(m_selected_resouce);
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
		if (ImGui::TreeNode(&mesh	, mesh.getName()))
		{
			ImGui::LabelText("Triangle count", "%d", mesh.getTriangleCount());
			ImGui::Text(mesh.getMaterial()->getPath().c_str());
			ImGui::SameLine();
			if (ImGui::Button("View material"))
			{
				m_selected_resouce = mesh.getMaterial();
			}
			ImGui::TreePop();
		}
	}
}


void AssetBrowser::onGuiResource()
{
	if (!m_selected_resouce) return;
	

	const char* path = m_selected_resouce->getPath().c_str();
	if (ImGui::CollapsingHeader(path, nullptr, true, true))
	{
		if (m_selected_resouce->isFailure())
		{
			ImGui::Text("Failed to load the resource");
			return;
		}

		if (m_selected_resouce->isLoading())
		{
			ImGui::Text("Loading...");
			return;
		}

		switch (getResourceType(path))
		{
			case Lumix::ResourceManager::MATERIAL:
				onGuiMaterial();
				break;
			case Lumix::ResourceManager::TEXTURE:
				onGuiTexture();
				break;
			case Lumix::ResourceManager::MODEL:
				onGuiModel();
				break;
			default:
				ASSERT(false);
				break;
		}
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

	if (strcmp(ext, "msh") == 0)
	{
		m_resources[MODEL].push(Lumix::Path(fullpath));
		return;
	}
	if (strcmp(ext, "dds") == 0 || strcmp(ext, "tga") == 0)
	{
		m_resources[TEXTURE].push(Lumix::Path(fullpath));
		return;
	}
	if (strcmp(ext, "mat") == 0)
	{
		m_resources[MATERIAL].push(Lumix::Path(fullpath));
		return;
	}
	if (strcmp(ext, "unv") == 0)
	{
		m_resources[UNIVERSE].push(Lumix::Path(fullpath));
		return;
	}
	if (strcmp(ext, "shd") == 0)
	{
		m_resources[SHADER].push(Lumix::Path(fullpath));
		return;
	}
}


void AssetBrowser::processDir(const char* path)
{
	auto* iter = Lumix::FS::createFileIterator(path, m_editor.getAllocator());
	Lumix::FS::FileInfo info;
	while (Lumix::FS::getNextFile(iter, &info))
	{
	
		if (info.filename[0] == '.')
			continue;

		if (info.is_directory)
		{
			char child_path[Lumix::MAX_PATH_LENGTH];
			Lumix::copyString(child_path, sizeof(child_path), path);
			Lumix::catString(child_path, sizeof(child_path), "/");
			Lumix::catString(child_path, sizeof(child_path), info.filename);
			processDir(child_path);
		}
		else
		{
			addResource(path, info.filename);
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