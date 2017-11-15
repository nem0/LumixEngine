#include "shader_compiler.h"
#include "editor/asset_browser.h"
#include "editor/file_system_watcher.h"
#include "editor/log_ui.h"
#include "editor/platform_interface.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/os_file.h"
#include "engine/fs/resource_file_device.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/mt/thread.h"
#include "engine/path.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/system.h"
#include "renderer/renderer.h"
#include <cstdio>
#include <stb/mf_resource.h>


namespace bgfx
{ 
	int compileShader(int _argc, const char* _argv[]);
	typedef void(*UserErrorFn)(void*, const char*, va_list);
	void setShaderCErrorFunction(UserErrorFn fn, void* user_ptr);
}


namespace Lumix
{


static const ResourceType SHADER_TYPE("shader");
static const ResourceType SHADER_BINARY_TYPE("shader_binary");


ShaderCompiler::ShaderCompiler(StudioApp& app, LogUI& log_ui)
	: m_app(app)
	, m_editor(app.getWorldEditor())
	, m_log_ui(log_ui)
	, m_dependencies(m_editor.getAllocator())
	, m_to_compile(m_editor.getAllocator())
	, m_hooked_files(m_editor.getAllocator())
	, m_to_reload(m_editor.getAllocator())
	, m_shd_files(m_editor.getAllocator())
	, m_changed_files(m_editor.getAllocator())
	, m_mutex(false)
	, m_load_hook(*m_editor.getEngine().getResourceManager().get(SHADER_TYPE), *this)
{
	JobSystem::JobDecl job;
	job.task = [](void* data) { ((ShaderCompiler*)data)->compileTask(); };
	job.data = this;
	JobSystem::runJobs(&job, 1, nullptr);
	m_is_opengl = bgfx::getRendererType() == bgfx::RendererType::OpenGL || bgfx::getRendererType() == bgfx::RendererType::OpenGLES;

	m_notifications_id = -1;
	m_empty_queue = 1;

	m_watcher = FileSystemWatcher::create("pipelines", m_editor.getAllocator());
	m_watcher->getCallback().bind<ShaderCompiler, &ShaderCompiler::onFileChanged>(this);
	
	findShaderFiles("pipelines/");
	parseDependencies();
	//makeUpToDate(false);

	Engine& engine = m_editor.getEngine();
	ResourceManagerBase* shader_manager = engine.getResourceManager().get(SHADER_BINARY_TYPE);
	shader_manager->setLoadHook(m_load_hook);
}


ShaderCompiler::LoadHook::LoadHook(ResourceManagerBase& manager, ShaderCompiler& compiler)
	: ResourceManagerBase::LoadHook(manager)
	, m_compiler(compiler)
{
}


void ShaderCompiler::compileTask()
{
	m_job_runnig = 1;
	for (;;)
	{
		JobSystem::wait(&m_empty_queue);
		if (m_job_exit_request) break;

		m_app.getAssetBrowser().enableUpdate(false);

		{
			MT::SpinLock lock(m_mutex);
			m_compiling = m_to_compile.back();
			m_to_compile.pop();
			m_empty_queue = m_to_compile.empty();
		}
		compile(m_compiling, false);

		if(m_empty_queue) m_app.getAssetBrowser().enableUpdate(true);
	}
	m_job_runnig = 0;
}


bool ShaderCompiler::LoadHook::onBeforeLoad(Resource& resource)
{
	if (PlatformInterface::fileExists(resource.getPath().c_str())) return false;

	char source_path[MAX_PATH_LENGTH];
	PathUtils::getBasename(source_path, lengthOf(source_path), resource.getPath().c_str());
	if (m_compiler.getSourceFromBinaryBasename(source_path, lengthOf(source_path), source_path))
	{
		MT::SpinLock lock(m_compiler.m_mutex);
		if (m_compiler.m_compiling != source_path 
			&& m_compiler.m_to_reload.find([&source_path](const string& str) { return str == source_path; }) < 0
			&& m_compiler.m_to_compile.find([&source_path](const auto& str) { return str == source_path; }) < 0)
		{
			m_compiler.m_to_compile.emplace(source_path);
			m_compiler.m_empty_queue = 0;
		}
		m_compiler.m_hooked_files.push(&resource);
		m_compiler.m_hooked_files.removeDuplicates();
	}
	
	return true;
}


void ShaderCompiler::queueCompile(const char* path)
{
	MT::SpinLock lock(m_mutex);
	m_to_compile.emplace(path);
	m_to_compile.removeDuplicates();
	m_empty_queue = 0;
}



bool ShaderCompiler::getSourceFromBinaryBasename(char* out, int max_size, const char* binary_basename)
{
	char shd_basename[MAX_PATH_LENGTH];
	char* cout = shd_basename;
	const char* cin = binary_basename;
	while (*cin && *cin != '_')
	{
		*cout = *cin;
		++cout;
		++cin;
	}
	*cout = '\0';

	for (string& shd_path : m_shd_files)
	{
		char tmp[MAX_PATH_LENGTH];
		PathUtils::getBasename(tmp, lengthOf(tmp), shd_path.c_str());
		if (equalStrings(tmp, shd_basename))
		{
			copyString(out, max_size, shd_path.c_str());
			return true;
		}
	}
	g_log_info.log("Editor") << binary_basename << " binary shader has no source code";
	return false;
}


Renderer& ShaderCompiler::getRenderer()
{
	IPlugin* plugin = m_editor.getEngine().getPluginManager().getPlugin("renderer");
	ASSERT(plugin);
	return static_cast<Renderer&>(*plugin);
}


static void getShaderPath(const char* shd_path, char* out, bool vertex)
{
	PathUtils::FileInfo file_info(shd_path);
	copyString(out, MAX_PATH_LENGTH, file_info.m_dir);
	catString(out, MAX_PATH_LENGTH, file_info.m_basename);
	catString(out, MAX_PATH_LENGTH, vertex ? "_vs.sc" : "_fs.sc");
}


bool ShaderCompiler::isChanged(const ShaderCombinations& combinations,
	const char* bin_base_path,
	const char* shd_path) const
{
	char tmp[MAX_PATH_LENGTH];
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
			StaticString<MAX_PATH_LENGTH>(bin_base_path, combinations.passes[i]);
		for (int j = 0; j < 1 << lengthOf(combinations.defines); ++j)
		{
			if ((j & (~combinations.vs_local_mask[i])) == 0)
			{
				const char* vs_bin_info =
					StaticString<MAX_PATH_LENGTH>(pass_path, j, "_vs.shb");
				if (!PlatformInterface::fileExists(vs_bin_info) ||
					PlatformInterface::getLastModified(vs_bin_info) < shd_last_modified)
				{
					return true;
				}
			}
			if ((j & (~combinations.fs_local_mask[i])) == 0)
			{
				const char* fs_bin_info =
					StaticString<MAX_PATH_LENGTH>(pass_path, j, "_fs.shb");
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


void ShaderCompiler::findShaderFiles(const char* src_dir)
{
	auto* iter = PlatformInterface::createFileIterator(src_dir, m_editor.getAllocator());
	PlatformInterface::FileInfo info;

	while (getNextFile(iter, &info))
	{
		if (info.is_directory && info.filename[0] != '.')
		{
			StaticString<MAX_PATH_LENGTH> child(src_dir, "/", info.filename);
			findShaderFiles(child);
		}

		if (!PathUtils::hasExtension(info.filename, "shd")) continue;

		StaticString<MAX_PATH_LENGTH> shd_path(src_dir, "/", info.filename);
		char normalized_path[MAX_PATH_LENGTH];
		PathUtils::normalize(shd_path, normalized_path, lengthOf(normalized_path));
		m_shd_files.emplace(normalized_path, m_editor.getAllocator());
	}

	PlatformInterface::destroyFileIterator(iter);
}


void ShaderCompiler::makeUpToDate(bool wait)
{
	if (!m_empty_queue)
	{
		if (wait) this->wait();
		return;
	}
	if (m_shd_files.empty()) return;

	StaticString<MAX_PATH_LENGTH> pipelines_dir(
		m_editor.getEngine().getDiskFileDevice()->getBasePath(), "/pipelines");
	StaticString<MAX_PATH_LENGTH> compiled_dir(pipelines_dir, "/compiled");
	if (m_is_opengl) compiled_dir << "_gl";
	if (!PlatformInterface::dirExists(pipelines_dir) && !PlatformInterface::makePath(pipelines_dir))
	{
		messageBox("Could not create directory pipelines. Please create it and "
			"restart the editor");
		return;
	}
	if (!PlatformInterface::dirExists(compiled_dir) && !PlatformInterface::makePath(compiled_dir))
	{
		messageBox("Could not create directory pipelines/compiled. Please create it and "
			"restart the editor");
		return;
	}

	auto& fs = m_editor.getEngine().getFileSystem();
	for (string& shd_path : m_shd_files)
	{
		auto* file = fs.open(fs.getDiskDevice(), Path(shd_path.c_str()), FS::Mode::OPEN_AND_READ);

		if (!file)
		{
			g_log_error.log("Editor") << "Could not open " << shd_path;
			continue;
		}

		int len = (int)file->size();
		Array<char> data(m_editor.getAllocator());
		data.resize(len + 1);
		file->read(&data[0], len);
		data[len] = 0;
		fs.close(*file);

		ShaderCombinations combinations;
		Shader::getShaderCombinations(shd_path.c_str(), getRenderer(), &data[0], &combinations);

		char basename[MAX_PATH_LENGTH];
		PathUtils::getBasename(basename, lengthOf(basename), shd_path.c_str());
		StaticString<MAX_PATH_LENGTH> bin_base_path(compiled_dir, "/", basename, "_");
		if (isChanged(combinations, bin_base_path, shd_path.c_str()))
		{
			queueCompile(shd_path.c_str());
		}
	}

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
				char basename[MAX_PATH_LENGTH];
				PathUtils::getBasename(basename, sizeof(basename), bin.c_str());
				char tmp[MAX_PATH_LENGTH];
				if (getSourceFromBinaryBasename(tmp, sizeof(tmp), basename))
				{
					queueCompile(tmp);
				}
			}
		}
	}

	if (wait) this->wait();
}


void ShaderCompiler::onFileChanged(const char* path)
{
	char ext[10];
	PathUtils::getExtension(ext, sizeof(ext), path);
	if (!equalStrings("sc", ext) && !equalStrings("shd", ext) && !equalStrings("sh", ext)) return;

	char tmp[MAX_PATH_LENGTH];
	copyString(tmp, "pipelines/");
	catString(tmp, path);
	char normalized[MAX_PATH_LENGTH];
	PathUtils::normalize(tmp, normalized, lengthOf(normalized));
	MT::SpinLock lock(m_mutex);
	m_changed_files.push(string(normalized, m_editor.getAllocator()));
}


static bool readLine(FS::IFile* file, char* out, int max_size)
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
	StaticString<30> compiled_dir("pipelines/compiled", m_is_opengl ? "_gl" : "");
	auto* iter = PlatformInterface::createFileIterator(compiled_dir, m_editor.getAllocator());

