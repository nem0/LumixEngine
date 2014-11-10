#pragma once


#include <QMap>
#include <QObject>
#include <QVector>
#include "core/array.h"
#include "core/delegate_list.h"
#include "core/map.h"
#include "core/path.h"
#include "core/string.h"


class ScriptCompiler : public QObject
{
	Q_OBJECT
public:
	enum Status
	{
		UNKNOWN,
		NOT_COMPILED,
		SUCCESS,
		FAILURE
	};

public:
	explicit ScriptCompiler(QObject* parent = NULL);
	void compile(const Lumix::Path& path);
	void compileAll();
	void setBasePath(const Lumix::Path& path) { m_base_path = path; }
	void checkFinished();
	bool isEmpty() const { return m_processes.empty(); }
	Status getStatus(const Lumix::Path& path);
	QString getLog(const Lumix::Path& path);

signals:
	void messageLogged(const QString& message);
	void compiled(const Lumix::Path&, uint32_t);

public slots:
	void compilerFinish(int exitCode);

private:
	struct ProcessInfo
	{
		class QProcess* m_process;
		Lumix::Path m_path;
	};

private:
	Lumix::Path m_base_path;
	QVector<ProcessInfo> m_processes;
	QMap<uint32_t, Status> m_status;
	QMap<uint32_t, QString> m_log;

};
