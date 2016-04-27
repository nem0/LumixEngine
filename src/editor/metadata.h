#pragma once


#include "engine/core/associative_array.h"
#include "engine/core/string.h"


class LUMIX_EDITOR_API Metadata
{
public:
	explicit Metadata(Lumix::IAllocator& allocator);
	~Metadata();

	bool load();
	bool save();

	bool setRawMemory(Lumix::uint32 file, Lumix::uint32 key, const void* data, size_t size);
	bool setInt(Lumix::uint32 file, Lumix::uint32 key, int value);
	bool setString(Lumix::uint32 file, Lumix::uint32 key, const char* value);

	int getInt(Lumix::uint32 file, Lumix::uint32 key) const;
	bool getString(Lumix::uint32 file, Lumix::uint32 key, char* out, int max_size) const;
	const void* getRawMemory(Lumix::uint32 file, Lumix::uint32 key) const;
	size_t getRawMemorySize(Lumix::uint32 file, Lumix::uint32 key) const;

	bool hasKey(Lumix::uint32 file, Lumix::uint32 key) const;

private:
	struct DataItem
	{
		enum Type
		{
			INT,
			STRING,
			RAW_MEMORY
		};

		Type m_type;

		union
		{
			int m_int;
			char m_string[Lumix::MAX_PATH_LENGTH];
			
			struct Raw
			{
				void* memory;
				size_t size;
			} m_raw;
		};
	};

	const DataItem* getData(Lumix::uint32 file, Lumix::uint32 key) const;
	DataItem* getOrCreateData(Lumix::uint32 file, Lumix::uint32 key);

	Lumix::AssociativeArray<Lumix::uint32, Lumix::AssociativeArray<Lumix::uint32, DataItem> > m_data;
	Lumix::IAllocator& m_allocator;
};
