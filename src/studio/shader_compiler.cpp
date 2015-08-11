#include "shader_compiler.h"
#include "ui_shader_compiler.h"
#include "core/log.h"
#include "core/path.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "file_system_watcher.h"
#include "graphics/shader.h"
#include "notifications.h"
#include <qdatetime.h>
#include <qdir.h>
#include <qfilesystemmodel.h>
#include <qprocess.h>


ShaderCompiler::ShaderCompiler()
	: m_editor(nullptr)
	, m_notifications(nullptr)

{
	m_compiled = 0;
	m_to_compile = 0;
	m_notifications_id = -1;
	m_is_compiling = false;
	m_watcher = FileSystemWatcher::create("shaders");
	m_watcher->getCallback()
		.bind<ShaderCompiler, &ShaderCompiler::onFileChanged>(this);

	parseDependencies();
	makeUpToDate();
}


static QString getSourceFromBinaryBasename(const QString& binary_basename)
{
	QString ret = binary_basename.mid(0, binary_basename.indexOf('_'));
	ret += binary_basename.mid(binary_basename.length() - 3);
	return ret;
}


void ShaderCompiler::makeUpToDate()
{
	QStringList src_list;
	QDir().mkpath("shaders/compiled");
	QDir dir("shaders");
	for (auto source_info : dir.entryInfoList())
	{
		if (source_info.suffix() == "shd")
		{
			QFile file(source_info.filePath());
			if (file.open(QIODevice::ReadOnly))
			{
				auto content = file.readAll();
				Lumix::ShaderCombinations combinations;
				Lumix::Shader::getShaderCombinations(content.data(),
													 &combinations);

				QString bin_base_path =
					QString("shaders/compiled/%1_").arg(source_info.baseName());
				for (int i = 0; i < combinations.m_pass_count; ++i)
				{
					QString pass_path =
						bin_base_path + combinations.m_passes[i];
					for (int j = 0;
						 j < 1 << Lumix::lengthOf(combinations.m_defines);
						 ++j)
					{
						if ((j & (~combinations.m_vs_combinations[i])) == 0)
						{
							QFileInfo vs_bin_info(
								pass_path + QString::number(j) + "_vs.shb");
							if (!vs_bin_info.exists() ||
								vs_bin_info.lastModified() <
									source_info.lastModified())
							{
								src_list.push_back(source_info.baseName());
								break;
							}
						}
						if ((j & (~combinations.m_fs_combinations[i])) == 0)
						{
							QFileInfo fs_bin_info(
								pass_path + QString::number(j) + "_fs.shb");
							if (!fs_bin_info.exists() ||
								fs_bin_info.lastModified() <
									source_info.lastModified())
							{
								src_list.push_back(source_info.baseName());
								break;
							}
						}
					}
				}

				file.close();
			}
		}
	}

	for (auto iter = m_dependencies.begin(), end = m_dependencies.end();
		 iter != end;
		 ++iter)
	{
		QFileInfo source_info(iter.key());
		for (auto bin : iter.value())
		{
			QFileInfo bin_info(bin);
			if (!bin_info.exists() ||
				bin_info.lastModified() < source_info.lastModified())
			{
				auto src = getSourceFromBinaryBasename(bin_info.baseName());
				src = src.left(src.length() - 3);
				src_list.push_back(src);
			}
		}
	}
	src_list.removeDuplicates();
	for (auto src : src_list)
	{
		compile(QString("shaders/") + src + ".shd");
	}
}


void ShaderCompiler::onFileChanged(const char* path)
{
	QFileInfo info(path);
	if (m_dependencies.contains(QString("shaders/") + path))
	{
		QString tmp = QString("shaders/") + path;
		tmp = tmp.mid(0, tmp.length() - 6) + ".shd";
		compile(tmp);
	}
	parseDependencies();
}


void ShaderCompiler::setNotifications(Notifications& notifications)
{
	m_notifications = &notifications;
}


void ShaderCompiler::parseDependencies()
{
	m_dependencies.clear();
	QDir dir("shaders/compiled");
	auto list = dir.entryInfoList(QStringList() << "*.shb.d",
								  QDir::Files | QDir::NoDotAndDotDot,
								  QDir::NoSort);
	for (auto info : list)
	{
		QFile file(info.absoluteFilePath());
		if (file.open(QIODevice::ReadOnly))
		{
			int end = 0;
			QString first_line(file.readLine().trimmed());
			while (end < first_line.length() &&
				   first_line[end].toLatin1() != ' ')
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
			m_dependencies[QString("shaders/") +
						   getSourceFromBinaryBasename(info.baseName()) + ".sc"]
				.push_back(first_line);
			file.close();
		}
	}
}


