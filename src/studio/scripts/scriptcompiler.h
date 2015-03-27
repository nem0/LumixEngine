#pragma once


#include <QMap>
#include <QObject>
#include <QString>
#include <QVector>
#include "core/array.h"
#include "core/blob.h"
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

	class Module;

public:
	explicit ScriptCompiler(QObject* parent = NULL);
	void compileModule(const QString& module_name);
	void compileAllModules();
	Status getStatus(const QString& module_name);
	QString getLog(const QString& module_name);
	void setWorldEditor(Lumix::WorldEditor& editor);
	void addScript(const QString& module_name, const QString& path);
	void onScriptRenamed(const Lumix::Path& old_path, const Lumix::Path& new_path);
	void removeScript(const Lumix::Path& path);
	void destroyModule(const QString& module_name);
	void setModuleOutputPath(const QString& module_name, const QString& path);
	void setSourcesPath(const QString& path) { m_sources_path = path; }
	void onScriptChanged(const QString& path);

signals:
	void compiled(const QString&);

private:
	struct ProcessInfo
	{
		class QProcess* m_process;
		Lumix::Path m_path;
	};

private:
	void emitCompiled(const QString& module_name) { emit compiled(module_name); }
	void onGameModeToggled(bool was_game_mode);

private:
	Lumix::WorldEditor* m_editor;
	QMap<QString, Module> m_modules;
	QString m_sources_path;
};


class ScriptCompiler::Module
{
	public:
		Module(const QString& name) : m_module_name(name) {}
		Module() { Q_ASSERT(false); }

	public:
		QVector<QString> m_scripts;
		Status m_status;
		QString m_log;
		QString m_module_name;
		QString m_output_path;
};