	auto& fs = m_editor.getEngine().getFileSystem();
	PlatformInterface::FileInfo info;
	while (PlatformInterface::getNextFile(iter, &info))
	{
		if (!PathUtils::hasExtension(info.filename, "d")) continue;

		auto* file = fs.open(fs.getDiskDevice(),
			Path(StaticString<MAX_PATH_LENGTH>(compiled_dir, "/", info.filename)),
			FS::Mode::READ | FS::Mode::OPEN);
		if (!file)
		{
			g_log_error.log("Editor") << "Could not open " << info.filename;
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
			char* trimmed_line = trimmed(line);
			char* c = trimmed_line;
			while (*c)
			{
				if (*c == ' ') break;
				++c;
			}
			*c = '\0';

			addDependency(trimmed_line, first_line);
		}

		char basename[MAX_PATH_LENGTH];
		char src[MAX_PATH_LENGTH];
		PathUtils::getBasename(basename, sizeof(basename), first_line);
		if (getSourceFromBinaryBasename(src, sizeof(src), basename))
		{
			addDependency(src, first_line);
		}

		fs.close(*file);
	}

	PlatformInterface::destroyFileIterator(iter);
}


void ShaderCompiler::addDependency(const char* ckey, const char* cvalue)
{
	char tmp[MAX_PATH_LENGTH];
	PathUtils::normalize(ckey, tmp, lengthOf(tmp));
	string key(tmp, m_editor.getAllocator());

	int idx = m_dependencies.find(key);
	if (idx < 0)
	{
		idx = m_dependencies.insert(key, Array<string>(m_editor.getAllocator()));
	}
	m_dependencies.at(idx).emplace(cvalue, m_editor.getAllocator());
}


