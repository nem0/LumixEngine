#pragma once


#include <QMap>
#include <QObject>
#include <QVector>
#include "core/array.h"
#include "core/delegate_list.h"
#include "core/path.h"
#include "core/string.h"


namespace Lumix { class WorldEditor; }


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
	void compileAll();
	void setBasePath(const Lumix::Path& path) { m_base_path = path; }
	bool isEmpty() const { return m_processes.empty(); }
	Status getStatus() { return m_status; }
	QString getLog() { return m_log; }
	void setWorldEditor(Lumix::WorldEditor& editor);
	void addScript(const Lumix::Path& path);
	void onScriptRenamed(const Lumix::Path& old_path, const Lumix::Path& new_path);
	void removeScript(const Lumix::Path& path);
	void clearScripts();
	void setProjectName(const QString& name) { m_project_name = name; }
	const QString& getProjectName() { return m_project_name; }
	void setSourcesPath(const QString& path) { m_sources_path = path; }

signals:
	void compiled();

private:
	struct ProcessInfo
	{
		class QProcess* m_process;
		Lumix::Path m_path;
	};

private:
	void emitCompiled() { emit compiled(); }
	void onGameModeToggled(bool was_game_mode);

private:
	Lumix::Path m_base_path;
	QVector<ProcessInfo> m_processes;
	QVector<Lumix::Path> m_scripts;
	Lumix::WorldEditor* m_editor;
	Status m_status;
	QString m_log;
	QString m_project_name;
	QString m_sources_path;
};
