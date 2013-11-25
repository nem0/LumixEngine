#include "task.h"
#include <Windows.h>


struct TaskImpl
{
	HANDLE m_handle;
	DWORD m_thread_id;
};


static DWORD WINAPI threadFunction(LPVOID task)
{
	return static_cast<Task*>(task)->task();
}


Task::Task()
{
	m_implementation = new TaskImpl();
	m_implementation->m_handle = NULL;
}


Task::~Task()
{
	delete m_implementation;
}


bool Task::create()
{
	m_implementation->m_handle = CreateThread(NULL, 0, threadFunction, this, CREATE_SUSPENDED, &m_implementation->m_thread_id);
	return m_implementation->m_handle != NULL;
}


bool Task::run()
{
	return ResumeThread(m_implementation->m_handle) != -1;
}


bool Task::destroy()
{
	return true;
}
