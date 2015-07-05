#pragma once


#include <qdockwidget.h>
#include <qmap.h>


namespace Lumix
{
	class WorldEditor;
}


namespace Ui
{
	class ShaderCompiler;
}


class FileSystemWatcher;


class ShaderCompiler : public QDockWidget
{
	public:
		ShaderCompiler();
		~ShaderCompiler();

		void setWorldEditor(Lumix::WorldEditor& editor) { m_editor = &editor; }

	private:
		void compileAll();
		void onFileChanged(const char* path);
		void parseDependencies();
		void compile(const QString& path);

	private:
		int m_to_compile;
		bool m_is_compiling;
		Ui::ShaderCompiler* m_ui;
		Lumix::WorldEditor* m_editor;
		FileSystemWatcher* m_watcher;
		QMap<QString, QVector<QString> > m_dependencies;
};
