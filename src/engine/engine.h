#pragma once


#include "core/lux.h"


namespace Lux
{
	namespace FS
	{
			class FileSystem;
	}

	class EditorServer;
	class InputSystem;
	class IPlugin;
	class ISerializer;
	class PluginManager;
	class Renderer;
	class ScriptSystem;
	class Universe;

	class LUX_ENGINE_API Engine
	{
		public:
			Engine() { m_impl = NULL; }
			~Engine() { ASSERT(m_impl == NULL); }

			bool create(int w, int h, const char* base_path, EditorServer* editor_server);
			void destroy();
			EditorServer* getEditorServer() const;
			FS::FileSystem& getFileSystem();
			Renderer& getRenderer();
			ScriptSystem& getScriptSystem();
			InputSystem& getInputSystem();
			PluginManager& getPluginManager();
			IPlugin* loadPlugin(const char* name);
			Universe* getUniverse() const;
			Universe* createUniverse();
			void destroyUniverse();
			void update();
			void serialize(ISerializer& serializer);
			void deserialize(ISerializer& serializer);

		private:
			struct EngineImpl* m_impl;
	};

} // ~namespace Lux