ShaderCompiler::~ShaderCompiler()
{
	m_job_exit_request = true;
	m_empty_queue = 0;
	JobSystem::wait(&m_job_runnig);
	FileSystemWatcher::destroy(m_watcher);
}


void ShaderCompiler::reloadShaders()
{
	m_to_reload.removeDuplicates();

	auto shader_manager = m_editor.getEngine().getResourceManager().get(SHADER_TYPE);
	for (string& shd_path : m_to_reload)
	{
		bool any_hooked = false;
		for (int i = m_hooked_files.size() - 1; i >= 0; --i)
		{
			Resource* res = m_hooked_files[i];
			const char* shader_path = ((ShaderBinary*)res)->m_shader->getPath().c_str();
			if (equalStrings(shader_path, shd_path.c_str()))
			{
				any_hooked = true;
				m_load_hook.continueLoad(*res);
				m_hooked_files.eraseFast(i);
			}
		}

		if(!any_hooked) shader_manager->reload(Path(shd_path.c_str()));
	}

	m_to_reload.clear();
}


void ShaderCompiler::updateNotifications()
{
	if (!m_empty_queue && m_notifications_id < 0)
	{
		m_notifications_id = m_log_ui.addNotification("Compiling shaders...");
	}

	if (!m_empty_queue && m_notifications_id != -1)
	{
		m_log_ui.setNotificationTime(m_notifications_id, 3.0f);
	}
	if(m_empty_queue) m_notifications_id = -1;
}


