#pragma once


#include <QObject>
#include <QMap>
#include "core/array.h"
#include "core/delegate_list.h"
#include "core/map.h"
#include "core/path.h"
#include "core/string.h"


class ScriptCompiler : public QObject
{
	Q_OBJECT
public:
	typedef Lumix::DelegateList<void (const Lumix::Path&, uint32_t)> CompileCallbacks;
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
	CompileCallbacks& onCompile() { return m_delegates; }
	void compileAll();
	void setBasePath(const Lumix::Path& path) { m_base_path = path; }
	void checkFinished();
	bool isEmpty() const { return m_processes.empty(); }
	Status getStatus(const Lumix::Path& path);
	const char* getLog(const Lumix::Path& path);

signals:
	void messageLogged(const QString& message);

public slots:
	void compilerFinish(int exitCode);

private:
	struct ProcessInfo
	{
		class QProcess* m_process;
		Lumix::Path m_path;
	};

private:
	CompileCallbacks m_delegates;
	Lumix::Path m_base_path;
	Lumix::Array<ProcessInfo> m_processes;
	QMap<uint32_t, Status> m_status;
	QMap<uint32_t, Lumix::string> m_log;

};
