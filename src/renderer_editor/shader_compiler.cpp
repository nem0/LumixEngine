#include "shader_compiler.h"
#include "core/FS/file_system.h"
#include "core/FS/ifile.h"
#include "core/FS/os_file.h"
#include "core/log.h"
#include "core/mt/thread.h"
#include "core/path.h"
#include "core/path_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/system.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "engine/plugin_manager.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "studio_lib/file_system_watcher.h"
#include "studio_lib/log_ui.h"
#include "studio_lib/platform_interface.h"
#include "studio_lib/utils.h"


ShaderCompiler::ShaderCompiler(Lumix::WorldEditor& editor, LogUI& log_ui)
	: m_editor(editor)
	, m_log_ui(log_ui)
	, m_dependencies(editor.getAllocator())
	, m_to_reload(editor.getAllocator())
	, m_processes(editor.getAllocator())
	, m_changed_files(editor.getAllocator())
	, m_mutex(false)
{
	m_notifications_id = -1;
	m_is_compiling = false;

	m_watcher = FileSystemWatcher::create("shaders", m_editor.getAllocator());
	m_watcher->getCallback().bind<ShaderCompiler, &ShaderCompiler::onFileChanged>(this);
	parseDependencies();
	makeUpToDate();
}


static void getSourceFromBinaryBasename(char* out, int max_size, const char* binary_basename)
{
	const char* cin = binary_basename;
	Lumix::copyString(out, max_size, "shaders/");
	char* cout = out + Lumix::stringLength(out);
	while (*cin && *cin != '_')
	{
		*cout = *cin;
		++cout;
		++cin;
	}
	cin = binary_basename + Lumix::stringLength(binary_basename) - 3;
	if (cin > binary_basename)
	{
		Lumix::copyString(cout, max_size - int(cout - out), ".shd");
	}
}


Lumix::Renderer& ShaderCompiler::getRenderer()
{
	Lumix::IPlugin* plugin = m_editor.getEngine().getPluginManager().getPlugin("renderer");
	ASSERT(plugin);
	return static_cast<Lumix::Renderer&>(*plugin);
}


bool ShaderCompiler::isChanged(const Lumix::ShaderCombinations& combinations,
	const char* bin_base_path,
	const char* shd_path) const
{
	for (int i = 0; i < combinations.m_pass_count; ++i)
	{
		const char* pass_path =
			StringBuilder<Lumix::MAX_PATH_LENGTH>(bin_base_path, combinations.m_passes[i]);
		for (int j = 0; j < 1 << Lumix::lengthOf(combinations.m_defines); ++j)
		{
			if ((j & (~combinations.m_vs_combinations[i])) == 0)
			{
				const char* vs_bin_info =
					StringBuilder<Lumix::MAX_PATH_LENGTH>(pass_path, j, "_vs.shb");
				if (!PlatformInterface::fileExists(vs_bin_info) ||
					PlatformInterface::getLastModified(vs_bin_info) <
						PlatformInterface::getLastModified(shd_path))
				{
					return true;
				}
			}
			if ((j & (~combinations.m_fs_combinations[i])) == 0)
			{
				const char* fs_bin_info =
					StringBuilder<Lumix::MAX_PATH_LENGTH>(pass_path, j, "_fs.shb");
				if (!PlatformInterface::fileExists(fs_bin_info) ||
					PlatformInterface::getLastModified(fs_bin_info) <
						PlatformInterface::getLastModified(shd_path))
				{
					return true;
				}
			}
		}
	}
	return false;
}


