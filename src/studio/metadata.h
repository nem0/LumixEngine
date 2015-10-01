#pragma once


#include "core/associative_array.h"
#include "core/string.h"
#include <cstdio>


class Metadata
{
public:
	Metadata(Lumix::IAllocator& allocator);

	bool load();
	bool save();
	bool setInt(uint32_t file, uint32_t key, int value);
	bool setString(uint32_t file, uint32_t key, const char* value);
	int getInt(uint32_t file, uint32_t key) const;
	bool getString(uint32_t file, uint32_t key, char* out, int max_size) const;
	bool hasKey(uint32_t file, uint32_t key) const;

private:
	struct DataItem
	{
		enum Type
		{
			INT,
			STRING
		};

		Type m_type;

		union
		{
			int m_int;
			char m_string[Lumix::MAX_PATH_LENGTH];
		};
	};

	const DataItem* getData(uint32_t file, uint32_t key) const;
	DataItem* getOrCreateData(uint32_t file, uint32_t key);

	Lumix::AssociativeArray<uint32_t, Lumix::AssociativeArray<uint32_t, DataItem> > m_data;
	Lumix::IAllocator& m_allocator;
};
