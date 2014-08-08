#pragma once


#include "core/lumix.h"


namespace Lumix
{
	namespace FS
	{
			class FileSystem;
	}

	class WorldEditor;
	class InputSystem;
	class IPlugin;
	class ISerializer;
	class PluginManager;
	class Renderer;
	class RenderScene;
	class ResourceManager;
	class Universe;


	class LUMIX_ENGINE_API Engine
	{
		public:
			Engine() { m_impl = NULL; }
			~Engine() { ASSERT(m_impl == NULL); }

			bool create(const char* base_path, FS::FileSystem* fs, WorldEditor* editor_server);
			void destroy();

			Universe* createUniverse();
			void destroyUniverse();

			WorldEditor* getWorldEditor() const;
			FS::FileSystem& getFileSystem();
			Renderer& getRenderer();
			InputSystem& getInputSystem();
			PluginManager& getPluginManager();
			IPlugin* loadPlugin(const char* name);
			Universe* getUniverse() const;
			RenderScene* getRenderScene() const;

			ResourceManager& getResourceManager() const;

			const char* getBasePath() const;
			void update(bool is_game_running);
			void serialize(ISerializer& serializer);
			void deserialize(ISerializer& serializer);
			float getFPS() const;

		private:
			struct EngineImpl* m_impl;
	};
	

} // ~namespace Lumix
