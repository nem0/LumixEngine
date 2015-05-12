#include "scriptcompiler.h"
#include <qdiriterator.h>
#include <qprocess.h>
#include <qtextstream.h>
#include "core/crc32.h"
#include "core/log.h"
#include "core/path.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "script/script_system.h"

ScriptCompiler::ScriptCompiler(QObject* parent) 
	: QObject(parent)
	, m_editor(NULL)
{
}


void ScriptCompiler::onScriptRenamed(const Lumix::Path& old_path, const Lumix::Path& new_path)
{
	for (auto iter = m_modules.begin(), end = m_modules.end(); iter != end; ++iter)
	{
		Module& module = *iter;
		for (int i = 0; i < module.m_scripts.size(); ++i)
		{
			if (module.m_scripts[i] == old_path)
			{
				module.m_scripts[i] = new_path;
				break;
			}
		}
	}
}


void ScriptCompiler::setModuleOutputPath(const QString& module_name, const QString& path)
{
	if (!m_modules.contains(module_name))
	{
		m_modules.insert(module_name, Module(module_name));
	}
	if (QFileInfo(path).isAbsolute())
	{
		m_modules[module_name].m_output_path = path;
	}
	else
	{
		m_modules[module_name].m_output_path = QString(m_editor->getBasePath()) + "/" + path;
	}
}


void ScriptCompiler::addScript(const QString& module_name, const QString& path)
{
	if (m_modules.find(module_name) == m_modules.end())
	{
		m_modules.insert(module_name, Module(module_name));
	}
	m_modules[module_name].m_scripts.push_back(path);
	m_modules[module_name].m_status = UNKNOWN;
}


void ScriptCompiler::removeScript(const Lumix::Path& path)
{
	for (auto iter = m_modules.begin(), end = m_modules.end(); iter != end; ++iter)
	{
		Module& module = *iter;
		for (int i = 0; i < module.m_scripts.size(); ++i)
		{
			if (module.m_scripts[i] == path)
			{
				module.m_scripts.removeAt(i);
				module.m_status = UNKNOWN;
				break;
			}
		}
	}
}


void ScriptCompiler::destroyModule(const QString& module_name)
{
	if (m_modules.contains(module_name))
	{
		m_modules[module_name].m_status = UNKNOWN;
		m_modules[module_name].m_scripts.clear();
	}
}


void ScriptCompiler::compileAllModules()
{
	for (auto iter = m_modules.begin(), end = m_modules.end(); iter != end; ++iter)
	{
		Module& module = *iter;
		compileModule(module.m_module_name);
	}
}


void ScriptCompiler::compileModule(const QString& module_name)
{
	if (!m_modules.contains(module_name))
	{
		return;
	}
	Module& module = m_modules[module_name];
	Q_ASSERT(!module.m_output_path.isEmpty());
	Lumix::OutputBlob* out_blob = m_editor->getAllocator().newObject<Lumix::OutputBlob>(m_editor->getAllocator());
	Lumix::ScriptScene* scene = static_cast<Lumix::ScriptScene*>(m_editor->getEngine().getScene(crc32("script")));
	if (m_editor->isGameMode())
	{
		scene->serializeScripts(*out_blob);
	}

	scene->beforeScriptCompiled();
	module.m_status = NOT_COMPILED;
	QString sources;
	for (int i = 0; i < module.m_scripts.size(); ++i)
	{
		sources += QString("		<ClCompile Include=\"%2/%1\"/>\n").arg(module.m_scripts[i]).arg(m_editor->getBasePath());
	}

	QFile file(QString("tmp/%1.vcxproj").arg(module.m_module_name));
	file.open(QIODevice::Text | QIODevice::WriteOnly);
	file.write(QString(
		"<Project DefaultTargets=\"Build\" ToolsVersion=\"12.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
		"	<ItemGroup>\n"
		"		<ProjectConfiguration Include = \"Debug|Win32\">\n"
		"			<Configuration>Debug</Configuration>\n"
		"			<Platform>Win32</Platform>\n"
		"		</ProjectConfiguration>\n"
		"		<ProjectConfiguration Include = \"Release|Win32\">\n"
		"			<Configuration>Release</Configuration>\n"
		"			<Platform>Win32</Platform>\n"
		"		</ProjectConfiguration>\n"
		"	</ItemGroup>\n"
		"	<Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.default.props\"/>\n"
		"	<PropertyGroup>\n"
		"		<ConfigurationType>DynamicLibrary</ConfigurationType>\n"
		"		<PlatformToolset>v120</PlatformToolset>\n"
		"	</PropertyGroup>\n"
		"	<Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\"/>\n"
		"	<ItemDefinitionGroup>\n"
		"		<ClCompile>\n"
		"			<AdditionalIncludeDirectories>%1\\src;%1\\external\\glew\\include</AdditionalIncludeDirectories>\n"
		"		</ClCompile>\n"
		"		<Link>\n"
		"			<AdditionalDependencies>animation.lib;core.lib;engine.lib;physics.lib</AdditionalDependencies>\n"
		"			<AdditionalLibraryDirectories>%1\\bin\\win32_debug</AdditionalLibraryDirectories>\n"
		"			<GenerateDebugInformation>true</GenerateDebugInformation>"
		"			<OutputFile>%2.dll</OutputFile>"
		"		</Link>\n"
		"	</ItemDefinitionGroup>\n"
		"	<ItemGroup>\n").arg(m_sources_path).arg(module.m_output_path).toLatin1().data());
	file.write(sources.toLatin1().data());
	file.write(
		"	</ItemGroup>\n"
		"	<Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Targets\"/>\n"
		"</Project>\n");
	file.close();

	QProcess* process = new QProcess;
	process->connect(process, (void (QProcess::*)(int))&QProcess::finished, [module_name, out_blob, scene, this, process](int exit_code){
		process->deleteLater();
		m_modules[module_name].m_log = process->readAll();
		m_modules[module_name].m_status = exit_code == 0 ? SUCCESS : FAILURE;
		emitCompiled(module_name);
		scene->afterScriptCompiled();
		if (m_editor->isGameMode())
		{
			Lumix::InputBlob blob(*out_blob);
			scene->deserializeScripts(blob);
			m_editor->getAllocator().deleteObject(out_blob);
		}
	});
	QStringList list;
	list << "/C";
	list << QString("%1/scripts/compile_all.bat %2").arg(m_editor->getBasePath()).arg(file.fileName());
	process->start("cmd.exe", list);
}


ScriptCompiler::Status ScriptCompiler::getStatus(const QString& module_name)
{
	return m_modules[module_name].m_status;
}


QString ScriptCompiler::getLog(const QString& module_name)
{
	return m_modules[module_name].m_log;
}


void ScriptCompiler::onScriptChanged(const QString& path)
{
	for (auto iter = m_modules.begin(), end = m_modules.end(); iter != end; ++iter)
	{
		Module& module = *iter;
		for (int i = 0; i < module.m_scripts.size(); ++i)
		{
			if (module.m_scripts[i] == path)
			{
				compileModule(module.m_module_name);
				break;
			}
		}
	}
}



void ScriptCompiler::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_editor = &editor;
}


void ScriptCompiler::onGameModeToggled(bool was_game_mode)
{
	if (!was_game_mode)
	{
		QFileInfo info(m_editor->getUniversePath().c_str());
		compileModule(info.baseName());
	}
}
