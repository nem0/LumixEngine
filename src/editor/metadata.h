#pragma once


#include "engine/associative_array.h"
#include "engine/string.h"


class LUMIX_EDITOR_API Metadata
{
public:
	explicit Metadata(Lumix::IAllocator& allocator);
	~Metadata();

	bool load();
	bool save();

	bool setRawMemory(Lumix::u32 file, Lumix::u32 key, const void* data, size_t size);
	bool setInt(Lumix::u32 file, Lumix::u32 key, int value);
	bool setString(Lumix::u32 file, Lumix::u32 key, const char* value);

	int getInt(Lumix::u32 file, Lumix::u32 key) const;
	bool getString(Lumix::u32 file, Lumix::u32 key, char* out, int max_size) const;
	const void* getRawMemory(Lumix::u32 file, Lumix::u32 key) const;
	size_t getRawMemorySize(Lumix::u32 file, Lumix::u32 key) const;

	bool hasKey(Lumix::u32 file, Lumix::u32 key) const;

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
			
			struct
			{
				void* memory;
				size_t size;
			} m_raw;
		};
	};

	const DataItem* getData(Lumix::u32 file, Lumix::u32 key) const;
	DataItem* getOrCreateData(Lumix::u32 file, Lumix::u32 key);

	Lumix::AssociativeArray<Lumix::u32, Lumix::AssociativeArray<Lumix::u32, DataItem> > m_data;
	Lumix::IAllocator& m_allocator;
};
