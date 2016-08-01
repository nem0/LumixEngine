#pragma once


#include "engine/associative_array.h"
#include "engine/mt/sync.h"
#include "engine/string.h"
#include "renderer/shader.h"


namespace Lumix
{
struct ShaderCombinations;
class WorldEditor;
}

namespace PlatformInterface
{
struct Process;
}


class FileSystemWatcher;
class LogUI;
class StudioApp;


class ShaderCompiler
{
public:
	ShaderCompiler(StudioApp& app, LogUI& log_ui);
	~ShaderCompiler();

	void compileAll(bool wait);
	void update();
	bool isCompiling() const { return m_is_compiling; }

private:
	void wait();
	void reloadShaders();
	void onCompiled(int value);
	void updateNotifications();
	void compileAllPasses(const char* path,
						  bool is_vertex_shader,
						  const int* define_masks,
						  const Lumix::ShaderCombinations& combinations);
	void compilePass(const char* path,
					 bool is_vertex_shader,
					 const char* pass,
					 int define_mask,
					 const Lumix::ShaderCombinations::Defines& all_defines);
	bool isChanged(const Lumix::ShaderCombinations& combinations,
				   const char* bin_base_path,
				   const char* shd_path) const;

	void onFileChanged(const char* path);
	void parseDependencies();
	void compile(const char* path);
	void makeUpToDate();
	Lumix::Renderer& getRenderer();
	void addDependency(const char* key, const char* value);
	void processChangedFiles();

private:
	struct ProcessInfo
	{
		PlatformInterface::Process* process;
		char path[Lumix::MAX_PATH_LENGTH];
	};

private:
	StudioApp& m_app;
	Lumix::WorldEditor& m_editor;
	FileSystemWatcher* m_watcher;
	int m_notifications_id;
	Lumix::AssociativeArray<Lumix::string, Lumix::Array<Lumix::string>> m_dependencies;
	Lumix::Array<Lumix::string> m_to_compile;
	Lumix::Array<Lumix::string> m_to_reload;
	Lumix::Array<ProcessInfo> m_processes;
	Lumix::Array<Lumix::string> m_changed_files;
	Lumix::MT::SpinMutex m_mutex;
	LogUI& m_log_ui;
	bool m_is_compiling;
};
