#include "editor/script_compiler.h"
#include <cstdio>
#include "core/crc32.h"
#include "core/log.h"


void ScriptCompiler::compileAll()
{
	WIN32_FIND_DATA find_data;
	HANDLE h = FindFirstFile("scripts\\*.cpp", &find_data);
	if(h == INVALID_HANDLE_VALUE)
	{
		return;
	}
	do
	{
		char path[260];
		sprintf(path, "scripts\\%s", find_data.cFileName);
		compile(path);
	}
	while(FindNextFile(h, &find_data));
}


void ScriptCompiler::compile(const char path[])
{
	STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory( &si, sizeof(si) );
    si.cb = sizeof(si);
    ZeroMemory( &pi, sizeof(pi) );
	char cmd_line[255];
	HANDLE read_pipe, write_pipe;

	SECURITY_ATTRIBUTES saAttr; 
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE; 
	saAttr.lpSecurityDescriptor = NULL; 

	CreatePipe(&read_pipe, &write_pipe, &saAttr, 0);
	SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
	//si.hStdInput = read_pipe;
	si.hStdOutput = write_pipe;
	si.hStdError = write_pipe;
	si.dwFlags |= STARTF_USESTDHANDLES;
	sprintf(cmd_line, "/C scripts\\compile.bat %s", path);
    if ( CreateProcess("C:\\windows\\system32\\cmd.exe",     // Application name
        cmd_line,
        NULL,
        NULL,
        TRUE,
        0,
        NULL,
        m_base_path.c_str(),
        &si,
        &pi) == TRUE)
	{
		Process* p = LUMIX_NEW(Process);
		p->m_handle = pi.hProcess;
		p->m_path = path;
		p->m_pipe = read_pipe;
		p->m_write_pipe = write_pipe;
		m_processes.push(p);
	}
	else
	{
		ASSERT(false);
	}
}


ScriptCompiler::Status ScriptCompiler::getStatus(const char* path)
{
	Lumix::Map<uint32_t, Status>::iterator iter = m_status.find(crc32(path));
	return iter == m_status.end() ? UNKNOWN : iter.second();
}


void ScriptCompiler::checkFinished()
{
	for(int i = m_processes.size() - 1; i >= 0; --i)
	{
		DWORD code;
		if(GetExitCodeProcess(m_processes[i]->m_handle, &code))
		{
			if(code != STILL_ACTIVE)
			{
				Process* p = m_processes[i];
				m_processes.eraseFast(i);
				m_delegates.invoke(p->m_path.c_str(), code);
				char buf[513];
				DWORD read;
				if(code != 0)
				{
					static Lumix::string text;
					text = "";
					do
					{
						ReadFile(p->m_pipe, buf, 512, &read, NULL);
						buf[read] = '\0';
						text += buf;
					}
					while(read == 512);
					Lumix::g_log_info.log("compile script", text.c_str());
				}
				m_status[crc32(p->m_path.c_str())] = code == 0 ? SUCCESS : FAILURE;
				CloseHandle(p->m_pipe);
				CloseHandle(p->m_write_pipe);
				LUMIX_DELETE(p);
			}
		}
	}
}