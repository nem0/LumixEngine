#pragma once

namespace litehtml
{
	class utf8_to_wchar
	{
		const byte* m_utf8;
		std::wstring m_str;
	public:
		utf8_to_wchar(const char* val);
		operator const wchar_t*() const
		{
			return m_str.c_str();
		}
	private:
		ucode_t getb()
		{
			if (!(*m_utf8)) return 0;
			return *m_utf8++;
		}
		ucode_t get_next_utf8(ucode_t val)
		{
			return (val & 0x3f);
		}
		ucode_t get_char();
	};

	class wchar_to_utf8
	{
		std::string m_str;
	public:
		wchar_to_utf8(const wchar_t* val);
		operator const char*() const
		{
			return m_str.c_str();
		}
	};

#ifdef LITEHTML_UTF8
#define litehtml_from_utf8(str)		str
#define litehtml_to_utf8(str)		str
#define litehtml_from_wchar(str)	wchar_to_utf8(str)
#else
#define litehtml_from_utf8(str)		utf8_to_wchar(str)
#define litehtml_from_wchar(str)	str
#define litehtml_to_utf8(str)		wchar_to_utf8(str)
#endif
}