#include <SDL.h>
#include <SDL_opengl.h>
#include "editor/editor_client.h"
#include "editor/editor_server.h"
#include "editor/server_message_types.h"
#include "editor_native/main_frame.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "graphics/renderer.h"
#include "gui/gui.h"
#include "gui/opengl_renderer.h"

struct App
{
	void initGui(Lux::EditorClient& client, Lux::EditorServer& server)
	{
		Lux::UI::OpenGLRenderer* renderer = LUX_NEW(Lux::UI::OpenGLRenderer)();
		renderer->create();
		renderer->loadFont("gui/font.tga", server.getEngine().getFileSystem());
		renderer->setWindowHeight(600);
		server.getEngine().loadPlugin("gui.dll");
		Lux::UI::Gui* gui = (Lux::UI::Gui*)server.getEngine().getPluginManager().getPlugin("gui");
		gui->setRenderer(*renderer);
		gui->createBaseDecorators("gui/skin.atl");

		m_main_frame = LUX_NEW(MainFrame)(client, *gui, gui->createTopLevelBlock(800, 600));
		m_main_frame->getParent()->layout();
		m_gui = gui;
	}

	bool create()
	{
		SDL_Renderer* displayRenderer;
		SDL_Init(SDL_INIT_VIDEO);
		SDL_RendererInfo displayRendererInfo;
		SDL_CreateWindowAndRenderer(800, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE, &m_display_window, &displayRenderer);
		SDL_GetRendererInfo(displayRenderer, &displayRendererInfo);
		SDL_GLContext context = SDL_GL_CreateContext(m_display_window);
		if ((displayRendererInfo.flags & SDL_RENDERER_ACCELERATED) == 0 || 
			(displayRendererInfo.flags & SDL_RENDERER_TARGETTEXTURE) == 0) {
				return false;
		}
		SDL_GL_MakeCurrent(m_display_window, context);
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 2 );
    
		char path[MAX_PATH];
		GetCurrentDirectoryA(MAX_PATH, path);
		m_server.create(NULL, NULL, path);
		m_server.onResize(800, 600);
		m_client.create();
	
		initGui(m_client, m_server);
		Lux::UI::Gui* gui = (Lux::UI::Gui*)m_server.getEngine().getPluginManager().getPlugin("gui");
		return true;
	}

	void handleEvents()
	{
		SDL_Event evt;
		while(SDL_PollEvent(&evt))
		{
			switch(evt.type)
			{
				case SDL_WINDOWEVENT:
					if(evt.window.event == SDL_WINDOWEVENT_RESIZED)
					{
						static_cast<Lux::UI::OpenGLRenderer&>(m_gui->getRenderer()).setWindowHeight(evt.window.data2);
						m_server.onResize(evt.window.data1, evt.window.data2);
						m_main_frame->getParent()->setArea(0, 0, 0, 0, 0, (float)evt.window.data1, 0, (float)evt.window.data2);
						m_main_frame->getParent()->layout();
					}
					break;
				case SDL_TEXTEDITING:
					evt.text.text;
					m_finished = false;
					break;
				case SDL_KEYDOWN:
					m_gui->keyDown(evt.key.keysym.sym);
					if(evt.key.keysym.sym == SDLK_ESCAPE)
						m_finished = true;
					break;
				case SDL_KEYUP:
					break;
				case SDL_MOUSEBUTTONDOWN:
					m_gui->mouseDown(evt.button.x, evt.button.y);
					if(!m_gui->click(evt.button.x, evt.button.y))
					{
						m_client.mouseDown(evt.button.x, evt.button.y, evt.button.button == SDL_BUTTON_LEFT ? 0 : 2);
					}
					break;
				case SDL_MOUSEBUTTONUP:
					m_gui->mouseUp(evt.button.x, evt.button.y);
					m_client.mouseUp(evt.button.x, evt.button.y, evt.button.button == SDL_BUTTON_LEFT ? 0 : 2);
					break;
				case SDL_MOUSEMOTION:
					m_gui->mouseMove(evt.motion.x, evt.motion.y, evt.motion.xrel, evt.motion.yrel);
					m_client.mouseMove(evt.motion.x, evt.motion.y, evt.motion.xrel, evt.motion.yrel);
					break;
				case SDL_QUIT:
					m_finished = true;
					break;
			}
		}
		const Uint8* keys = SDL_GetKeyboardState(NULL);
		if(m_gui->getFocusedBlock() == NULL)
		{

			bool forward = keys[SDL_SCANCODE_W] != 0;
			bool backward = keys[SDL_SCANCODE_S] != 0;
			bool left =  keys[SDL_SCANCODE_A] != 0;
			bool right =  keys[SDL_SCANCODE_D] != 0;
			bool shift =  keys[SDL_SCANCODE_LSHIFT] != 0;
			if (forward || backward || left || right)
			{
				float camera_speed = 1.0f;
				m_client.navigate(forward ? camera_speed : (backward ? -camera_speed : 0.0f)
					, right ? camera_speed : (left ? -camera_speed : 0.0f)
					, shift ? 1 : 0);
			} 
		}
	}


	void mainLoop()
	{
		m_finished = false;
		while(!m_finished)
		{
			handleEvents();
			update();
			render();
		}
	}

	void update()
	{
		m_main_frame->update();
		m_server.tick(NULL, NULL);
	}

	void render()
	{
		m_gui->render();
		SDL_GL_SwapWindow(m_display_window);
	}

	void destroy()
	{
		m_server.destroy();
	    SDL_Quit();
	}

	MainFrame* m_main_frame;
	Lux::UI::Gui* m_gui;
	Lux::EditorServer m_server;
	Lux::EditorClient m_client;
	SDL_Window* m_display_window;
	bool m_finished;

};


int main(int argc, char* argv[])
{
	App app;
	app.create();
	app.mainLoop();
	app.destroy();	
    return 0;
}