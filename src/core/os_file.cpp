#include "core/os_file.h"
#include "core/lux.h"

#include <assert.h>
#include <windows.h>

namespace Lux
{
	namespace FS
	{
		struct OsFileImpl
		{
			HANDLE m_file;
		};

		OsFile::OsFile()
		{
		}

		OsFile::~OsFile()
		{
			ASSERT(NULL == m_impl);
		}

		bool OsFile::open(const char* path, Mode mode)
		{
			//TODO: normalize path
			HANDLE hnd = INVALID_HANDLE_VALUE;
			if(Mode::OPEN & mode)
			{
				hnd = ::CreateFile(path, 
					Mode::WRITE & mode ? GENERIC_WRITE : 0 | Mode::READ & mode ? GENERIC_READ : 0,
					Mode::WRITE & mode ? 0 : FILE_SHARE_READ,
					NULL,
					OPEN_EXISTING,
					FILE_ATTRIBUTE_NORMAL,
					NULL);
								DWORD error = ::GetLastError();
								error = ::GetLastError();

			}
			else if(Mode::OPEN_OR_CREATE & mode)
			{
				hnd = ::CreateFile(path, 
					Mode::WRITE & mode ? GENERIC_WRITE : 0 | Mode::READ & mode ? GENERIC_READ : 0,
					Mode::WRITE & mode ? 0 : FILE_SHARE_READ,
					NULL,
					OPEN_ALWAYS,
					FILE_ATTRIBUTE_NORMAL,
					NULL);
			}
			else if(Mode::RECREATE & mode)
			{
				hnd = ::CreateFile(path, 
					Mode::WRITE & mode ? GENERIC_WRITE : 0 | Mode::READ & mode ? GENERIC_READ : 0,
					Mode::WRITE & mode ? 0 : FILE_SHARE_READ,
					NULL,
					CREATE_ALWAYS,
					FILE_ATTRIBUTE_NORMAL,
					NULL);
			}
			else
			{
				ASSERT(false);
				return false;
			}

			if(INVALID_HANDLE_VALUE != hnd)
			{
				OsFileImpl* impl = LUX_NEW(OsFileImpl)(); //TODO: lock-free free list
				impl->m_file = hnd;
				m_impl = impl;

				return true;
			}
			ASSERT(false);
			return false;
		}

		void OsFile::close()
		{
			ASSERT(NULL != m_impl);

			::CloseHandle(m_impl->m_file);
			LUX_DELETE(m_impl);
			m_impl = NULL;
		}

		bool OsFile::write(const void* data, size_t size)
		{
			ASSERT(NULL != m_impl);
			size_t written = 0;
			::WriteFile(m_impl->m_file, data, size, (LPDWORD)&written, NULL);
			return size == written;
		}

		bool OsFile::read(void* data, size_t size)
		{
			ASSERT(NULL != m_impl);
			size_t readed = 0;
			::ReadFile(m_impl->m_file, data, size, (LPDWORD)&readed, NULL);
			return size == readed;
		}

		int OsFile::size()
		{
			ASSERT(NULL != m_impl);
			return ::GetFileSize(m_impl->m_file, 0);
		}

		int OsFile::pos() const
		{
			ASSERT(NULL != m_impl);
			return ::SetFilePointer(m_impl->m_file, 0, NULL, FILE_CURRENT);
		}

		int OsFile::seek(SeekMode base, int pos)
		{
			ASSERT(NULL != m_impl);
			int dir = 0;
			switch(base)
			{
			case SeekMode::BEGIN:
				dir = FILE_BEGIN;
				break;
			case SeekMode::END:
				dir = FILE_END;
				break;
			case SeekMode::CURRENT:
				dir = FILE_CURRENT;
				break;
			}

			return ::SetFilePointer(m_impl->m_file, pos, NULL, dir);
		}

		void OsFile::writeEOF()
		{
			ASSERT(NULL != m_impl);
			::SetEndOfFile(m_impl->m_file);
		}
	} // ~namespace FS
} // ~namespace Lux
