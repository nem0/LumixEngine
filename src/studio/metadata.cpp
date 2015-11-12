#include "metadata.h"
#include <cstdio>


static const char* METADATA_FILENAME = "metadata.bin";


Metadata::Metadata(Lumix::IAllocator& allocator)
	: m_allocator(allocator)
	, m_data(allocator)
{
}


bool Metadata::load()
{
	FILE* fp = fopen(METADATA_FILENAME, "rb");
	if (!fp) return false;

	m_data.clear();
	int count;
	fread(&count, sizeof(count), 1, fp);
	for (int i = 0; i < count; ++i)
	{
		Lumix::uint32 key;
		fread(&key, sizeof(key), 1, fp);
		int idx = m_data.insert(key, Lumix::AssociativeArray<Lumix::uint32, DataItem>(m_allocator));

		auto& file_data = m_data.at(idx);
		int inner_count;
		fread(&inner_count, sizeof(inner_count), 1, fp);
		for (int j = 0; j < inner_count; ++j)
		{
			fread(&key, sizeof(key), 1, fp);
			int idx = file_data.insert(key, DataItem());
			auto& value = file_data.at(idx);
			fread(&value.m_type, sizeof(value.m_type), 1, fp);
			switch (value.m_type)
			{
				case DataItem::INT: fread(&value.m_int, sizeof(value.m_int), 1, fp); break;
				case DataItem::STRING:
				{
					int len;
					fread(&len, sizeof(len), 1, fp);
					fread(value.m_string, len, 1, fp);
				}
				break;
				default: ASSERT(false); break;
			}
		}
	}

	fclose(fp);
	return true;
}


bool Metadata::save()
{
	FILE* fp = fopen(METADATA_FILENAME, "wb");
	if (!fp) return false;

	int count = m_data.size();
	fwrite(&count, sizeof(count), 1, fp);
	for (int i = 0; i < m_data.size(); ++i)
	{
		auto key = m_data.getKey(i);
		fwrite(&key, sizeof(key), 1, fp);
		auto& file_data = m_data.at(i);
		count = file_data.size();
		fwrite(&count, sizeof(count), 1, fp);
		for (int j = 0; j < file_data.size(); ++j)
		{
			key = file_data.getKey(j);
			fwrite(&key, sizeof(key), 1, fp);
			auto& value = file_data.at(j);
			fwrite(&value.m_type, sizeof(value.m_type), 1, fp);
			switch (value.m_type)
			{
				case DataItem::INT: fwrite(&value.m_int, sizeof(value.m_int), 1, fp); break;
				case DataItem::STRING:
				{
					int len = Lumix::stringLength(value.m_string);
					fwrite(&len, sizeof(len), 1, fp);
					fwrite(value.m_string, len, 1, fp);
				}
				break;
				default: ASSERT(false); break;
			}
		}
	}

	fclose(fp);
	return true;
}


Metadata::DataItem* Metadata::getOrCreateData(Lumix::uint32 file, Lumix::uint32 key)
{
	int index = m_data.find(file);
	if (index < 0)
	{
		index = m_data.insert(file, Lumix::AssociativeArray<Lumix::uint32, DataItem>(m_allocator));
	}

	auto& file_data = m_data.at(index);
	index = file_data.find(key);
	if (index < 0)
	{
		index = file_data.insert(key, DataItem());
	}

	return &file_data.at(index);
}


const Metadata::DataItem* Metadata::getData(Lumix::uint32 file, Lumix::uint32 key) const
{
	int index = m_data.find(file);
	if (index < 0) return nullptr;

	auto& file_data = m_data.at(index);
	index = file_data.find(key);
	if (index < 0) return nullptr;

	return &file_data.at(index);
}


bool Metadata::setInt(Lumix::uint32 file, Lumix::uint32 key, int value)
{
	auto* data = getOrCreateData(file, key);
	if (!data) return false;

	data->m_type = DataItem::INT;
	data->m_int = value;

	return true;
}


bool Metadata::setString(Lumix::uint32 file, Lumix::uint32 key, const char* value)
{
	auto* data = getOrCreateData(file, key);
	if (!data) return false;

	data->m_type = DataItem::STRING;
	Lumix::copyString(data->m_string, value);

	return true;
}


bool Metadata::hasKey(Lumix::uint32 file, Lumix::uint32 key) const
{
	return getData(file, key) != nullptr;
}


int Metadata::getInt(Lumix::uint32 file, Lumix::uint32 key) const
{
	const auto* data = getData(file, key);
	if (!data || data->m_type != DataItem::INT) return 0;
	return data->m_int;
}


bool Metadata::getString(Lumix::uint32 file, Lumix::uint32 key, char* out, int max_size) const
{
	const auto* data = getData(file, key);
	if (!data || data->m_type != DataItem::STRING) return false;
	Lumix::copyString(out, max_size, data->m_string);

	return true;
}
