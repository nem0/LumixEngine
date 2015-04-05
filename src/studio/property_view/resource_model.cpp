#include "resource_model.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/texture.h"


ResourceModel::ResourceModel(Lumix::Resource* resource)
	: m_resource(resource)
{
	m_resource->getObserverCb().bind<ResourceModel, &ResourceModel::onResourceLoaded>(this);
	if (resource->isReady())
	{
		onResourceLoaded(Lumix::Resource::State::READY, Lumix::Resource::State::READY);
	}
}


ResourceModel::~ResourceModel()
{
	m_resource->getObserverCb().unbind<ResourceModel, &ResourceModel::onResourceLoaded>(this);
}


void ResourceModel::fillModelInfo()
{
	Lumix::Model* model = static_cast<Lumix::Model*>(m_resource);
	auto object = this->object("Model", model);
	object
		.property("Bone count", &Lumix::Model::getBoneCount)
		.property("Bounding radius", &Lumix::Model::getBoundingRadius)
		.array("Meshes", model->getMeshCount(), &Lumix::Model::getMeshPtr, [](const Lumix::Mesh* mesh) -> const char* { return mesh->getName(); })
		.property("Triangles", &Lumix::Mesh::getTriangleCount)
		.property("Material", [](const Lumix::Mesh* mesh) -> const char* { return mesh->getMaterial()->getPath().c_str(); });
}


static Lumix::Material::Uniform* getMaterialUniform(Lumix::Material* material, QString name)
{
	for (int i = 0; i < material->getUniformCount(); ++i)
	{
		if (name == material->getUniform(i).m_name)
		{
			return &material->getUniform(i);
		}
	}
	return NULL;
}


void ResourceModel::fillMaterialInfo()
{
	Lumix::Material* material = static_cast<Lumix::Material*>(m_resource);
	auto object = this->object("Material", material);
	object
		.property("Alpha cutout", &Lumix::Material::isAlphaCutout, &Lumix::Material::enableAlphaCutout)
		.property("Alpha to coverage", &Lumix::Material::isAlphaToCoverage, &Lumix::Material::enableAlphaToCoverage)
		.property("Backface culling", &Lumix::Material::isBackfaceCulling, &Lumix::Material::enableBackfaceCulling)
		.property("Shadow receiver", &Lumix::Material::isShadowReceiver, &Lumix::Material::enableShadowReceiving)
		.property("Z test", &Lumix::Material::isZTest, &Lumix::Material::enableZTest)
		.property("Shader", [](Lumix::Material* material) -> const char* { return material->getShader()->getPath().c_str(); });
	object
		.array("Textures", material->getTextureCount(), &Lumix::Material::getTexture, [](Lumix::Texture* texture) -> const char* { return texture->getPath().c_str(); })
		.property("Width", &Lumix::Texture::getWidth)
		.property("Height", &Lumix::Texture::getHeight)
		.property("Bytes per pixel", &Lumix::Texture::getBytesPerPixel);
	for (int i = 0; i < material->getUniformCount(); ++i)
	{
		auto& uniform = material->getUniform(i);
		if (uniform.m_is_editable)
		{
			QString name = uniform.m_name;
			object.property(uniform.m_name
				, [name](Lumix::Material* material) -> QVariant {
					Lumix::Material::Uniform* uniform = getMaterialUniform(material, name);
					if (uniform)
					{
						switch (uniform->m_type)
						{
							case Lumix::Material::Uniform::FLOAT:
								return uniform->m_float;
						}
					}
					return QVariant();
				}
				, [name](Lumix::Material* material, const QVariant& value) {
					Lumix::Material::Uniform* uniform = getMaterialUniform(material, name);
					if (uniform)
					{
						switch (uniform->m_type)
						{
							case Lumix::Material::Uniform::FLOAT:
								uniform->m_float = value.toFloat();
								break;
						}
					}
				}
			);
		}
	}
}


void ResourceModel::fillTextureInfo()
{
	Lumix::Texture* texture = static_cast<Lumix::Texture*>(m_resource);
	object("Texture", texture)
		.property("Width", &Lumix::Texture::getWidth)
		.property("Height", &Lumix::Texture::getHeight)
		.property("Bytes per pixel", &Lumix::Texture::getBytesPerPixel);
}


void ResourceModel::onResourceLoaded(Lumix::Resource::State, Lumix::Resource::State new_state)
{
	if (new_state == Lumix::Resource::State::READY)
	{
		beginResetModel();
		if (dynamic_cast<Lumix::Model*>(m_resource))
		{
			fillModelInfo();
		}
		else if (dynamic_cast<Lumix::Material*>(m_resource))
		{
			fillMaterialInfo();
		}
		else if (dynamic_cast<Lumix::Texture*>(m_resource))
		{
			fillTextureInfo();
		}
		else
		{
			Q_ASSERT(false);
		}
		endResetModel();
	}
}
