#include "scriptcompilerwidget.h"
#include "ui_scriptcompilerwidget.h"
#include <qfilesystemmodel.h>
#include "scriptcompiler.h"


ScriptCompilerWidget::ScriptCompilerWidget(QWidget* parent) :
	QDockWidget(parent),
	m_ui(new Ui::ScriptCompilerWidget)
{
	m_ui->setupUi(this);
	m_model = new QFileSystemModel;
	m_model->setRootPath(QDir::currentPath() + "/scripts/");
	m_base_path = QDir::currentPath().toLatin1().data();
	QStringList filters;
	filters << "*.cpp";
	m_model->setNameFilterDisables(false);
	m_model->setNameFilters(filters);
	m_ui->scriptListView->setModel(m_model);
	m_ui->scriptListView->setRootIndex(m_model->index(QDir::currentPath() + "/scripts/"));
	m_compiler = new ScriptCompiler;
	connect(m_compiler, SIGNAL(messageLogged(const QString&)), this, SLOT(logMessage(const QString&)));
	m_compiler->setBasePath(m_base_path.c_str());
	m_compiler->compileAll();
}

ScriptCompilerWidget::~ScriptCompilerWidget()
{
	delete m_compiler;
	delete m_ui;
	delete m_model;
}

void ScriptCompilerWidget::logMessage(const QString& message)
{
	m_ui->logView->addItem(message);
}

void ScriptCompilerWidget::on_scriptListView_clicked(const QModelIndex &index)
{
	QString path = m_model->filePath(index);
	const char* c = m_compiler->getLog(path.toLatin1().data());
	m_ui->compilerOutputView->setText(c);
}

void ScriptCompilerWidget::on_compileAllButton_clicked()
{
	m_compiler->compileAll();
}