static void errorCallback(void*, const char* format, va_list args)
{
	char tmp[4096];
	vsnprintf(tmp, lengthOf(tmp), format, args);
	g_log_error.log("Renderer") << tmp;
}


void ShaderCompiler::compilePass(const char* shd_path,
	bool is_vertex_shader,
	const char* pass,
	int define_mask,
	const ShaderCombinations::Defines& all_defines,
	bool debug)
{
	const char* base_path = m_editor.getEngine().getDiskFileDevice()->getBasePath();

	for (int mask = 0; mask < 1 << lengthOf(all_defines); ++mask)
	{
		if ((mask & (~define_mask)) == 0)
		{
			PathUtils::FileInfo shd_file_info(shd_path);
			StaticString<MAX_PATH_LENGTH> source_path(
				"", shd_file_info.m_dir, shd_file_info.m_basename, is_vertex_shader ? "_vs.sc" : "_fs.sc");
			StaticString<MAX_PATH_LENGTH> out_path(base_path);
			out_path << "/pipelines/compiled" << (m_is_opengl ? "_gl/" : "/");
			out_path << shd_file_info.m_basename << "_" << pass;
			out_path << mask << (is_vertex_shader ? "_vs.shb" : "_fs.shb");

			const char* args_array[19];
			args_array[0] = "-f";
			args_array[1] = source_path;
			args_array[2] = "-o";
			args_array[3] = out_path;
			args_array[4] = "--depends";
			args_array[5] = "-i";
			StaticString<MAX_PATH_LENGTH> include(base_path, "/pipelines/");
			args_array[6] = include;
			args_array[7] = "--varyingdef";
			StaticString<MAX_PATH_LENGTH> varying(base_path, "/pipelines/varying.def.sc");
			args_array[8] = varying;
			args_array[9] = "--platform";
			if (m_is_opengl)
			{
				args_array[10] = "linux";
				args_array[11] = "--profile";
				args_array[12] = "140";
			}
			else
			{
				args_array[10] = "windows";
				args_array[11] = "--profile";
				args_array[12] = is_vertex_shader ? "vs_4_0" : "ps_4_0";
			}
			args_array[13] = "--type";
			args_array[14] = is_vertex_shader ? "vertex" : "fragment";
			if (debug)
			{
				args_array[15] = "--debug";
				args_array[16] = "--disasm";
			}
			else
			{
				args_array[15] = "-O";
				args_array[16] = "3";
			}
			args_array[17] = "--define";
			StaticString<256> defines(pass, ";");
			for (int i = 0; i < lengthOf(all_defines); ++i)
			{
				if (mask & (1 << i))
				{
					defines << getRenderer().getShaderDefine(all_defines[i]) << ";";
				}
			}
			args_array[18] = defines;
			bgfx::setShaderCErrorFunction(errorCallback, nullptr);
			if (bgfx::compileShader(19, args_array) == EXIT_FAILURE)
			{
				g_log_error.log("Renderer") << "Failed to compile " << source_path << "(" << out_path << "), defines = \"" << defines << "\"";
			}
		}
	}
}


