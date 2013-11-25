#include "entity_moved_event.h"
#include "core/crc32.h"


namespace Lux
{


const Event::Type EntityMovedEvent::type = crc32("entity_moved");


} // !namespace Lux