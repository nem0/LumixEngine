#pragma once


#include "core/lumix.h"
#include "core/array.h"


namespace Lumix
{
	namespace FS
	{
			class FileSystem;
	}

	class WorldEditor;
	class InputSystem;
	class IPlugin;
	class IScene;
	class ISerializer;
	class PluginManager;
	class Renderer;
	class ResourceManager;
	class Universe;


	class LUMIX_ENGINE_API Engine
	{
		public:
			virtual ~Engine() {}

			static Engine* create(const char* base_path, FS::FileSystem* fs, WorldEditor* editor);
			static void destroy(Engine* engine);

			virtual Universe* createUniverse() = 0;
			virtual void destroyUniverse() = 0;

			virtual WorldEditor* getWorldEditor() const = 0;
			virtual FS::FileSystem& getFileSystem() = 0;
			virtual Renderer& getRenderer() = 0;
			virtual InputSystem& getInputSystem() = 0;
			virtual PluginManager& getPluginManager() = 0;
			virtual IPlugin* loadPlugin(const char* name) = 0;
			virtual Universe* getUniverse() const = 0;
			virtual const Array<IScene*>& getScenes() const = 0;
			virtual IScene* getScene(uint32_t type) const = 0;

			virtual ResourceManager& getResourceManager() = 0;

			virtual const char* getBasePath() const = 0;
			virtual void update(bool is_game_running) = 0;
			virtual void serialize(ISerializer& serializer) = 0;
			virtual void deserialize(ISerializer& serializer) = 0;
			virtual float getFPS() const = 0;
			virtual float getLastTimeDelta() = 0;

		protected:
			Engine() {}
	};
	

} // ~namespace Lumix
