#pragma once


#include "core/lux.h"
#include "core/event_manager.h"


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
	class RenderScene;
	class ResourceManager;
	class ScriptSystem;
	class Universe;


	class LUX_ENGINE_API Engine
	{
		public:
			class UniverseCreatedEvent;
			class UniverseDestroyedEvent;

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
			RenderScene* getRenderScene() const;

			ResourceManager& getResourceManager() const;

			const char* getBasePath() const;
			void update();
			void serialize(ISerializer& serializer);
			void deserialize(ISerializer& serializer);
			float getFPS() const;

		private:
			struct EngineImpl* m_impl;
	};

	class LUX_ENGINE_API Engine::UniverseCreatedEvent : public Event
	{
		public:
			static const Event::Type s_type;

		public:
			UniverseCreatedEvent(Universe& universe)
				: m_universe(universe)
			{
				m_type = s_type;
			}

			Universe& getUniverse() { return m_universe; }

		private:
			Universe& m_universe;
	};

	class LUX_ENGINE_API Engine::UniverseDestroyedEvent : public Event
	{
		public:
			static const Event::Type s_type;

		public:
			UniverseDestroyedEvent(Universe& universe)
				: m_universe(universe)
			{
				m_type = s_type;
			}

			Universe& getUniverse() { return m_universe; }

		private:
			Universe& m_universe;
	};


} // ~namespace Lux