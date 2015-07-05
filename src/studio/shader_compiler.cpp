#include "shader_compiler.h"
#include "ui_shader_compiler.h"
#include "core/log.h"
#include "core/path.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "file_system_watcher.h"
#include "graphics/shader.h"
#include <qdir.h>
#include <qfilesystemmodel.h>
#include <qprocess.h>


ShaderCompiler::ShaderCompiler()
	: QDockWidget(nullptr)
	, m_editor(nullptr)
{
	m_is_compiling = false;
	m_watcher = FileSystemWatcher::create("shaders");
	m_watcher->getCallback().bind<ShaderCompiler, &ShaderCompiler::onFileChanged>(this);
	m_ui = new Ui::ShaderCompiler;
	m_ui->setupUi(this);
	connect(m_ui->compileAllButton, &QPushButton::clicked, this, &ShaderCompiler::compileAll);

	auto* model = new QFileSystemModel(m_ui->shaderList);
	model->setRootPath(QDir::currentPath() + "/shaders");
	model->setReadOnly(false);
	model->setNameFilters(QStringList() << "*.shd");
	model->setNameFilterDisables(false);
	model->setFilter(QDir::Filter::Files);

	m_ui->shaderList->setModel(model);
	m_ui->shaderList->setRootIndex(model->index(QDir::currentPath() + "/shaders"));

	parseDependencies();
}


static QString getSourceFromBinaryBasename(const QString& binary_basename)
{
	QString ret = binary_basename.mid(0, binary_basename.indexOf('_'));
	ret += binary_basename.mid(binary_basename.length() - 3);
	return ret;
}


void ShaderCompiler::onFileChanged(const char* path)
{
	QFileInfo info(path);
	if (info.suffix() == "shb")
	{
		auto shader_manager = m_editor->getEngine().getResourceManager().get(Lumix::ResourceManager::SHADER);
		QString shader_path("shaders/");
		QString source_basename = getSourceFromBinaryBasename(info.baseName());
		shader_path += source_basename.mid(0, source_basename.length() - 3) + ".shd";
		shader_manager->reload(Lumix::Path(shader_path.toLatin1().data()));
	}
	else
	{
		if (m_dependencies.contains(QString("shaders/") + path))
		{
			QString tmp = QString("shaders/") + path;
			tmp = tmp.mid(0, tmp.length() - 6) + ".shd";
			compile(tmp);
		}
	}
	parseDependencies();
}


void ShaderCompiler::parseDependencies()
{
	m_dependencies.clear();
	QDir dir("shaders/compiled");
	auto list = dir.entryInfoList(QStringList() << "*.shb.d", QDir::Files | QDir::NoDotAndDotDot, QDir::NoSort);
	for (auto info : list)
	{
		QFile file(info.absoluteFilePath());
		if (file.open(QIODevice::ReadOnly))
		{
			int end = 0;
			QString first_line(file.readLine().trimmed());
			while (end < first_line.length() && first_line[end].toLatin1() != ' ')
			{
				++end;
			}
			first_line = first_line.mid(0, end);
			QString line;
			while (!file.atEnd())
			{
				line = file.readLine().trimmed();
				end = 0;
				while (end < line.length() && line[end].toLatin1() != ' ')
				{
					++end;
				}
				line = line.mid(0, end);
				m_dependencies[line].push_back(first_line);
			}
			QFileInfo info(first_line);
			m_dependencies[QString("shaders/") + getSourceFromBinaryBasename(info.baseName()) + ".sc"].push_back(first_line);
			file.close();
		}
	}
}


ShaderCompiler::~ShaderCompiler()
{
	delete m_ui;
}


static void compilePass(QFileInfo& shader_file_info
	, bool is_vertex_shader
	, const char* pass
	, int define_mask
	, const Lumix::ShaderCombinations::Defines& all_defines)
{
	for (int mask = 0; mask < 1 << lengthOf(all_defines); ++mask)
	{
		if ((mask & (~define_mask)) == 0)
		{
			QProcess* process = new QProcess;
			process->connect(process, (void (QProcess::*)(int))&QProcess::finished, [process](int value){
				if (value != 0)
				{
					Lumix::g_log_error.log("shader compiler") << process->readAllStandardError().data();
				}
			});

			QString cmd = "shaders/shaderc.exe -f ";
			cmd.append(shader_file_info.dir().path() + "/" + shader_file_info.baseName());
			cmd.append(is_vertex_shader ? "_vs.sc" : "_fs.sc");
			cmd.append(" -o shaders/compiled/");
			cmd.append(shader_file_info.baseName() + "_" + pass);
			cmd.append(QString::number(mask));
			cmd.append(is_vertex_shader ? "_vs.shb" : "_fs.shb");
			cmd.append(" --depends --platform linux --profile 100 --type ");
			cmd.append(is_vertex_shader ? "vertex" : "fragment");
			cmd.append(" -D ");
			cmd.append(pass);
			for (int i = 0; i < lengthOf(all_defines); ++i)
			{
				if (mask & (1 << i))
				{
					cmd.append(" -D ");
					cmd.append(all_defines[i]);
				}
			}
			process->start(cmd);
		}
	}
}


static void compileAllPasses(QFileInfo& shader_file_info
	, bool is_vertex_shader
	, const int* define_masks
	, const Lumix::ShaderCombinations& combinations
)
{
	for (int i = 0; i < combinations.m_pass_count; ++i)
	{
		compilePass(shader_file_info, is_vertex_shader, combinations.m_passes[i], define_masks[i], combinations.m_defines);
	}
}


void ShaderCompiler::compile(const QString& path)
{
	QFileInfo file(path);
	QFile shader_file(path);
	if (shader_file.open(QIODevice::ReadOnly))
	{
		auto content = shader_file.readAll();
		Lumix::ShaderCombinations combinations;
		Lumix::Shader::getShaderCombinations(content.data(), &combinations);

		compileAllPasses(file, false, combinations.m_fs_combinations, combinations);
		compileAllPasses(file, true, combinations.m_vs_combinations, combinations);

		shader_file.close();
	}
}


void ShaderCompiler::compileAll()
{
	if (m_is_compiling)
	{
		return;
	}
	m_is_compiling = true;
	QDir dir("shaders");
	dir.mkpath("compiled");
	auto list = dir.entryInfoList(QStringList() << "*.shd");
	m_ui->progressBar->setValue(1);
	m_to_compile = list.size() * 2;
	for (auto file : list)
	{
		QFile shader_file(file.filePath());
		if (shader_file.open(QIODevice::ReadOnly))
		{
			auto content = shader_file.readAll();
			Lumix::ShaderCombinations combinations;
			Lumix::Shader::getShaderCombinations(content.data(), &combinations);

			compileAllPasses(file, false, combinations.m_fs_combinations, combinations);
			compileAllPasses(file, true, combinations.m_vs_combinations, combinations);

			shader_file.close();
		}
	}
}
