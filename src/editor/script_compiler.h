#pragma once


#include <Windows.h>
#include "core/delegate_list.h"
#include "core/map.h"
#include "core/array.h"
#include "core/string.h"


class ScriptCompiler
{
	public:
		typedef Lumix::DelegateList<void (const char*, uint32_t)> CompileCallbacks;
		enum Status
		{
			UNKNOWN,
			NOT_COMPILED,
			SUCCESS,
			FAILURE
		};

	public:
		void compile(const char path[]);
		CompileCallbacks& onCompile() { return m_delegates; }
		void compileAll();
		void setBasePath(const char* path) { m_base_path = path; }
		void checkFinished();
		bool isEmpty() const { return m_processes.empty(); }
		Status getStatus(const char* path);

	private:
		struct Process
		{
			HANDLE m_handle;
			HANDLE m_pipe;
			HANDLE m_write_pipe;
			Lumix::string m_path;
		};

	private:
		CompileCallbacks m_delegates;
		Lumix::string m_base_path;
		Lumix::Array<Process*> m_processes;
		Lumix::Map<uint32_t, Status> m_status;
};