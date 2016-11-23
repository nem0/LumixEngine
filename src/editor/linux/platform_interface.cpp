#include "platform_interface.h"
#include "engine/command_line_parser.h"
#include "engine/iallocator.h"
#include "engine/log.h"
#include "engine/string.h"
#include "imgui/imgui.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <X11/Xlib.h>


namespace PlatformInterface
{

struct FileIterator
{
};


FileIterator* createFileIterator(const char* path, Lumix::IAllocator& allocator)
{
	return (FileIterator*)opendir(path);
}


void destroyFileIterator(FileIterator* iterator)
{
	closedir((DIR*)iterator);
}


bool getNextFile(FileIterator* iterator, FileInfo* info)
{
	if (!iterator) return false;

	auto* dir = (DIR*)iterator;
	auto* dir_ent = readdir(dir);
	if (!dir_ent) return false;

	info->is_directory = dir_ent->d_type == DT_DIR;
	Lumix::copyString(info->filename, dir_ent->d_name);
	return true;
}


void getCurrentDirectory(char* buffer, int buffer_size)
{
	if(!getcwd(buffer, buffer_size))
	{
		buffer[0] = 0;
	}
}


struct Process
{
	explicit Process(Lumix::IAllocator& allocator)
		: allocator(allocator)
	{
	}

