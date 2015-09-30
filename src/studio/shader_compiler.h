#pragma once


#include "core/associative_array.h"
#include "core/mt/spin_mutex.h"
#include "core/string.h"
#include "renderer/shader.h"


namespace Lumix
{
struct Process;
class ShaderCombinations;
class WorldEditor;
}


class FileSystemWatcher;
class LogUI;


class ShaderCompiler
{
public:
	ShaderCompiler(Lumix::WorldEditor& editor, LogUI& log_ui);
	~ShaderCompiler();

	void compileAll();
	void update(float time_delta);

private:
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
		Lumix::Process* process;
		char path[Lumix::MAX_PATH_LENGTH];
	};

private:
	bool m_is_compiling;
	Lumix::WorldEditor& m_editor;
	FileSystemWatcher* m_watcher;
	int m_notifications_id;
	Lumix::AssociativeArray<Lumix::string, Lumix::Array<Lumix::string>> m_dependencies;
	Lumix::Array<Lumix::string> m_to_reload;
	Lumix::Array<ProcessInfo> m_processes;
	Lumix::Array<Lumix::string> m_changed_files;
	Lumix::MT::SpinMutex m_mutex;
	LogUI& m_log_ui;
};
