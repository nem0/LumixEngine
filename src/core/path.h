#pragma once

namespace Lux
{
	class LUX_CORE_API Path
	{
	public:
		LUX_FORCE_INLINE Path()
			: m_path(NULL)
			, m_id(0)
		{ }

		Path(const Path& rhs);
		Path(const char* path);
		Path(uint32_t id, const char* path);

		~Path();

		operator const char*() const { return m_path; }
		operator uint32_t() const { return m_id; }

		bool operator == (const Path& rhs) const { return m_id == rhs.m_id; }
		bool operator == (const char* rhs) const { Path path(rhs); return m_id == path.m_id; }
		bool operator == (uint32_t rhs) const { return m_id == rhs; }

		bool isValid() { return NULL != m_path; }

	private:
		char*		m_path;
		uint32_t	m_id;
	};
}