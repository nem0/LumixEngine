#pragma once

#include <QDockWidget>
#include "core/string.h"

namespace Ui 
{
	class ScriptCompilerWidget;
}

namespace Lumix
{
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

private slots:
	void on_scriptListView_clicked(const QModelIndex &index);
	void logMessage(const QString& message);
	void on_compileAllButton_clicked();

private:
	Ui::ScriptCompilerWidget* m_ui;
	class QFileSystemModel* m_model;
	ScriptCompiler* m_compiler;
	QString m_base_path;
};

