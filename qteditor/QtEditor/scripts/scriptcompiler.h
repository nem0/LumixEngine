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
	typedef Lux::DelegateList<void (const char*, uint32_t)> CompileCallbacks;
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
		Lux::string m_path;
	};

private:
	CompileCallbacks m_delegates;
	Lux::string m_base_path;
	Lux::Array<ProcessInfo> m_processes;
	Lux::Map<uint32_t, Status> m_status;
	Lux::Map<uint32_t, Lux::string> m_log;

};
