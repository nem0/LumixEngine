#include "editor/studio_app.h"


using namespace Lumix;


LUMIX_STUDIO_ENTRY(gui)
{
	app.registerComponent("gui_image", "GUI/Image");
	app.registerComponent("gui_rect", "GUI/Rect");
	app.registerComponent("gui_text", "GUI/Text");
}
