#pragma once

#include "core/lux.h"


namespace Lux
{
	
class ISerializer;
class Renderer;
namespace FS
{
	class FileSystem;
	class IFile;
}

class LUX_ENGINE_API Pipeline LUX_ABSTRACT
{
	public:
		virtual ~Pipeline() {}

		virtual void render() = 0;
		virtual bool deserialize(ISerializer& serializer) = 0;
		virtual void load(const char* path, FS::FileSystem& file_system) = 0;
		virtual const char* getPath() = 0;

		static Pipeline* create(Renderer& renderer);
		static void destroy(Pipeline* pipeline);
};


}