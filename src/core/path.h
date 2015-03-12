#pragma once

#include "core/associative_array.h"
#include "core/MT/spin_mutex.h"
#include "core/string.h"
#include <cstring>


namespace Lumix
{

	class InputBlob;
	class OutputBlob;

	class PathInternal
	{
	public:
		char				m_path[LUMIX_MAX_PATH];
		uint32_t			m_id;
		volatile int32_t	m_ref_count;
	};


	class LUMIX_CORE_API PathManager
	{
		friend class Path;
		public:
			PathManager();
			~PathManager();

			void serialize(OutputBlob& serializer);
			void deserialize(InputBlob& serializer);

		private:
			PathInternal* getPath(uint32_t hash, const char* path);
			PathInternal* getPath(uint32_t hash);
			PathInternal* getPathMultithreadUnsafe(uint32_t hash, const char* path);
			void incrementRefCount(PathInternal* path);
			void decrementRefCount(PathInternal* path);

		private:
			DefaultAllocator m_allocator;
			AssociativeArray<uint32_t, PathInternal*> m_paths;
			MT::SpinMutex m_mutex;
	};


	extern PathManager LUMIX_CORE_API g_path_manager;


	class LUMIX_CORE_API Path
	{
	public:
		Path();
		Path(const Path& rhs);
		explicit Path(uint32_t hash);
		explicit Path(const char* path);
		void operator =(const Path& rhs);
		void operator =(const char* rhs);
		void operator =(const string& rhs);
		bool operator ==(const Path& rhs) const { return m_data->m_id == rhs.m_data->m_id; }

		~Path();
		
		operator const char*() const { return m_data->m_path; }
		operator uint32_t() const { return m_data->m_id; }
		uint32_t getHash() const { return m_data->m_id; }

		const char* c_str() const { return m_data->m_path; }
		size_t length() const { return strlen(m_data->m_path); }
		bool isValid() const { return m_data->m_path[0] != '\0'; }
		
	private:
		PathInternal* m_data;
	};

} // namespace Lumix