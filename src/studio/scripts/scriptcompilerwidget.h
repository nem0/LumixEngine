#pragma once

#include <QDockWidget>
#include "core/string.h"

namespace Ui 
{
	class ScriptCompilerWidget;
}

namespace Lumix
{
	struct Component;
	class Path;
	class Universe;
	class WorldEditor;
}

class ScriptCompiler;

class ScriptCompilerWidget : public QDockWidget
{
	Q_OBJECT

public:
	explicit ScriptCompilerWidget(QWidget* parent = NULL);
	~ScriptCompilerWidget();
	ScriptCompiler* getCompiler() const { return m_compiler; }
	void setWorldEditor(Lumix::WorldEditor& editor);
	void setUniverse(Lumix::Universe* universe);

private:
	void onUniverseCreated();
	void onUniverseDestroyed();
	void onUniverseLoaded();
	void onComponentCreated(const Lumix::Component&);
	void onComponentDestroyed(const Lumix::Component&);
	void onScriptRenamed(const Lumix::Path& old_path, const Lumix::Path& new_path);

private slots:
	void on_compileAllButton_clicked();
	void on_openInVSButton_clicked();

private:
	Ui::ScriptCompilerWidget* m_ui;
	ScriptCompiler* m_compiler;
	QString m_base_path;
	Lumix::WorldEditor* m_editor;
	Lumix::Universe* m_universe;
};

