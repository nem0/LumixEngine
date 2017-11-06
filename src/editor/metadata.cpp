#include "metadata.h"
#include "engine/fs/os_file.h"
#include <cstdio>


namespace Lumix
{


static const char* METADATA_FILENAME = "metadata.bin";
static const u32 METADATA_MAGIC = 0x4D455441; // 'META'


enum class MetadataVersion : i32
{
	FIRST,

	LATEST
};


Metadata::Metadata(IAllocator& allocator)
	: m_allocator(allocator)
	, m_data(allocator)
{
}


bool Metadata::load()
{
	FS::OsFile file;
	if (!file.open(METADATA_FILENAME, FS::Mode::OPEN_AND_READ)) return false;

	m_data.clear();
	int count;
	u32 magic;
	file.read(&magic, sizeof(magic));
	if (magic != METADATA_MAGIC)
	{
		file.close();
		return false;
	}
	i32 version;
	file.read(&version, sizeof(version));
	if (version > (int)MetadataVersion::LATEST)
	{
		file.close();
		return false;
	}
	file.read(&count, sizeof(count));
	for (int i = 0; i < count; ++i)
	{
		u32 key;
		file.read(&key, sizeof(key));
		auto& file_data = m_data.emplace(key, m_allocator);

		int inner_count;
		file.read(&inner_count, sizeof(inner_count));
		for (int j = 0; j < inner_count; ++j)
		{
			file.read(&key, sizeof(key));
			DataItem& value = file_data.insert(key);
			file.read(&value.m_type, sizeof(value.m_type));
			switch (value.m_type)
			{
				case DataItem::INT: file.read(&value.m_int, sizeof(value.m_int)); break;
				case DataItem::STRING:
				{
					int len;
					file.read(&len, sizeof(len));
					file.read(value.m_string, len);
				}
				break;
				case DataItem::RAW_MEMORY:
				{
					int len;
					file.read(&len, sizeof(len));
					value.m_raw.memory = m_allocator.allocate(len);
					file.read(value.m_raw.memory, len);
				}
				break;
				default: ASSERT(false); break;
			}
		}
	}

	file.close();
	return true;
}


bool Metadata::save()
{
	FS::OsFile file;
	if (!file.open(METADATA_FILENAME, FS::Mode::CREATE_AND_WRITE)) return false;

	file.write(&METADATA_MAGIC, sizeof(METADATA_MAGIC));
	i32 version = (int)MetadataVersion::LATEST;
	file.write(&version, sizeof(version));
	int count = m_data.size();
	file.write(&count, sizeof(count));
	for (int i = 0; i < m_data.size(); ++i)
	{
		auto key = m_data.getKey(i);
		file.write(&key, sizeof(key));
		auto& file_data = m_data.at(i);
		count = file_data.size();
		file.write(&count, sizeof(count));
		for (int j = 0; j < file_data.size(); ++j)
		{
			key = file_data.getKey(j);
			file.write(&key, sizeof(key));
			auto& value = file_data.at(j);
			file.write(&value.m_type, sizeof(value.m_type));
			switch (value.m_type)
			{
				case DataItem::INT: file.write(&value.m_int, sizeof(value.m_int)); break;
				case DataItem::STRING:
				{
					int len = stringLength(value.m_string);
					file.write(&len, sizeof(len));
					file.write(value.m_string, len);
				}
				break;
				case DataItem::RAW_MEMORY:
				{
					int len = (int)value.m_raw.size;
					file.write(&len, sizeof(len));
					file.write(value.m_raw.memory, len);
				}
				break;
				default: ASSERT(false); break;
			}
		}
	}

	file.close();
	return true;
}


Metadata::~Metadata()
{
	for (int i = 0; i < m_data.size(); ++i)
	{
		auto& x = m_data.at(i);
		for (int j = 0; j < x.size(); ++j)
		{
			auto& data = x.at(j);
			if (data.m_type == DataItem::RAW_MEMORY)
			{
				m_allocator.deallocate(data.m_raw.memory);
			}
		}
	}
}


Metadata::DataItem* Metadata::getOrCreateData(u32 file, u32 key)
{
	int index = m_data.find(file);
	if (index < 0)
	{
		index = m_data.insert(file, AssociativeArray<u32, DataItem>(m_allocator));
	}

	auto& file_data = m_data.at(index);
	index = file_data.find(key);
	if (index >= 0) return &file_data.at(index);
	return &file_data.insert(key);
}


const Metadata::DataItem* Metadata::getData(u32 file, u32 key) const
{
	int index = m_data.find(file);
	if (index < 0) return nullptr;

	auto& file_data = m_data.at(index);
	index = file_data.find(key);
	if (index < 0) return nullptr;

	return &file_data.at(index);
}


const void* Metadata::getRawMemory(u32 file, u32 key) const
{
	const auto* data = getData(file, key);
	if (!data || data->m_type != DataItem::RAW_MEMORY) return nullptr;
	return data->m_raw.memory;
}


size_t Metadata::getRawMemorySize(u32 file, u32 key) const
{
	const auto* data = getData(file, key);
	if (!data || data->m_type != DataItem::RAW_MEMORY) return 0;
	return data->m_raw.size;
}



bool Metadata::setRawMemory(u32 file, u32 key, const void* mem, size_t size)
{
	auto* data = getOrCreateData(file, key);
	if (!data) return false;

	data->m_type = DataItem::RAW_MEMORY;
	m_allocator.deallocate(data->m_raw.memory);
	data->m_raw.memory = m_allocator.allocate(size);
	copyMemory(data->m_raw.memory, mem, size);
	data->m_raw.size = size;

	return true;
}


bool Metadata::setInt(u32 file, u32 key, int value)
{
	auto* data = getOrCreateData(file, key);
	if (!data) return false;

	data->m_type = DataItem::INT;
	data->m_int = value;

	return true;
}


bool Metadata::setString(u32 file, u32 key, const char* value)
{
	auto* data = getOrCreateData(file, key);
	if (!data) return false;

	data->m_type = DataItem::STRING;
	copyString(data->m_string, value);

	return true;
}


bool Metadata::hasKey(u32 file, u32 key) const
{
	return getData(file, key) != nullptr;
}


int Metadata::getInt(u32 file, u32 key) const
{
	const auto* data = getData(file, key);
	if (!data || data->m_type != DataItem::INT) return 0;
	return data->m_int;
}


bool Metadata::getString(u32 file, u32 key, char* out, int max_size) const
{
	const auto* data = getData(file, key);
	if (!data || data->m_type != DataItem::STRING) return false;
	copyString(out, max_size, data->m_string);

	return true;
}


} // namespace Lumix