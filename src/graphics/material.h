#pragma once


#include "core/pod_array.h"
#include "core/resource.h"


namespace Lux
{
namespace FS
{
	class FileSystem;
	class IFile;
}

class ResourceManager;
class Shader;
class Texture;


class Material : public Resource
{
	friend class MaterialManager;
public:
	void apply();
	void setShader(Shader* shader) { m_shader = shader; }
	void addTexture(Texture* texture) { m_textures.push(texture); }
	Shader* getShader() { return m_shader; }

protected:
	Material(const Path& path, ResourceManager& resource_manager)
		: Resource(path)
		, m_shader()
		, m_textures()
		, m_resource_manager(resource_manager)
	{ }

	private:
		void loaded(FS::IFile* file, bool success, FS::FileSystem& fs);
	~Material()
	{ }

	virtual void doLoad(void) LUX_OVERRIDE;
	virtual void doUnload(void) LUX_OVERRIDE;
	virtual void doReload(void) LUX_OVERRIDE;

private:
	void loaded(FS::IFile* file, bool success);

private:
	Shader*	m_shader;
	PODArray<Texture*> m_textures;
	ResourceManager& m_resource_manager;
};

} // ~namespace Lux
