#pragma once


#include "core/associative_array.h"
#include "core/string.h"
#include <cstdio>


class Metadata
{
public:
	explicit Metadata(Lumix::IAllocator& allocator);

	bool load();
	bool save();
	bool setInt(Lumix::uint32 file, Lumix::uint32 key, int value);
	bool setString(Lumix::uint32 file, Lumix::uint32 key, const char* value);
	int getInt(Lumix::uint32 file, Lumix::uint32 key) const;
	bool getString(Lumix::uint32 file, Lumix::uint32 key, char* out, int max_size) const;
	bool hasKey(Lumix::uint32 file, Lumix::uint32 key) const;

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

	const DataItem* getData(Lumix::uint32 file, Lumix::uint32 key) const;
	DataItem* getOrCreateData(Lumix::uint32 file, Lumix::uint32 key);

	Lumix::AssociativeArray<Lumix::uint32, Lumix::AssociativeArray<Lumix::uint32, DataItem> > m_data;
	Lumix::IAllocator& m_allocator;
};
