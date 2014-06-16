#include "scriptcompiler.h"
#include <qdiriterator.h>
#include <qprocess.h>
#include <qtextstream.h>
#include "core/crc32.h"
#include "core/log.h"


ScriptCompiler::ScriptCompiler(QObject* parent) :
	QObject(parent)
{
}


void ScriptCompiler::compileAll()
{
	QDirIterator dirIt(QString(m_base_path.c_str()) + "/scripts/", QDirIterator::Subdirectories);
	while (dirIt.hasNext()) 
	{
		dirIt.next();
		if (QFileInfo(dirIt.filePath()).isFile())
		{
			if (QFileInfo(dirIt.filePath()).suffix() == "cpp")
			{
				compile(dirIt.filePath().toLatin1().data());
			}
		}
	}
}


void ScriptCompiler::compile(const char path[])
{
	ProcessInfo process;
	process.m_path = path;
	process.m_process = new QProcess();
	m_processes.push(process);
	QStringList list;
	char cmd_line[255];
	sprintf(cmd_line, "scripts\\compile.bat %s", path);
	list.push_back("/C");
	list.push_back(cmd_line);
	connect(process.m_process, SIGNAL(finished(int)), this, SLOT(compilerFinish(int)));
	process.m_process->start("cmd.exe", list);
}


void ScriptCompiler::compilerFinish(int exitCode)
{
	QProcess* process = static_cast<QProcess*>(QObject::sender());
	QString s = process->readAll();;
	process->deleteLater();
	while(process->waitForReadyRead())
	{
		s += process->readAll();
	}
	for(int i = 0; i < m_processes.size(); ++i)
	{
		if(m_processes[i].m_process == process)
		{
			uint32_t hash = crc32(m_processes[i].m_path.c_str());
			m_log[hash] = s.toLatin1().data();
			m_status[hash] = exitCode == 0 ? SUCCESS : FAILURE;
			
			QString msg;
			msg.sprintf("Script %s compiled", m_processes[i].m_path.c_str());
			emit messageLogged(msg);
			break;
		}
	}
}


ScriptCompiler::Status ScriptCompiler::getStatus(const char* path)
{
	Lumix::Map<uint32_t, Status>::iterator iter = m_status.find(crc32(path));
	return iter == m_status.end() ? UNKNOWN : iter.second();
}


void ScriptCompiler::checkFinished()
{
	for(int i = m_processes.size() - 1; i >= 0; --i)
	{
		ProcessInfo& process = m_processes[i];
		if(process.m_process->state() == QProcess::NotRunning)
		{
			uint32_t hash = crc32(process.m_path.c_str());
			m_status[hash] = process.m_process->exitCode() == 0 ? SUCCESS : FAILURE;
			QString s;
			while(process.m_process->waitForReadyRead())
			{
				s += process.m_process->readAll();
			}
			m_log[hash] = s.toLatin1().data();
			delete process.m_process;
			m_processes.eraseFast(i);
		}
	}
}


const char* ScriptCompiler::getLog(const char* path)
{
	return m_log[crc32(path)].c_str();
}