void ShaderCompiler::makeUpToDate()
{
	auto* iter = PlatformInterface::createFileIterator("shaders", m_editor.getAllocator());

	Lumix::Array<Lumix::string> src_list(m_editor.getAllocator());
	auto& fs = m_editor.getEngine().getFileSystem();
	PlatformInterface::FileInfo info;
	while (getNextFile(iter, &info))
	{
		char basename[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getBasename(basename, sizeof(basename), info.filename);
		if (!Lumix::PathUtils::hasExtension(info.filename, "shd")) continue;
		const char* shd_path = StringBuilder<Lumix::MAX_PATH_LENGTH>("shaders/", info.filename);
		auto* file =
			fs.open(fs.getDiskDevice(), Lumix::Path(shd_path), Lumix::FS::Mode::OPEN_AND_READ);

		if (!file)
		{
			Lumix::g_log_error.log("shader compiler") << "Could not open " << info.filename;
			continue;
		}

		int len = (int)file->size();
		Lumix::Array<char> data(m_editor.getAllocator());
		data.resize(len + 1);
		file->read(&data[0], len);
		data[len] = 0;
		fs.close(*file);

		Lumix::ShaderCombinations combinations;
		Lumix::Shader::getShaderCombinations(getRenderer(), &data[0], &combinations);

		const char* bin_base_path =
			StringBuilder<Lumix::MAX_PATH_LENGTH>("shaders/compiled/", basename, "_");
		if (isChanged(combinations, bin_base_path, shd_path))
		{
			src_list.emplace(shd_path, m_editor.getAllocator());
		}
	}

	PlatformInterface::destroyFileIterator(iter);

	for (int i = 0; i < m_dependencies.size(); ++i)
	{
		auto& key = m_dependencies.getKey(i);
		auto& value = m_dependencies.at(i);
		for (auto& bin : value)
		{
			if (!PlatformInterface::fileExists(bin.c_str()) ||
				PlatformInterface::getLastModified(bin.c_str()) <
					PlatformInterface::getLastModified(key.c_str()))
			{
				char basename[Lumix::MAX_PATH_LENGTH];
				Lumix::PathUtils::getBasename(basename, sizeof(basename), bin.c_str());
				char tmp[Lumix::MAX_PATH_LENGTH];
				getSourceFromBinaryBasename(tmp, sizeof(tmp), basename);
				Lumix::string src(tmp, m_editor.getAllocator());
				src_list.push(src);
			}
		}
	}

	src_list.removeDuplicates();
	for (auto src : src_list)
	{
		compile(src.c_str());
	}
}


void ShaderCompiler::onFileChanged(const char* path)
{
	char ext[10];
	Lumix::PathUtils::getExtension(ext, sizeof(ext), path);
	if (Lumix::compareString("sc", ext) != 0 && Lumix::compareString("shd", ext) != 0 &&
		Lumix::compareString("sh", ext) != 0)
		return;

	char tmp[Lumix::MAX_PATH_LENGTH];
	Lumix::copyString(tmp, "shaders/");
	Lumix::catString(tmp, path);
	Lumix::MT::SpinLock lock(m_mutex);
	m_changed_files.push(Lumix::string(tmp, m_editor.getAllocator()));
}


static bool readLine(Lumix::FS::IFile* file, char* out, int max_size)
{
	ASSERT(max_size > 0);
	char* c = out;

	while (c < out + max_size - 1)
	{
		if (!file->read(c, 1))
		{
			return (c != out);
		}
		if (*c == '\n') break;
		++c;
	}
	*c = '\0';
	return true;
}


void ShaderCompiler::parseDependencies()
{
	m_dependencies.clear();
	auto* iter = PlatformInterface::createFileIterator("shaders/compiled", m_editor.getAllocator());

	auto& fs = m_editor.getEngine().getFileSystem();
	PlatformInterface::FileInfo info;
	while (PlatformInterface::getNextFile(iter, &info))
	{
		if (!Lumix::PathUtils::hasExtension(info.filename, "d")) continue;

		auto* file = fs.open(fs.getDiskDevice(),
			Lumix::Path(StringBuilder<Lumix::MAX_PATH_LENGTH>("shaders/compiled/", info.filename)),
			Lumix::FS::Mode::READ | Lumix::FS::Mode::OPEN);
		if (!file)
		{
			Lumix::g_log_error.log("shader compiler") << "Could not open " << info.filename;
			continue;
		}

		char first_line[100];
		readLine(file, first_line, sizeof(first_line));
		for (int i = 0; i < sizeof(first_line); ++i)
		{
			if (first_line[i] == '\0' || first_line[i] == ' ')
			{
				first_line[i] = '\0';
				break;
			}
		}

		char line[100];
		while (readLine(file, line, sizeof(line)))
		{
			char* trimmed_line = Lumix::trimmed(line);
			char* c = trimmed_line;
			while (*c)
			{
				if (*c == ' ') break;
				++c;
			}
			*c = '\0';

			addDependency(trimmed_line, first_line);
		}

		char basename[Lumix::MAX_PATH_LENGTH];
		char src[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getBasename(basename, sizeof(basename), first_line);
		getSourceFromBinaryBasename(src, sizeof(src), basename);

		addDependency(src, first_line);

		fs.close(*file);
	}

	PlatformInterface::destroyFileIterator(iter);
}


void ShaderCompiler::addDependency(const char* ckey, const char* cvalue)
{
	Lumix::string key(ckey, m_editor.getAllocator());

	int idx = m_dependencies.find(key);
	if (idx < 0)
	{
		idx = m_dependencies.insert(key, Lumix::Array<Lumix::string>(m_editor.getAllocator()));
	}
	m_dependencies.at(idx).emplace(cvalue, m_editor.getAllocator());
}


ShaderCompiler::~ShaderCompiler()
{
	while (!m_processes.empty()) update();

	FileSystemWatcher::destroy(m_watcher);
}


void ShaderCompiler::reloadShaders()
{
	m_to_reload.removeDuplicates();

	auto shader_manager =
		m_editor.getEngine().getResourceManager().get(Lumix::ResourceManager::SHADER);
	for (auto& path : m_to_reload)
	{
		shader_manager->reload(Lumix::Path(path.c_str()));
	}

	m_to_reload.clear();
}


void ShaderCompiler::updateNotifications()
{
	if (m_is_compiling && m_notifications_id < 0)
	{
		m_notifications_id = m_log_ui.addNotification("Compiling shaders...");
	}

	if (!m_is_compiling)
	{
		m_log_ui.setNotificationTime(m_notifications_id, 3.0f);
		m_notifications_id = -1;
	}
}


void ShaderCompiler::compilePass(const char* shd_path,
	bool is_vertex_shader,
	const char* pass,
	int define_mask,
	const Lumix::ShaderCombinations::Defines& all_defines)
{
	for (int mask = 0; mask < 1 << Lumix::lengthOf(all_defines); ++mask)
	{
		if ((mask & (~define_mask)) == 0)
		{
			updateNotifications();
			char basename[Lumix::MAX_PATH_LENGTH];
			Lumix::PathUtils::getBasename(basename, sizeof(basename), shd_path);
			const char* source_path = StringBuilder<Lumix::MAX_PATH_LENGTH>(
				"\"shaders/", basename, is_vertex_shader ? "_vs.sc\"" : "_fs.sc\"");
			char out_path[Lumix::MAX_PATH_LENGTH];
			Lumix::copyString(out_path, m_editor.getBasePath());
			Lumix::catString(out_path, "/shaders/compiled/");
			Lumix::catString(out_path, basename);
			Lumix::catString(out_path, "_");
			Lumix::catString(out_path, StringBuilder<30>(pass, mask));
			Lumix::catString(out_path, is_vertex_shader ? "_vs.shb" : "_fs.shb");

			StringBuilder<1024> args(" -f ");

			args << source_path << " -o \"" << out_path << "\" --depends --platform windows --type "
				 << (is_vertex_shader ? "vertex --profile vs_4_0" : "fragment --profile ps_4_0")
				 << " -D " << pass;
			for (int i = 0; i < Lumix::lengthOf(all_defines); ++i)
			{
				if (mask & (1 << i))
				{
					args << " -D " << getRenderer().getShaderDefine(all_defines[i]);
				}
			}

			StringBuilder<Lumix::MAX_PATH_LENGTH> cmd(
				m_editor.getBasePath(), "/shaders/shaderc.exe");

			PlatformInterface::deleteFile(out_path);
			auto* process = PlatformInterface::createProcess(cmd, args, m_editor.getAllocator());
			if (!process)
			{
				Lumix::g_log_error.log("shader compiler") << "Could not execute command: " << cmd;
			}
			else
			{
				auto& p = m_processes.pushEmpty();
				p.process = process;
				Lumix::copyString(p.path, out_path);
			}
		}
	}
}


void ShaderCompiler::processChangedFiles()
{
	if (m_is_compiling) return;

	char changed_file_path[Lumix::MAX_PATH_LENGTH];
	{
		Lumix::MT::SpinLock lock(m_mutex);
		if (m_changed_files.empty()) return;

		m_changed_files.removeDuplicates();
		const char* tmp = m_changed_files.back().c_str();
		Lumix::copyString(changed_file_path, sizeof(changed_file_path), tmp);
		m_changed_files.pop();
	}
	Lumix::string tmp_string(changed_file_path, m_editor.getAllocator());
	int find_idx = m_dependencies.find(tmp_string);
	if (find_idx < 0)
	{
		int len = Lumix::stringLength(changed_file_path);
		if (len <= 6) return;

		if (Lumix::compareString(changed_file_path + len - 6, "_fs.sc") == 0 ||
			Lumix::compareString(changed_file_path + len - 6, "_vs.sc") == 0)
		{
			Lumix::copyString(
				changed_file_path + len - 6, Lumix::lengthOf(changed_file_path) - len + 6, ".shd");
			tmp_string = changed_file_path;
			find_idx = m_dependencies.find(tmp_string);
		}
	}
	if (find_idx >= 0)
	{
		if (Lumix::PathUtils::hasExtension(changed_file_path, "shd"))
		{
			compile(changed_file_path);
		}
		else
		{
			Lumix::Array<Lumix::string> src_list(m_editor.getAllocator());

			for (auto& bin : m_dependencies.at(find_idx))
			{
				char basename[Lumix::MAX_PATH_LENGTH];
				Lumix::PathUtils::getBasename(basename, sizeof(basename), bin.c_str());
				char tmp[Lumix::MAX_PATH_LENGTH];
				getSourceFromBinaryBasename(tmp, sizeof(tmp), basename);
				Lumix::string src(tmp, m_editor.getAllocator());
				src_list.push(src);
			}

			src_list.removeDuplicates();

			for (auto& src : src_list)
			{
				compile(src.c_str());
			}
		}
	}
}


void ShaderCompiler::wait()
{
	while (m_is_compiling)
	{
		update();
		Lumix::MT::sleep(500);
	}
}


void ShaderCompiler::update()
{
	PROFILE_FUNCTION();
	for (int i = 0; i < m_processes.size(); ++i)
	{
		if (PlatformInterface::isProcessFinished(*m_processes[i].process))
		{

			bool failed = PlatformInterface::getProcessExitCode(*m_processes[i].process) != 0;
			if (failed)
			{
				if (strstr(m_processes[i].path, "imgui") != nullptr)
				{
					Lumix::messageBox("Could not compile imgui shader");
				}

				char buf[1024];
				int read;
				while ((read = PlatformInterface::getProcessOutput(
							*m_processes[i].process, buf, sizeof(buf) - 1)) > 0)
				{
					buf[read] = 0;
					Lumix::g_log_error.log("shader compiler") << buf;
				}
			}

			PlatformInterface::destroyProcess(*m_processes[i].process);
			m_processes.eraseFast(i);

			updateNotifications();
			if (m_processes.empty() && m_changed_files.empty())
			{
				reloadShaders();
				parseDependencies();
			}
		}
	}
	m_is_compiling = !m_processes.empty();

	processChangedFiles();
}


void ShaderCompiler::compileAllPasses(const char* path,
	bool is_vertex_shader,
	const int* define_masks,
	const Lumix::ShaderCombinations& combinations)
{
	for (int i = 0; i < combinations.m_pass_count; ++i)
	{
		compilePass(path,
			is_vertex_shader,
			combinations.m_passes[i],
			define_masks[i],
			combinations.m_defines);
	}
}


void ShaderCompiler::compile(const char* path)
{
	StringBuilder<Lumix::MAX_PATH_LENGTH> compiled_dir(m_editor.getBasePath(), "/shaders/compiled");
	if (!PlatformInterface::makePath(compiled_dir))
	{
		if (!PlatformInterface::dirExists(compiled_dir))
		{
			Lumix::messageBox("Could not create directory shaders/compiled. Please create it and "
							  "restart the editor");
		}
	}

	m_to_reload.emplace(path, m_editor.getAllocator());

	auto& fs = m_editor.getEngine().getFileSystem();
	auto* file = fs.open(fs.getDiskDevice(), Lumix::Path(path), Lumix::FS::Mode::OPEN_AND_READ);
	if (file)
	{
		int size = (int)file->size();
		Lumix::Array<char> data(m_editor.getAllocator());
		data.resize(size + 1);
		file->read(&data[0], size);
		data[size] = 0;
		fs.close(*file);

		Lumix::ShaderCombinations combinations;
		Lumix::Shader::getShaderCombinations(getRenderer(), &data[0], &combinations);

		compileAllPasses(path, false, combinations.m_fs_combinations, combinations);
		compileAllPasses(path, true, combinations.m_vs_combinations, combinations);
	}
	else
	{
		Lumix::g_log_error.log("shader compiler") << "Could not open " << path;
	}
}


void ShaderCompiler::compileAll(bool wait)
{
	if (m_is_compiling)
	{
		if(wait) this->wait();
		return;
	}

	m_is_compiling = true;

	PlatformInterface::FileInfo info;
	auto* iter = PlatformInterface::createFileIterator("shaders", m_editor.getAllocator());

	auto& fs = m_editor.getEngine().getFileSystem();
	while (PlatformInterface::getNextFile(iter, &info))
	{
		if (!Lumix::PathUtils::hasExtension(info.filename, "shd")) continue;

		const char* shd_path = StringBuilder<Lumix::MAX_PATH_LENGTH>("shaders/", info.filename);
		auto* file =
			fs.open(fs.getDiskDevice(), Lumix::Path(shd_path), Lumix::FS::Mode::READ | Lumix::FS::Mode::OPEN);

		if (file)
		{
			int size = (int)file->size();
			Lumix::Array<char> data(m_editor.getAllocator());
			data.resize(size + 1);
			file->read(&data[0], size);
			data[size] = '\0';

			Lumix::ShaderCombinations combinations;
			Lumix::Shader::getShaderCombinations(getRenderer(), &data[0], &combinations);

			compileAllPasses(shd_path, false, combinations.m_fs_combinations, combinations);
			compileAllPasses(shd_path, true, combinations.m_vs_combinations, combinations);

			fs.close(*file);
		}
		else
		{
			Lumix::g_log_error.log("shader compiler") << "Could not open " << shd_path;
		}
	}

	PlatformInterface::destroyFileIterator(iter);

	if(wait)
	{
		this->wait();
	}
}
