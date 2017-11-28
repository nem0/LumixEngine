#pragma once


#include "engine/associative_array.h"


namespace Lumix
{


class LUMIX_EDITOR_API Metadata
{
public:
	explicit Metadata(IAllocator& allocator);
	~Metadata();

	bool load();
	bool save();

	bool setRawMemory(u32 file, u32 key, const void* data, size_t size);
	bool setInt(u32 file, u32 key, int value);
	bool setString(u32 file, u32 key, const char* value);

	int getInt(u32 file, u32 key) const;
	bool getString(u32 file, u32 key, char* out, int max_size) const;
	const void* getRawMemory(u32 file, u32 key) const;
	size_t getRawMemorySize(u32 file, u32 key) const;

	bool hasKey(u32 file, u32 key) const;

private:
	struct DataItem
	{
		enum Type
		{
			UNINITIALIZED,
			INT,
			STRING,
			RAW_MEMORY
		} m_type = UNINITIALIZED;

		union
		{
			int m_int;
			char m_string[MAX_PATH_LENGTH];
			
			struct
			{
				void* memory;
				size_t size;
			} m_raw;
		};
	};

	const DataItem* getData(u32 file, u32 key) const;
	DataItem* getOrCreateData(u32 file, u32 key);

	AssociativeArray<u32, AssociativeArray<u32, DataItem> > m_data;
	IAllocator& m_allocator;
};


} // namespace Lumix