	Lumix::IAllocator& allocator;
	pid_t handle;
	int pipes[2];
	int exit_code;
};


bool isProcessFinished(Process& process)
{
	if (process.handle == -1) return true;
	int status;
	int success = waitpid(process.handle, &status, WNOHANG);
	if (success == 0) return false;
	process.exit_code = WEXITSTATUS(status);
	process.handle = -1;
	return true;
}


int getProcessExitCode(Process& process)
{
	if (process.handle != -1)
	{
		int status;
		int success = waitpid(process.handle, &status, WNOHANG);
		ASSERT(success != -1 && success != 0);
		process.exit_code = WEXITSTATUS(status);
		process.handle = -1;
	}
	return process.exit_code;
}


void destroyProcess(Process& process)
{
	if (process.handle != -1)
	{
		kill(process.handle, SIGKILL);
		int status;
		waitpid(process.handle, &status, 0);
	}
	LUMIX_DELETE(process.allocator, &process);
}


Process* createProcess(const char* cmd, const char* args, Lumix::IAllocator& allocator)
{
	struct stat sb;
	if(stat (cmd, &sb) == 0 && (sb.st_mode & S_IXUSR) == 0)
	{
		Lumix::g_log_error.log("Editor") << cmd << " is not executable.";
		return nullptr;
	}


	auto* process = LUMIX_NEW(allocator, Process)(allocator);
	int res = pipe(process->pipes);
	ASSERT(res == 0);

	process->handle = fork();
	ASSERT(process->handle != -1);
	if (process->handle == 0)
	{
		dup2(process->pipes[1], STDOUT_FILENO);
		dup2(process->pipes[1], STDERR_FILENO);
		close(process->pipes[0]);
		close(process->pipes[1]);

		Lumix::CommandLineParser parser(args);
		char* args_array[256];
		char** i = args_array;
		*i = (char*)cmd;
		++i;
		while (i - args_array < Lumix::lengthOf(args_array) - 2 && parser.next())
		{
			char tmp[1024];
			parser.getCurrent(tmp, Lumix::lengthOf(tmp));
			int len = Lumix::stringLength(tmp) + 1;
			auto* copy = (char*)malloc(len);
			Lumix::copyString(copy, len, tmp);
			*i = copy;
			++i;
		}
		*i = nullptr;

		execv(cmd, args_array);
		_exit(-1);
	}
	else
	{
		int fl = fcntl(process->pipes[0], F_GETFL, 0);
		fcntl(process->pipes[0], F_SETFL, fl | O_NONBLOCK);
		close(process->pipes[1]);
	}
	return process;
}


int getProcessOutput(Process& process, char* buf, int buf_size)
{
	if(buf_size <= 0) return -1;
	buf[0] = 0;
	size_t ret = read(process.pipes[0], buf, buf_size);
	return (int)ret;
}


bool getSaveFilename(char* out, int max_size, const char* filter, const char* default_extension)
{
	/*OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = out;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = max_size;
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrDefExt = default_extension;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_NONETWORKBUTTON;

	return GetSaveFileName(&ofn) == TRUE;*/
	return false;
}


bool getOpenFilename(char* out, int max_size, const char* filter, const char* starting_file)
{
	/*OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	if (starting_file)
	{
		char* to = out;
		for (const char* from = starting_file; *from; ++from, ++to)
		{
			if (to - out > max_size - 1) break;
			*to = *to == '/' ? '\\' : *from;
		}
		*to = '\0';
	}
	else
	{
		out[0] = '\0';
	}
	ofn.lpstrFile = out;
	ofn.nMaxFile = max_size;
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = nullptr;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_NONETWORKBUTTON;

	return GetOpenFileName(&ofn) == TRUE;*/
	return false;
}


bool getOpenDirectory(char* out, int max_size, const char* starting_dir)
{
	/*bool ret = false;
	IFileDialog *pfd;
	if (SUCCEEDED(CoCreateInstance(
		CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
	{
		if (starting_dir)
		{
			PIDLIST_ABSOLUTE pidl;
			WCHAR wstarting_dir[MAX_PATH];
			WCHAR* wc = wstarting_dir;
			for (const char* c = starting_dir; *c && wc - wstarting_dir < MAX_PATH - 1; ++c, ++wc)
			{
				*wc = *c == '/' ? '\\' : *c;
			}
			*wc = 0;

			HRESULT hresult = ::SHParseDisplayName(wstarting_dir, 0, &pidl, SFGAO_FOLDER, 0);
			if (SUCCEEDED(hresult))
			{
				IShellItem *psi;
				hresult = ::SHCreateShellItem(NULL, NULL, pidl, &psi);
				if (SUCCEEDED(hresult))
				{
					pfd->SetFolder(psi);
				}
				ILFree(pidl);
			}
		}

		DWORD dwOptions;
		if (SUCCEEDED(pfd->GetOptions(&dwOptions)))
		{
			pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
		}
		if (SUCCEEDED(pfd->Show(NULL)))
		{
			IShellItem* psi;
			if (SUCCEEDED(pfd->GetResult(&psi)))
			{
				WCHAR* tmp;
				if (SUCCEEDED(psi->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &tmp)))
				{
					char* c = out;
					while (*tmp && c - out < max_size - 1)
					{
						*c = (char)*tmp;
						++c;
						++tmp;
					}
					*c = '\0';
					ret = true;
				}
				psi->Release();
			}
		}
		pfd->Release();
	}
	return ret;*/
	return false;
}


bool shellExecuteOpen(const char* path)
{
	return system(path) == 0;
}


bool deleteFile(const char* path)
{
	return unlink(path) == 0;
}


bool moveFile(const char* from, const char* to)
{
	return rename(from, to) == 0;
}


size_t getFileSize(const char* path)
{
	struct stat tmp;
	stat(path, &tmp);
	return tmp.st_size;
}


bool fileExists(const char* path)
{
	struct stat tmp;
	return ((stat(path, &tmp) == 0) && (((tmp.st_mode) & S_IFMT) != S_IFDIR));
}


bool dirExists(const char* path)
{
	struct stat tmp;
	return ((stat(path, &tmp) == 0) && (((tmp.st_mode) & S_IFMT) == S_IFDIR));
}


Lumix::u64 getLastModified(const char* file)
{
	struct stat tmp;
	Lumix::u64 ret = 0;
	ret = tmp.st_mtim.tv_sec * 1000 + Lumix::u64(tmp.st_mtim.tv_nsec / 1000000);
	return ret;
}


bool makePath(const char* path)
{
	return mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0;
}


void copyToClipboard(const char* text)
{
	//ASSERT(false); // TODO
}


void setWindow(SDL_Window* window)
{
	//ASSERT(false); // TODO
}


void clipCursor(int x, int y, int w, int h)
{
	//ASSERT(false); // TODO
}


void unclipCursor()
{
	ASSERT(false); // TODO
}


} // namespace PlatformInterface

