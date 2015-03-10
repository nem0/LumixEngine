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
	m_status = UNKNOWN;
	m_project_name = "default";
}


void ScriptCompiler::onScriptRenamed(const Lumix::Path& old_path, const Lumix::Path& new_path)
{
	for (int i = 0; m_scripts.size(); ++i)
	{
		if (m_scripts[i] == old_path)
		{
			m_scripts[i] = new_path;
			break;
		}
	}
}


void ScriptCompiler::addScript(const Lumix::Path& path)
{
	m_scripts.push_back(path);
	m_status = UNKNOWN;
}


void ScriptCompiler::removeScript(const Lumix::Path& path)
{
	for (int i = 0; m_scripts.size(); ++i)
	{
		if (m_scripts[i] == path)
		{
			m_scripts.removeAt(i);
			break;
		}
	}
	m_status = UNKNOWN;
}


void ScriptCompiler::clearScripts()
{
	m_status = UNKNOWN;
	m_scripts.clear();
}


void ScriptCompiler::compileAll()
{
	Lumix::ScriptScene* scene = static_cast<Lumix::ScriptScene*>(m_editor->getEngine().getScene(crc32("script")));
	scene->beforeScriptCompiled();
	m_status = NOT_COMPILED;
	QString sources = QString("		<ClCompile Include=\"%1.cpp\"/>\n").arg(m_project_name);
	for (int i = 0; i < m_scripts.size(); ++i)
	{
		sources += QString("		<ClCompile Include=\"%1\"/>\n").arg(m_scripts[i].c_str());
	}

	QFile file(QString("scripts/%1.vcxproj").arg(m_project_name));
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
		"			<AdditionalIncludeDirectories>%1\\src</AdditionalIncludeDirectories>\n"
		"		</ClCompile>\n"
		"		<Link>\n"
		"			<AdditionalDependencies>core.lib;engine.lib;physics.lib</AdditionalDependencies>\n"
		"			<AdditionalLibraryDirectories>%1\\bin\\win32_debug</AdditionalLibraryDirectories>\n"
		"			<GenerateDebugInformation>false</GenerateDebugInformation>"
		"		</Link>\n"
		"	</ItemDefinitionGroup>\n"
		"	<ItemGroup>\n").arg(m_sources_path).toLatin1().data());
	file.write(sources.toLatin1().data());
	file.write(
		"	</ItemGroup>\n"
		"	<Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Targets\"/>\n"
		"</Project>\n");
	file.close();

	QProcess* process = new QProcess;
	process->connect(process, (void (QProcess::*)(int))&QProcess::finished, [scene, this, process](int exit_code){
		process->deleteLater();
		m_log = process->readAll();
		m_status = exit_code == 0 ? SUCCESS : FAILURE;
		emitCompiled();
	});
	QStringList list;
	list << "/C";
	list << QString("%1/scripts/compile_all.bat %2").arg(m_base_path.c_str()).arg(file.fileName());
	process->start("cmd.exe", list);
}


void ScriptCompiler::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_editor = &editor;
}


void ScriptCompiler::onGameModeToggled(bool was_game_mode)
{
	if (!was_game_mode)
	{
		compileAll();
	}
}
