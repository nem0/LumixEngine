#pragma once


#include "core/lux.h"


namespace Lux
{

		struct Entity;
		class EventManager;
        struct ServerMessage;
		struct Vec3;

        class LUX_ENGINE_API EditorClient
        {
                public:
                        EditorClient() { m_impl = 0; }

                        bool create();
						void destroy();
                        void addEntity();
                        void toggleGameMode();
                        void addComponent(uint32_t type);
                        void mouseDown(int x, int y, int button);
                        void mouseUp(int x, int y, int button);
                        void mouseMove(int x, int y, int dx, int dy);
                        void requestProperties(uint32_t type_crc);
                        void setComponentProperty(const char* component, const char* property, const void* value, int32_t length);
                        void navigate(float forward, float right, int32_t fast);
						void loadUniverse(const char* path);
						void saveUniverse(const char* path);
						void setEntityPosition(int32_t entity, const Vec3& position);
						EventManager& getEventManager();

                private:
                        struct EditorClientImpl* m_impl;
        };

} // ~namespace Lux