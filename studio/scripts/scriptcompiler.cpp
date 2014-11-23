#include "scriptcompiler.h"
#include <qdiriterator.h>
#include <qprocess.h>
#include <qtextstream.h>
#include "core/crc32.h"
#include "core/log.h"
#include "core/path.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "script/script_system.h"

ScriptCompiler::ScriptCompiler(QObject* parent) 
	: QObject(parent)
	, m_editor(NULL)
{
}


void ScriptCompiler::compileAll()
{
	if (m_editor)
	{
		Lumix::ScriptScene* scene = static_cast<Lumix::ScriptScene*>(m_editor->getEngine().getScene(crc32("script")));
		scene->beforeScriptReload();
	}
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


void ScriptCompiler::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_editor = &editor;
}

void ScriptCompiler::compile(const Lumix::Path& path)
{
	ProcessInfo process;
	Lumix::Path rel_path;
	if(strncmp(path.c_str(), m_base_path.c_str(), m_base_path.length()) == 0)
	{
		rel_path = path.c_str() + m_base_path.length() + 1;
	}
	else
	{
		rel_path = path;
	}
	process.m_path = rel_path;
	process.m_process = new QProcess();
	m_processes.push_back(process);
	QStringList list;
	char cmd_line[255];
	sprintf(cmd_line, "%s\\scripts\\compile.bat %s\\%s", m_base_path.c_str(), m_base_path.c_str(), rel_path.c_str());

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
			if(exitCode == 0)
			{
				msg.sprintf("Script %s compiled successfully", m_processes[i].m_path.c_str());
			}
			else
			{
				msg.sprintf("Script %s failed to compile", m_processes[i].m_path.c_str());
			}
			emit compiled(m_processes[i].m_path.c_str(), exitCode);
			emit messageLogged(msg);
			m_processes.removeAt(i);
			break;
		}
	}
	if (m_processes.empty())
	{
		Lumix::ScriptScene* scene = static_cast<Lumix::ScriptScene*>(m_editor->getEngine().getScene(crc32("script")));
		scene->afterScriptReload();
	}
}


ScriptCompiler::Status ScriptCompiler::getStatus(const Lumix::Path& path)
{
	uint32_t hash;
	if(strncmp(path.c_str(), m_base_path.c_str(), m_base_path.length()) == 0)
	{
		hash = crc32(path.c_str() + m_base_path.length() + 1);
	}
	else
	{
		hash = crc32(path.c_str());
	}

	QMap<uint32_t, Status>::iterator iter = m_status.find(hash);
	return iter == m_status.end() ? UNKNOWN : iter.value();
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
			m_processes.remove(i);
		}
	}
	if (m_processes.empty())
	{
		Lumix::ScriptScene* scene = static_cast<Lumix::ScriptScene*>(m_editor->getEngine().getScene(crc32("script")));
		scene->afterScriptReload();
	}
}


QString ScriptCompiler::getLog(const Lumix::Path& path)
{
	uint32_t hash;
	if(strncmp(path.c_str(), m_base_path.c_str(), m_base_path.length()) == 0)
	{
		hash = crc32(path.c_str() + m_base_path.length() + 1);
	}
	else
	{
		hash = crc32(path.c_str());
	}
	return m_log[hash];
}
