#pragma once


#include "core/pod_array.h"


namespace Lux
{
namespace FS
{
	class FileSystem;
	class IFile;
}

class Shader;
class Texture;


class Material
{
	public:
		void load(const char* path, FS::FileSystem& file_system);
		void apply();
		void setShader(Shader* shader) { m_shader = shader; }
		void addTexture(Texture* texture) { m_textures.push(texture); }
		Shader* getShader() { return m_shader; }

	private:
		void loaded(FS::IFile* file, bool success);

	private:
		Shader*	m_shader;
		PODArray<Texture*> m_textures;
};


} // ~namespace Lux
