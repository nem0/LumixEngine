#pragma once


#include "core/lux.h"


namespace Lux
{
namespace FS
{

	class FileSystem;

	struct PathString
	{
		int32_t m_references;
		uint32_t m_hash;
		char* m_path;
	};

	class LUX_CORE_API Path LUX_FINAL
	{
		public:
			Path(const char* path, FileSystem& file_system);
			Path(const Path& path);
			~Path();

			const char* getCString() const { return m_path_string->m_path; }
			uint32_t getHash() const { return m_path_string->m_hash; }

		public:
			static const uint32_t MAX_LENGTH = 260;

		private:
			void operator=(const Path&) {}
			void normalize(const char* src, char* dest);

			FileSystem& m_file_system;
			PathString* m_path_string;
	};

} // ~namespace Path
} // ~namspace Lux
