#pragma once


#include <qmap.h>
#include <qvariant.h>


class Metadata
{
	public:
		bool load(const QString& path);
		bool save(const QString& path);
		QVariant get(const QString& filepath, const QString& key) const;
		void set(const QString& filepath, const QString& key, const QVariant& value);
		bool exists(const QString& filepath, const QString& key) const;

	private:
		QMap<QString, QMap<QString, QVariant> > m_data;
};
