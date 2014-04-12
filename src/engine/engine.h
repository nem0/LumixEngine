#pragma once


#include "core/lux.h"


namespace Lux
{
	namespace FS
	{
			class FileSystem;
	}

	class EditorServer;
	class EventManager;
	class InputSystem;
	class IPlugin;
	class ISerializer;
	class PluginManager;
	class Renderer;
	class ScriptSystem;
	class Universe;

	class ResourceManager;

	class LUX_ENGINE_API Engine
	{
		public:
			Engine() { m_impl = NULL; }
			~Engine() { ASSERT(m_impl == NULL); }

			bool create(const char* base_path, FS::FileSystem* fs, EditorServer* editor_server);
			void destroy();

			Universe* createUniverse();
			void destroyUniverse();

			EditorServer* getEditorServer() const;
			FS::FileSystem& getFileSystem();
			Renderer& getRenderer();
			ScriptSystem& getScriptSystem();
			InputSystem& getInputSystem();
			PluginManager& getPluginManager();
			EventManager& getEventManager() const;
			IPlugin* loadPlugin(const char* name);
			Universe* getUniverse() const;

			ResourceManager& getResourceManager() const;

			const char* getBasePath() const;
			void update();
			void serialize(ISerializer& serializer);
			void deserialize(ISerializer& serializer);
			float getFPS() const;

		private:
			struct EngineImpl* m_impl;
	};

} // ~namespace Lux