#include "file_utils.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


namespace Lux
{


string getCwd()
{
	TCHAR path[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, path);
	char tmp[MAX_PATH];
	TCHAR* from = path;
	char* to = tmp;
	while(*from)
	{
		*to = (char)*from;
		++to;
		++from;
	}
	*to = 0;
	return tmp;
}


} // !namespace Lux