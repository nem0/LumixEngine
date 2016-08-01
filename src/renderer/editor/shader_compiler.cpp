#include "shader_compiler.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/os_file.h"
#include "engine/log.h"
#include "engine/mt/thread.h"
#include "engine/path.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/system.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "renderer/renderer.h"
#include "editor/asset_browser.h"
#include "editor/file_system_watcher.h"
#include "editor/log_ui.h"
#include "editor/platform_interface.h"
#include "editor/studio_app.h"
#include "editor/utils.h"


static const Lumix::ResourceType SHADER_TYPE("shader");


ShaderCompiler::ShaderCompiler(StudioApp& app, LogUI& log_ui)
	: m_app(app)
	, m_editor(*app.getWorldEditor())
	, m_log_ui(log_ui)
	, m_dependencies(m_editor.getAllocator())
	, m_to_compile(m_editor.getAllocator())
	, m_to_reload(m_editor.getAllocator())
	, m_processes(m_editor.getAllocator())
	, m_changed_files(m_editor.getAllocator())
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


static void getShaderPath(const char* shd_path, char* out, bool vertex)
{
	Lumix::PathUtils::FileInfo file_info(shd_path);
	Lumix::copyString(out, Lumix::MAX_PATH_LENGTH, file_info.m_dir);
	Lumix::catString(out, Lumix::MAX_PATH_LENGTH, file_info.m_basename);
	Lumix::catString(out, Lumix::MAX_PATH_LENGTH, vertex ? "_vs.sc" : "_fs.sc");
}


