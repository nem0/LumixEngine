#pragma once


#include "core/lux.h"


namespace Lux
{
	class EditorServer;
	class IFileSystem;
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
			Engine() { m_impl = 0; }

			bool create(int w, int h, const char* base_path, EditorServer* editor_server);
			void destroy();
			EditorServer* getEditorServer() const;
			IFileSystem& getFileSystem();
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