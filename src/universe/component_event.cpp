#include "component_event.h"
#include "core/crc32.h"


namespace Lux
{


const Event::Type ComponentEvent::type = crc32("component");


} // !namespace Lux