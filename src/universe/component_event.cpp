#include "component_event.h"
#include "core/crc32.h"


namespace Lumix
{


const Event::Type ComponentEvent::type = crc32("component");


} // !namespace Lumix
