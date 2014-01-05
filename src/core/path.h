#pragma once


#include "core/lux.h"


namespace Lux
{
namespace FS
{

	class FileSystem;
	class PathManager;

	struct PathString
	{
		int32_t m_references;
		uint32_t m_hash;
		char* m_path;
		PathManager* m_path_manager;
	};

	class LUX_CORE_API Path LUX_FINAL
	{
		public:
			Path();
			Path(const char* path, FileSystem& file_system);
			Path(const Path& path);
			~Path();
			void operator=(const Path& path);
			bool isValid() const { return m_path_string != NULL; }
			const char* getCString() const { return m_path_string->m_path; }
			uint32_t getHash() const { return m_path_string->m_hash; }
			bool operator ==(const Path& rhs) const { return m_path_string == rhs.m_path_string; }

		public:
			static const uint32_t MAX_LENGTH = 260;

		private:
			void normalize(const char* src, char* dest);

			PathString* m_path_string;
	};

} // ~namespace Path
} // ~namspace Lux
