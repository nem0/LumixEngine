#pragma once


#include <qdockwidget.h>
#include <qmap.h>
#include "graphics/shader.h"


namespace Lumix
{
class ShaderCombinations;
class WorldEditor;
}


class FileSystemWatcher;
class Notifications;
class QFileInfo;


class ShaderCompiler
{
public:
	ShaderCompiler();
	~ShaderCompiler();

	void setWorldEditor(Lumix::WorldEditor& editor) { m_editor = &editor; }
	void compileAll();
	void setNotifications(Notifications& notifications);

private:
	void reloadShaders();
	void onCompiled(int value);
	void updateNotifications();
	void compileAllPasses(QFileInfo& shader_file_info,
						  bool is_vertex_shader,
						  const int* define_masks,
						  const Lumix::ShaderCombinations& combinations);
	void compilePass(QFileInfo& shader_file_info,
					 bool is_vertex_shader,
					 const char* pass,
					 int define_mask,
					 const Lumix::ShaderCombinations::Defines& all_defines);

	void onFileChanged(const char* path);
	void parseDependencies();
	void compile(const QString& path);
	void makeUpToDate();

private:
	int m_to_compile;
	int m_compiled;
	bool m_is_compiling;
	Lumix::WorldEditor* m_editor;
	FileSystemWatcher* m_watcher;
	Notifications* m_notifications;
	int m_notifications_id;
	QMap<QString, QVector<QString>> m_dependencies;
	QStringList m_to_reload;
};
