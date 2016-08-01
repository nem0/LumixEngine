#pragma once


#include "engine/lumix.h"


namespace Lumix
{


class CommandLineParser
{
public:
	CommandLineParser(const char* cmd_line)
		: m_cmd_line(cmd_line)
		, m_current(nullptr)
	{
		ASSERT(m_cmd_line != nullptr);
	}


	bool next()
	{
		if (!m_current)
		{
			m_current = m_cmd_line;
			skipWhitespaces();
			return *m_current != 0;
		}

		while (*m_current && !isWhitespace(*m_current))
		{
			if (*m_current == '"')
			{
				skipString();
			}
			else
			{
				++m_current;
			}
		}
		skipWhitespaces();
		return *m_current != 0;
	}


	void getCurrent(char* output, int max_size)
	{
		ASSERT(*m_current);
		ASSERT(max_size > 0);
		const char* rhs = m_current;
		char* end = output + max_size - 1;
		char* lhs = output;
		if (*m_current == '"')
		{
			++rhs;
			while (*rhs && *rhs != '"' && lhs != end)
			{
				*lhs = *rhs;
				++lhs;
				++rhs;
			}
			*lhs = 0;
			return;
		}
		
		while (*rhs && !isWhitespace(*rhs) && lhs != end)
		{
			*lhs = *rhs;
			++lhs;
			++rhs;
		}
		*lhs = 0;
	}


	bool currentEquals(const char* value) 
	{
		ASSERT(*m_current);

		const char* lhs = m_current;
		const char* rhs = value;
		while (*lhs && *rhs && *lhs == *rhs)
		{
			++lhs;
			++rhs;
		}

		return *rhs == 0 && (*lhs == 0 || isWhitespace(*lhs));
	}


private:
	static bool isWhitespace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }


	void skipWhitespaces()
	{
		while (*m_current && isWhitespace(*m_current))
		{
			++m_current;
		}
	}


	void skipString()
	{
		ASSERT(*m_current == '"');
		++m_current;
		while (*m_current && *m_current != '"')
		{
			++m_current;
		}
		if (*m_current) ++m_current;
	}


private:
	const char* m_current;
	const char* m_cmd_line;
};


} // namespace Lumix