ShaderCompiler::~ShaderCompiler()
{
}


void ShaderCompiler::reloadShaders()
{
	m_to_reload.removeDuplicates();

	if (m_editor)
	{
		auto shader_manager = m_editor->getEngine().getResourceManager().get(
			Lumix::ResourceManager::SHADER);
		for (auto& path : m_to_reload)
		{
			shader_manager->reload(Lumix::Path(path.toLatin1().data()));
		}
	}

	m_to_reload.clear();
}


void ShaderCompiler::updateNotifications()
{
	if (m_notifications)
	{
		if (m_notifications_id < 0)
		{
			m_notifications_id = m_notifications->showProgressNotification(
				"Compiling shaders...");
		}

		m_notifications->setProgress(m_notifications_id,
									 qMax(100 * m_compiled / m_to_compile, 1));

		if (m_to_compile == m_compiled)
		{
			reloadShaders();
			m_to_compile = m_compiled = 0;
			m_notifications->setNotificationTime(m_notifications_id, 3.0f);
			m_notifications_id = -1;
		}
	}
}


void ShaderCompiler::compilePass(
	QFileInfo& shader_file_info,
	bool is_vertex_shader,
	const char* pass,
	int define_mask,
	const Lumix::ShaderCombinations::Defines& all_defines)
{

	for (int mask = 0; mask < 1 << Lumix::lengthOf(all_defines); ++mask)
	{
		if ((mask & (~define_mask)) == 0)
		{
			++m_to_compile;
			updateNotifications();
			QProcess* process = new QProcess;
			QString source_path = shader_file_info.dir().path() + "/" +
								  shader_file_info.baseName() +
								  (is_vertex_shader ? "_vs.sc" : "_fs.sc");
			process->connect(
				process,
				(void (QProcess::*)(int)) & QProcess::finished,
				[process, this, source_path](int value)
				{
					++m_compiled;
					updateNotifications();
					if (value != 0)
					{
						Lumix::g_log_error.log("shader compiler")
							<< source_path.toLatin1().data() << ":\n"
							<< process->readAllStandardError().data();
					}
				});

			QString cmd = "shaders/shaderc.exe -f ";
			cmd.append(source_path);
			cmd.append(" -o shaders/compiled/");
			cmd.append(shader_file_info.baseName() + "_" + pass);
			cmd.append(QString::number(mask));
			cmd.append(is_vertex_shader ? "_vs.shb" : "_fs.shb");
			cmd.append(" --depends --platform windows --type ");
			cmd.append(is_vertex_shader ? "vertex --profile vs_3_0" : "fragment --profile ps_3_0");
			cmd.append(" -D ");
			cmd.append(pass);
			for (int i = 0; i < Lumix::lengthOf(all_defines); ++i)
			{
				if (mask & (1 << i))
				{
					cmd.append(" -D ");
					cmd.append(all_defines[i]);
				}
			}
			QFile::remove(QString("shaders/compiled/") +
						  shader_file_info.baseName() + "_" + pass +
						  QString::number(mask) +
						  (is_vertex_shader ? "_vs.shb" : "_fs.shb"));
			process->start(cmd);
		}
	}
}


void ShaderCompiler::compileAllPasses(
	QFileInfo& shader_file_info,
	bool is_vertex_shader,
	const int* define_masks,
	const Lumix::ShaderCombinations& combinations)
{
	for (int i = 0; i < combinations.m_pass_count; ++i)
	{
		compilePass(shader_file_info,
					is_vertex_shader,
					combinations.m_passes[i],
					define_masks[i],
					combinations.m_defines);
	}
}


void ShaderCompiler::compile(const QString& path)
{
	m_to_reload.push_back(path);
	QFileInfo file(path);
	QFile shader_file(path);
	if (shader_file.open(QIODevice::ReadOnly))
	{
		auto content = shader_file.readAll();
		Lumix::ShaderCombinations combinations;
		Lumix::Shader::getShaderCombinations(content.data(), &combinations);

		compileAllPasses(
			file, false, combinations.m_fs_combinations, combinations);
		compileAllPasses(
			file, true, combinations.m_vs_combinations, combinations);

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
	for (auto file : list)
	{
		QFile shader_file(file.filePath());
		if (shader_file.open(QIODevice::ReadOnly))
		{
			auto content = shader_file.readAll();
			Lumix::ShaderCombinations combinations;
			Lumix::Shader::getShaderCombinations(content.data(), &combinations);

			compileAllPasses(
				file, false, combinations.m_fs_combinations, combinations);
			compileAllPasses(
				file, true, combinations.m_vs_combinations, combinations);

			shader_file.close();
		}
	}
}
