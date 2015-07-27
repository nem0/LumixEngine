#include "metadata.h"
#include <qdatastream.h>
#include <qfile.h>


bool Metadata::load(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		return false;
	}
	QDataStream str(&file);
	str >> m_data;

	file.close();
	return true;
}


QVariant Metadata::get(const QString& filepath, const QString& key) const
{
	return m_data[filepath][key];
}


void Metadata::set(const QString& filepath, const QString& key, const QVariant& value)
{
	m_data[filepath][key] = value;
}


bool Metadata::exists(const QString& filepath, const QString& key) const
{
	auto iter = m_data.find(filepath);
	if (iter == m_data.end())
	{
		return false;
	}
	return iter.value().find(key) != iter.value().end();
}


bool Metadata::save(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly))
	{
		return false;
	}
	QDataStream str(&file);
	str << m_data;

	file.close();
	return true;
}
