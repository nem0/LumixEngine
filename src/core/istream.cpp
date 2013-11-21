#include "istream.h"
#include <cstring>


namespace Lux
{


void IStream::write(const char* str)
{
	write(str, strlen(str));
}


}