bool ShaderCompiler::isChanged(const Lumix::ShaderCombinations& combinations,
	const char* bin_base_path,
	const char* shd_path) const
{
	char tmp[Lumix::MAX_PATH_LENGTH];
	auto shd_last_modified = PlatformInterface::getLastModified(shd_path);
	getShaderPath(shd_path, tmp, true);
	if (!PlatformInterface::fileExists(tmp) ||
		PlatformInterface::getLastModified(tmp) > shd_last_modified)
	{
		shd_last_modified = PlatformInterface::getLastModified(tmp);
	}

	getShaderPath(shd_path, tmp, false);
	if (!PlatformInterface::fileExists(tmp) ||
		PlatformInterface::getLastModified(tmp) > shd_last_modified)
	{
		shd_last_modified = PlatformInterface::getLastModified(tmp);
	}

	for (int i = 0; i < combinations.pass_count; ++i)
	{
		const char* pass_path =
			Lumix::StaticString<Lumix::MAX_PATH_LENGTH>(bin_base_path, combinations.passes[i]);
		for (int j = 0; j < 1 << Lumix::lengthOf(combinations.defines); ++j)
		{
			if ((j & (~combinations.vs_local_mask[i])) == 0)
			{
				const char* vs_bin_info =
					Lumix::StaticString<Lumix::MAX_PATH_LENGTH>(pass_path, j, "_vs.shb");
				if (!PlatformInterface::fileExists(vs_bin_info) ||
					PlatformInterface::getLastModified(vs_bin_info) < shd_last_modified)
				{
					return true;
				}
			}
			if ((j & (~combinations.fs_local_mask[i])) == 0)
			{
				const char* fs_bin_info =
					Lumix::StaticString<Lumix::MAX_PATH_LENGTH>(pass_path, j, "_fs.shb");
				if (!PlatformInterface::fileExists(fs_bin_info) ||
					PlatformInterface::getLastModified(fs_bin_info) < shd_last_modified)
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
	bool is_opengl = getRenderer().isOpenGL();
	Lumix::Array<Lumix::string> src_list(m_editor.getAllocator());
	auto& fs = m_editor.getEngine().getFileSystem();
	PlatformInterface::FileInfo info;
	while (getNextFile(iter, &info))
	{
		char basename[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getBasename(basename, sizeof(basename), info.filename);
		if (!Lumix::PathUtils::hasExtension(info.filename, "shd")) continue;
		Lumix::StaticString<Lumix::MAX_PATH_LENGTH> shd_path("shaders/", info.filename);
		auto* file =
			fs.open(fs.getDiskDevice(), Lumix::Path(shd_path), Lumix::FS::Mode::OPEN_AND_READ);

		if (!file)
		{
			Lumix::g_log_error.log("Editor") << "Could not open " << info.filename;
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

		Lumix::StaticString<Lumix::MAX_PATH_LENGTH> bin_base_path(
			"shaders/compiled", is_opengl ? "_gl/" : "/", basename, "_");
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
		m_to_compile.emplace(src.c_str(), m_editor.getAllocator());
	}
}


void ShaderCompiler::onFileChanged(const char* path)
{
	char ext[10];
	Lumix::PathUtils::getExtension(ext, sizeof(ext), path);
	if (!Lumix::equalStrings("sc", ext) && !Lumix::equalStrings("shd", ext) && !Lumix::equalStrings("sh", ext)) return;

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
	bool is_opengl = getRenderer().isOpenGL();
	Lumix::StaticString<30> compiled_dir("shaders/compiled", is_opengl ? "_gl" : "");
	auto* iter = PlatformInterface::createFileIterator(compiled_dir, m_editor.getAllocator());

	auto& fs = m_editor.getEngine().getFileSystem();
	PlatformInterface::FileInfo info;
	while (PlatformInterface::getNextFile(iter, &info))
	{
		if (!Lumix::PathUtils::hasExtension(info.filename, "d")) continue;

		auto* file = fs.open(fs.getDiskDevice(),
			Lumix::Path(Lumix::StaticString<Lumix::MAX_PATH_LENGTH>(compiled_dir, "/", info.filename)),
			Lumix::FS::Mode::READ | Lumix::FS::Mode::OPEN);
		if (!file)
		{
			Lumix::g_log_error.log("Editor") << "Could not open " << info.filename;
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

	auto shader_manager = m_editor.getEngine().getResourceManager().get(SHADER_TYPE);
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
	const char* base_path = m_editor.getEngine().getDiskFileDevice()->getBasePath();
	bool is_opengl = getRenderer().isOpenGL();

	for (int mask = 0; mask < 1 << Lumix::lengthOf(all_defines); ++mask)
	{
		if ((mask & (~define_mask)) == 0)
		{
			updateNotifications();
			char basename[Lumix::MAX_PATH_LENGTH];
			Lumix::PathUtils::getBasename(basename, sizeof(basename), shd_path);
			Lumix::StaticString<Lumix::MAX_PATH_LENGTH> source_path (
				"\"shaders/", basename, is_vertex_shader ? "_vs.sc\"" : "_fs.sc\"");
			char out_path[Lumix::MAX_PATH_LENGTH];
			Lumix::copyString(out_path, base_path);
			Lumix::catString(out_path, "/shaders/compiled");
			Lumix::catString(out_path, is_opengl ? "_gl/" : "/");
			Lumix::catString(out_path, basename);
			Lumix::catString(out_path, "_");
			Lumix::catString(out_path, Lumix::StaticString<30>(pass, mask));
			Lumix::catString(out_path, is_vertex_shader ? "_vs.shb" : "_fs.shb");

			Lumix::StaticString<1024> args(" -f ");

			args << source_path << " -o \"" << out_path << "\" --depends ";
			if (getRenderer().isOpenGL())
			{
				args << "--platform linux --profile 140 ";
			}
			else
			{
				args << "--platform windows " << (is_vertex_shader ? "--profile vs_5_0" : "--profile ps_5_0");
			}
			args << " --type "
				<< (is_vertex_shader ? "vertex -O3" : "fragment -O3")
				<< " --define " << pass << ";";
			for (int i = 0; i < Lumix::lengthOf(all_defines); ++i)
			{
				if (mask & (1 << i))
				{
					args << getRenderer().getShaderDefine(all_defines[i]) << ";";
				}
			}

			#ifdef _WIN32
				Lumix::StaticString<Lumix::MAX_PATH_LENGTH> cmd(base_path, "/shaders/shaderc.exe");
			#elif defined __linux__
				Lumix::StaticString<Lumix::MAX_PATH_LENGTH> cmd(base_path, "/shaders/shaderc");
			#else
				#error Platform not supported
			#endif
			
			PlatformInterface::deleteFile(out_path);
			auto* process = PlatformInterface::createProcess(cmd, args, m_editor.getAllocator());
			if (!process)
			{
				Lumix::g_log_error.log("Editor") << "Could not execute command: " << cmd;
			}
			else
			{
				auto& p = m_processes.emplace();
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
		if (Lumix::equalStrings(changed_file_path + len - 6, "_fs.sc") ||
			Lumix::equalStrings(changed_file_path + len - 6, "_vs.sc"))
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
			m_to_compile.emplace(changed_file_path, m_editor.getAllocator());
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
				m_to_compile.emplace(src.c_str(), m_editor.getAllocator());
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
				Lumix::g_log_error.log("Editor") << "Failed to compile " << m_processes[i].path << ". Error log:";
				while ((read = PlatformInterface::getProcessOutput(*m_processes[i].process, buf, sizeof(buf) - 1)) > 0)
				{
					buf[read] = 0;
					Lumix::g_log_error.log("Editor") << buf;
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
	m_is_compiling = !m_processes.empty() || !m_to_compile.empty();
	m_app.getAssetBrowser()->enableUpdate(!m_is_compiling);

	processChangedFiles();

	if (m_processes.size() < 4 && !m_to_compile.empty())
	{
		compile(m_to_compile.back().c_str());
		m_to_compile.pop();
	}
}


void ShaderCompiler::compileAllPasses(const char* path,
	bool is_vertex_shader,
	const int* define_masks,
	const Lumix::ShaderCombinations& combinations)
{
	for (int i = 0; i < combinations.pass_count; ++i)
	{
		compilePass(path,
			is_vertex_shader,
			combinations.passes[i],
			define_masks[i],
			combinations.defines);
	}
}


void ShaderCompiler::compile(const char* path)
{
	char basename[Lumix::MAX_PATH_LENGTH];
	Lumix::PathUtils::getBasename(basename, Lumix::lengthOf(basename), path);
	if (Lumix::findSubstring(basename, "_"))
	{
		Lumix::g_log_error.log("Editor") << "Shaders with underscore are not supported. " << path
										 << " will not be compiled.";
		return;
	}

	Lumix::StaticString<Lumix::MAX_PATH_LENGTH> compiled_dir(
		m_editor.getEngine().getDiskFileDevice()->getBasePath(), "/shaders/compiled");
	if (getRenderer().isOpenGL()) compiled_dir << "_gl";
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

		compileAllPasses(path, false, combinations.fs_local_mask, combinations);
		compileAllPasses(path, true, combinations.vs_local_mask, combinations);
	}
	else
	{
		Lumix::g_log_error.log("Editor") << "Could not open " << path;
	}
}


void ShaderCompiler::compileAll(bool wait)
{
	if (m_is_compiling)
	{
		if(wait) this->wait();
		return;
	}

	Lumix::StaticString<Lumix::MAX_PATH_LENGTH> compiled_dir(
		m_editor.getEngine().getDiskFileDevice()->getBasePath(), "/shaders/compiled");
	if (getRenderer().isOpenGL()) compiled_dir << "_gl";
	if (!PlatformInterface::makePath(compiled_dir))
	{
		if (!PlatformInterface::dirExists(compiled_dir))
		{
			Lumix::messageBox("Could not create directory shaders/compiled. Please create it and "
				"restart the editor");
		}
		return;
	}

	m_is_compiling = true;
	m_app.getAssetBrowser()->enableUpdate(!m_is_compiling);

	PlatformInterface::FileInfo info;
	auto* iter = PlatformInterface::createFileIterator("shaders", m_editor.getAllocator());

	auto& fs = m_editor.getEngine().getFileSystem();
	while (PlatformInterface::getNextFile(iter, &info))
	{
		if (!Lumix::PathUtils::hasExtension(info.filename, "shd")) continue;

		const char* shd_path = Lumix::StaticString<Lumix::MAX_PATH_LENGTH>("shaders/", info.filename);
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

			compileAllPasses(shd_path, false, combinations.fs_local_mask, combinations);
			compileAllPasses(shd_path, true, combinations.vs_local_mask, combinations);

			fs.close(*file);
		}
		else
		{
			Lumix::g_log_error.log("Editor") << "Could not open " << shd_path;
		}
	}

	PlatformInterface::destroyFileIterator(iter);

	if(wait) this->wait();
}
