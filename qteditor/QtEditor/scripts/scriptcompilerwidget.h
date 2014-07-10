#pragma once

#include <QDockWidget>
#include "core/string.h"

namespace Ui 
{
	class ScriptCompilerWidget;
}

class ScriptCompiler;

class ScriptCompilerWidget : public QDockWidget
{
	Q_OBJECT

public:
	explicit ScriptCompilerWidget(QWidget* parent = NULL);
	~ScriptCompilerWidget();
	ScriptCompiler* getCompiler() const { return m_compiler; }

private slots:
	void on_scriptListView_clicked(const QModelIndex &index);
	void logMessage(const QString& message);

	void on_compileAllButton_clicked();

private:
	Ui::ScriptCompilerWidget* m_ui;
	class QFileSystemModel* m_model;
	ScriptCompiler* m_compiler;
	Lumix::string m_base_path;
};