void ShaderCompiler::processChangedFiles()
{
	if (!m_empty_queue) return;

	char changed_file_path[MAX_PATH_LENGTH];
	{
		MT::SpinLock lock(m_mutex);
		if (m_changed_files.empty()) return;

		m_changed_files.removeDuplicates();
		const char* tmp = m_changed_files.back().c_str();
		copyString(changed_file_path, sizeof(changed_file_path), tmp);
		m_changed_files.pop();
	}
	string tmp_string(changed_file_path, m_editor.getAllocator());
	int find_idx = m_dependencies.find(tmp_string);
	if (find_idx < 0)
	{
		int len = stringLength(changed_file_path);
		if (len <= 6) return;
		if (equalStrings(changed_file_path + len - 6, "_fs.sc") ||
			equalStrings(changed_file_path + len - 6, "_vs.sc"))
		{
			copyString(
				changed_file_path + len - 6, lengthOf(changed_file_path) - len + 6, ".shd");
			tmp_string = changed_file_path;
			find_idx = m_dependencies.find(tmp_string);
		}
	}
	if (find_idx >= 0)
	{
		if (PathUtils::hasExtension(changed_file_path, "shd"))
		{
			queueCompile(changed_file_path);
		}
		else
		{
			Array<StaticString<MAX_PATH_LENGTH>> src_list(m_editor.getAllocator());

			for (auto& bin : m_dependencies.at(find_idx))
			{
				char basename[MAX_PATH_LENGTH];
				PathUtils::getBasename(basename, sizeof(basename), bin.c_str());
				char tmp[MAX_PATH_LENGTH];
				if (getSourceFromBinaryBasename(tmp, sizeof(tmp), basename))
				{
					src_list.emplace(tmp);
				}
			}

			src_list.removeDuplicates();

			for (auto& src : src_list)
			{
				queueCompile(src);
			}
		}
	}
}


void ShaderCompiler::wait()
{
	while (!m_empty_queue)
	{
		MT::sleep(5);
	}
}


void ShaderCompiler::update()
{
	PROFILE_FUNCTION();
	updateNotifications();

	processChangedFiles();

	MT::SpinLock lock(m_mutex);
	if (!m_to_reload.empty())
	{
		reloadShaders();
		parseDependencies();
	}
}


void ShaderCompiler::compileAllPasses(const char* path,
	bool is_vertex_shader,
	const int* define_masks,
	const ShaderCombinations& combinations,
	bool debug)
{
	for (int i = 0; i < combinations.pass_count; ++i)
	{
		compilePass(path, is_vertex_shader, combinations.passes[i], define_masks[i], combinations.defines, debug);
	}
}


void ShaderCompiler::compile(const char* path, bool debug)
{
	{
		MT::SpinLock lock(m_mutex);
		m_compiling = path;
	}

	char basename[MAX_PATH_LENGTH];
	PathUtils::getBasename(basename, lengthOf(basename), path);
	if (findSubstring(basename, "_"))
	{
		g_log_error.log("Editor") << "Shaders with underscore are not supported. " << path
			<< " will not be compiled.";
		return;
	}

	StaticString<MAX_PATH_LENGTH> compiled_dir(
		m_editor.getEngine().getDiskFileDevice()->getBasePath(), "/pipelines/compiled");
	if (m_is_opengl) compiled_dir << "_gl";
	if (!PlatformInterface::makePath(compiled_dir))
	{
		if (!PlatformInterface::dirExists(compiled_dir))
		{
			messageBox("Could not create directory pipelines/compiled. Please create it and "
				"restart the editor");
		}
	}

	auto& fs = m_editor.getEngine().getFileSystem();
	auto* file = fs.open(fs.getDiskDevice(), Path(path), FS::Mode::OPEN_AND_READ);
	if (file)
	{
		int size = (int)file->size();
		Array<char> data(m_editor.getAllocator());
		data.resize(size + 1);
		file->read(&data[0], size);
		data[size] = 0;
		fs.close(*file);

		ShaderCombinations combinations;
		Shader::getShaderCombinations(path, getRenderer(), &data[0], &combinations);

		compileAllPasses(path, false, combinations.fs_local_mask, combinations, debug);
		compileAllPasses(path, true, combinations.vs_local_mask, combinations, debug);
	}
	else
	{
		g_log_error.log("Editor") << "Could not open " << path;
	}

	{
		MT::SpinLock lock(m_mutex);
		m_to_reload.emplace(path, m_editor.getAllocator());
		m_compiling = "";
	}
}


} // namespace Lumix
