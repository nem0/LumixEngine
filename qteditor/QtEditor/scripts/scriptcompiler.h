#pragma once


#include <QObject>
#include "core/delegate_list.h"
#include "core/map.h"
#include "core/array.h"
#include "core/string.h"

class ScriptCompiler : public QObject
{
	Q_OBJECT
public:
	typedef Lumix::DelegateList<void (const char*, uint32_t)> CompileCallbacks;
	enum Status
	{
		UNKNOWN,
		NOT_COMPILED,
		SUCCESS,
		FAILURE
	};

public:
	explicit ScriptCompiler(QObject* parent = NULL);
	void compile(const char path[]);
	CompileCallbacks& onCompile() { return m_delegates; }
	void compileAll();
	void setBasePath(const char* path) { m_base_path = path; }
	void checkFinished();
	bool isEmpty() const { return m_processes.empty(); }
	Status getStatus(const char* path);
	const char* getLog(const char* path);

signals:
	void messageLogged(const QString& message);

public slots:
	void compilerFinish(int exitCode);

private:
	struct ProcessInfo
	{
		class QProcess* m_process;
		Lumix::string m_path;
	};

private:
	CompileCallbacks m_delegates;
	Lumix::string m_base_path;
	Lumix::Array<ProcessInfo> m_processes;
	Lumix::Map<uint32_t, Status> m_status;
	Lumix::Map<uint32_t, Lumix::string> m_log;

};
