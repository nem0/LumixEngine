#include "scriptcompilerwidget.h"
#include "ui_scriptcompilerwidget.h"
#include <qfilesystemmodel.h>
#include "scriptcompiler.h"


ScriptCompilerWidget::ScriptCompilerWidget(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::ScriptCompilerWidget)
{
    ui->setupUi(this);
	m_model = new QFileSystemModel;
	m_model->setRootPath(QDir::currentPath() + "/scripts/");
	m_base_path = QDir::currentPath().toLatin1().data();
	QStringList filters;
	filters << "*.cpp";
	m_model->setNameFilterDisables(false);
	m_model->setNameFilters(filters);
	ui->scriptListView->setModel(m_model);
	ui->scriptListView->setRootIndex(m_model->index(QDir::currentPath() + "/scripts/"));
	m_compiler = new ScriptCompiler;
	m_compiler->setBasePath(m_base_path.c_str());
	m_compiler->compileAll();
}

ScriptCompilerWidget::~ScriptCompilerWidget()
{
    delete ui;
	delete m_model;
}

void ScriptCompilerWidget::on_scriptListView_clicked(const QModelIndex &index)
{
	QString path = m_model->filePath(index);
	const char* c = m_compiler->getLog(path.toLatin1().data());
	ui->compilerOutputView->setText(c);
}
