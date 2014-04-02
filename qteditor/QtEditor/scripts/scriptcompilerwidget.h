#pragma once

#include <QDockWidget>
#include "core/string.h"

namespace Ui {
class ScriptCompilerWidget;
}

class ScriptCompilerWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit ScriptCompilerWidget(QWidget *parent = 0);
    ~ScriptCompilerWidget();

private slots:
    void on_scriptListView_clicked(const QModelIndex &index);
	void logMessage(const QString& message);

private:
    Ui::ScriptCompilerWidget *m_ui;
	class QFileSystemModel* m_model;
	class ScriptCompiler* m_compiler;
	Lux::string m_base_path;
};

