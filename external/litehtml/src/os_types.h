#pragma once

namespace litehtml
{
#if defined( WIN32 ) || defined( WINCE )

#ifndef LITEHTML_UTF8

	typedef std::wstring		tstring;
	typedef wchar_t				tchar_t;
	typedef std::wstringstream	tstringstream;

	#define _t(quote)			L##quote

	#define t_strlen			wcslen
	#define t_strcmp			wcscmp
	#define t_strncmp			wcsncmp
	#define t_strcasecmp		_wcsicmp
	#define t_strncasecmp		_wcsnicmp
	#define t_strtol			wcstol
	#define t_atoi				_wtoi
	#define t_strtod			wcstod
	#define t_itoa(value, buffer, size, radix)	_itow_s(value, buffer, size, radix)
	#define t_strstr			wcsstr
	#define t_tolower			towlower
	#define t_isdigit			iswdigit

#else

	typedef std::string			tstring;
	typedef char				tchar_t;
	typedef std::stringstream	tstringstream;

	#define _t(quote)			quote

	#define t_strlen			strlen
	#define t_strcmp			strcmp
	#define t_strncmp			strncmp
	#define t_strcasecmp		_stricmp
	#define t_strncasecmp		_strnicmp
	#define t_strtol			strtol
	#define t_atoi				atoi
	#define t_strtod			strtod
	#define t_itoa(value, buffer, size, radix)	_itoa_s(value, buffer, size, radix)
	#define t_strstr			strstr
	#define t_tolower			tolower
	#define t_isdigit			isdigit

#endif

	#ifdef _WIN64
		typedef unsigned __int64 uint_ptr;
	#else
		typedef unsigned int	uint_ptr;
	#endif

#else
	#define LITEHTML_UTF8

	typedef std::string			tstring;
	typedef char				tchar_t;
	typedef void*				uint_ptr;
	typedef std::stringstream	tstringstream;

	#define _t(quote)			quote

	#define t_strlen			strlen
	#define t_strcmp			strcmp
	#define t_strncmp			strncmp

	#define t_strcasecmp		strcasecmp
	#define t_strncasecmp		strncasecmp
	#define t_itoa(value, buffer, size, radix)	snprintf(buffer, size, "%d", value)

	#define t_strtol			strtol
	#define t_atoi				atoi
	#define t_strtod			strtod
	#define t_strstr			strstr
	#define t_tolower			tolower
	#define t_isdigit			isdigit

#endif
}
