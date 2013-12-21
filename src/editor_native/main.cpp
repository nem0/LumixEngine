#include "SDL.h"
#include <SDL_opengl.h>
#include "core/crc32.h"
#include "core/file_system.h"
#include "core/json_serializer.h"
#include "core/memory_stream.h"
#include "editor/editor_client.h"
#include "editor/editor_server.h"
#include "editor/server_message_types.h"
#include "editor_native/main_frame.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "graphics/renderer.h"
#include "gui/block.h"
#include "gui/decorators/box_decorator.h"
#include "gui/decorators/check_box_decorator.h"
#include "gui/decorators/text_decorator.h"
#include "gui/gui.h"
#include "gui/opengl_renderer.h"
#include "platform/socket.h"


SDL_Renderer* displayRenderer;
SDL_Window* displayWindow;
MainFrame g_main_frame;


void Display_InitGL()
{
    glShadeModel( GL_SMOOTH );
    glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
    glClearDepth( 1.0f );
    glEnable( GL_DEPTH_TEST );
    glDepthFunc( GL_LEQUAL );
    glHint( GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST );
}

int Display_SetViewport( int width, int height )
{
    GLfloat ratio;
    if ( height == 0 ) {
        height = 1;
    }
    ratio = ( GLfloat )width / ( GLfloat )height;
    glViewport( 0, 0, ( GLsizei )width, ( GLsizei )height );
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity( );
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity( );
    return 1;
}
 

void initGui(Lux::EditorClient& client, Lux::EditorServer& server)
{
	Lux::UI::OpenGLRenderer* renderer = new Lux::UI::OpenGLRenderer();
	renderer->create();
	renderer->loadFont("gui/font.tga");
	renderer->setWindowHeight(600);
	Lux::UI::CheckBoxDecorator* check_box_decorator = new Lux::UI::CheckBoxDecorator("_check_box");
	Lux::UI::TextDecorator* text_decorator = new Lux::UI::TextDecorator("_text");
	Lux::UI::TextDecorator* text_centered_decorator = new Lux::UI::TextDecorator("_text_centered");
	text_centered_decorator->setTextCentered(true);
	Lux::UI::BoxDecorator* box_decorator = new Lux::UI::BoxDecorator("_box");
	server.getEngine().loadPlugin("gui.dll");
	Lux::UI::Gui* gui = (Lux::UI::Gui*)server.getEngine().getPluginManager().getPlugin("gui");
	gui->addDecorator(*text_decorator);
	gui->addDecorator(*text_centered_decorator);
	gui->addDecorator(*box_decorator);
	gui->addDecorator(*check_box_decorator);
	gui->setRenderer(*renderer);
	check_box_decorator->create(*gui, "gui/skin.atl");
	box_decorator->create(*gui, "gui/skin.atl");
	g_main_frame.create(client, *gui, 800, 600);
}


int main(int argc, char* argv[])
{
	SDL_Init(SDL_INIT_VIDEO);
    SDL_RendererInfo displayRendererInfo;
    SDL_CreateWindowAndRenderer(800, 600, SDL_WINDOW_OPENGL, &displayWindow, &displayRenderer);
    SDL_GetRendererInfo(displayRenderer, &displayRendererInfo);
	SDL_GLContext context = SDL_GL_CreateContext(displayWindow);
    if ((displayRendererInfo.flags & SDL_RENDERER_ACCELERATED) == 0 || 
        (displayRendererInfo.flags & SDL_RENDERER_TARGETTEXTURE) == 0) {
			return -1;
    }
	SDL_GL_MakeCurrent(displayWindow, context);
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 2 );
    
   // Display_InitGL();
    
    Display_SetViewport(800, 600);
    
	char path[MAX_PATH];
	GetCurrentDirectoryA(MAX_PATH, path);
	GLenum glerr = glGetError();
	const GLubyte* glstr = glGetString(GL_VENDOR);
	Lux::EditorServer server;
	server.create(NULL, NULL, path);
	server.onResize(800, 600);
	Lux::EditorClient client;
	client.create();
	initGui(client, server);
	Lux::UI::Gui* gui = (Lux::UI::Gui*)server.getEngine().getPluginManager().getPlugin("gui");
	SDL_Event evt;
	bool finished = false;
	while(!finished)
	{
		while(SDL_PollEvent(&evt))
		{
			switch(evt.type)
			{
				case SDL_TEXTEDITING:
					evt.text.text;
					finished = false;
					break;
				case SDL_KEYDOWN:
					gui->keyDown(evt.key.keysym.sym);
					if(evt.key.keysym.sym == SDLK_ESCAPE)
						finished = true;
					break;
				case SDL_KEYUP:
					break;
				case SDL_MOUSEBUTTONDOWN:
					if(!gui->click(evt.button.x, evt.button.y))
					{
						client.mouseDown(evt.button.x, evt.button.y, evt.button.button == SDL_BUTTON_LEFT ? 0 : 2);
					}
					break;
				case SDL_MOUSEBUTTONUP:
					client.mouseUp(evt.button.x, evt.button.y, evt.button.button == SDL_BUTTON_LEFT ? 0 : 2);
					break;
				case SDL_MOUSEMOTION:
					client.mouseMove(evt.motion.x, evt.motion.y, evt.motion.xrel, evt.motion.yrel);
					break;
				case SDL_QUIT:
					finished = true;
					break;
			}
		}
		const Uint8* keys = SDL_GetKeyboardState(NULL);
		if(gui->getFocusedBlock() == NULL)
		{

			bool forward = keys[SDL_SCANCODE_W] != 0;
			bool backward = keys[SDL_SCANCODE_S] != 0;
			bool left =  keys[SDL_SCANCODE_A] != 0;
			bool right =  keys[SDL_SCANCODE_D] != 0;
			bool shift =  keys[SDL_SCANCODE_LSHIFT] != 0;
			if (forward || backward || left || right)
			{
				float camera_speed = 1.0f;
				client.navigate(forward ? camera_speed : (backward ? -camera_speed : 0.0f)
					, right ? camera_speed : (left ? -camera_speed : 0.0f)
					, shift ? 1 : 0);
			} 
		}
		server.tick(NULL, NULL);
		gui->render();
		SDL_GL_SwapWindow(displayWindow);
	}
    SDL_Quit();
    return 0;
}