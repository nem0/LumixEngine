#include "SDL.h"
#include <SDL_opengl.h>
#include "core/crc32.h"
#include "core/ifilesystem.h"
#include "core/json_serializer.h"
#include "core/memory_stream.h"
#include "editor/editor_server.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "graphics/renderer.h"
#include "gui/block.h"
#include "gui/controls.h"
#include "gui/decorators/box_decorator.h"
#include "gui/decorators/text_decorator.h"
#include "gui/gui.h"
#include "gui/opengl_renderer.h"
#include "platform/socket.h"

SDL_Renderer* displayRenderer;
SDL_Window* displayWindow;
Lux::Engine g_engine;

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
 

void clickClicked(Lux::UI::Block& block)
{
}


void initGui(Lux::EditorServer& server)
{
	Lux::UI::OpenGLRenderer* renderer = new Lux::UI::OpenGLRenderer();
	renderer->create();
	renderer->loadFont("gui/font.tga");
	renderer->setWindowHeight(600);
	Lux::UI::TextDecorator* text_decorator = new Lux::UI::TextDecorator("_text");
	Lux::UI::BoxDecorator* box_decorator = new Lux::UI::BoxDecorator("_box");
	box_decorator->create(*renderer, "gui/invader.tga");
	box_decorator->setPart(0, 252/512.0f, 6/512.0f, (399-252)/512.0f, 31/512.0f);
	server.getEngine().loadPlugin("gui.dll");
	Lux::UI::Gui* gui = (Lux::UI::Gui*)server.getEngine().getPluginManager().getPlugin("gui");
	gui->addDecorator(*text_decorator);
	gui->addDecorator(*box_decorator);
	gui->setRenderer(*renderer);
	Lux::UI::Block* root = gui->createTopLevelBlock(800, 600);
	
	/*Lux::UI::Block* button = Lux::UI::createButton("Click here!", 0, 0, root, gui);
	gui->addCallback("clickClicked", &clickClicked);
	button->registerEventHandler("click", gui->getCallback("clickClicked"));*/
	Lux::UI::createTextBox(0, 0, root, gui);
	
	Lux::MemoryStream stream;
	Lux::JsonSerializer serializer(stream, Lux::JsonSerializer::WRITE);
	root->serialize(serializer);

	
	{
		stream.rewindForRead();
		Lux::JsonSerializer serializer2(stream, Lux::JsonSerializer::READ);
		Lux::UI::Block* root2 = gui->createTopLevelBlock(800, 600);
		root2->deserialize(serializer2);
		//root->hide();
		root2->setPosition(100, 100);
		root2->layout();
	}

	/*Lux::UI::Block* button = new Lux::UI::Block();
	button->create(root, text_decorator);
	button->setText("click here!");
	button->setPosition(0, 0);
	button->setSize(100, 20);*/

	root->layout();
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
	initGui(server);
	Lux::UI::Gui* gui = (Lux::UI::Gui*)server.getEngine().getPluginManager().getPlugin("gui");

	SDL_Event evt;
	bool finished = false;
	int mx, my;
	SDL_GetMouseState(&mx, &my);
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
					gui->click(evt.button.x, evt.button.y);
					break;
				case SDL_MOUSEBUTTONUP:
					break;
				case SDL_MOUSEMOTION:
					break;
				case SDL_QUIT:
					finished = true;
					break;
			}
		}
		server.tick(NULL, NULL);
		gui->render();
		SDL_GL_SwapWindow(displayWindow);
	}
    SDL_Quit();
    return 0;
}