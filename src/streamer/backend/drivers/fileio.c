/*

Copyright (c) 2006-2010 Jesper Svennevid

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/
#include "fileio.h"
#include "../backend.h"

#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(_IOP)
#include "../iop/irx_imports.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#if defined(STREAMER_UNIX)
#include <sys/types.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#if defined(_WIN32)
static HANDLE s_handles[STREAMER_MAX_FILEHANDLES];
#endif

IODriver* FileIo_Create(const char* root)
{
#if defined(_IOP)
	FileIoDriver* driver = AllocSysMemory(ALLOC_FIRST, sizeof(FileIoDriver),0);
#else
	FileIoDriver* driver = malloc(sizeof(FileIoDriver));
#if defined(_WIN32)
	int i;
#endif
#endif

	driver->m_interface.destroy = FileIo_Destroy;
	driver->m_interface.open = FileIo_Open;
	driver->m_interface.close = FileIo_Close;
	driver->m_interface.read = FileIo_Read;
	driver->m_interface.lseek = FileIo_LSeek;

	driver->m_interface.dopen = 0;
	driver->m_interface.dclose = 0;
	driver->m_interface.dread = 0;

	driver->m_interface.align = 0;

	strcpy(driver->m_root,root); // TODO: overflow check

#if defined(_WIN32)
	for (i = 0; i < STREAMER_MAX_FILEHANDLES; ++i)
	{
		s_handles[i] = INVALID_HANDLE_VALUE;
	}
#endif

	STREAMER_PRINTF(("FileIo: Driver created\n"));
	return &(driver->m_interface);
}

void FileIo_Destroy(struct IODriver* driver)
{
#if defined(_IOP)
	FreeSysMemory(driver);
#else
	free(driver);
#endif

	STREAMER_PRINTF(("FileIo: Driver destroyed\n"));
}

int FileIo_Open(struct IODriver* driver, const char* filename, StreamerOpenMode mode)
{
	FileIoDriver* local = (FileIoDriver*)driver;
#if defined(_WIN32)
	int hindex = -1, i;
	static int access[] = { GENERIC_READ, GENERIC_WRITE };
	static int share[] = { FILE_SHARE_READ, 0 };
	static int disposition[] = { OPEN_EXISTING, CREATE_ALWAYS };
#endif

	// TODO: if we're reading from cdrom, we need to re-parse the filename

	char buffer[256];
	strcpy(buffer,local->m_root);
	strcat(buffer,filename); // TODO: overflow check

	STREAMER_PRINTF(("FileIo: open(\"%s\", %d)\n", filename, mode));

#if defined(_WIN32)
	for (i = 0; i < STREAMER_MAX_FILEHANDLES; ++i)
	{
		if (s_handles[i] == INVALID_HANDLE_VALUE)
		{
			hindex = i;
			break;
		}
	}

	if (hindex < 0)
	{
		STREAMER_PRINTF(("FileIo: Out of available file handles\n"));
		return -1;
	}

	s_handles[hindex] = CreateFile(buffer, access[mode], share[mode], 0, disposition[mode], FILE_ATTRIBUTE_NORMAL, 0);
	if (s_handles[hindex] == INVALID_HANDLE_VALUE)
	{
		STREAMER_PRINTF(("FileIo: Could not open file\n"));
		return -1;
	}

	return hindex;
#else
	return open(buffer,(mode == StreamerOpenMode_Read) ? O_RDONLY : 0);
#endif
}

int FileIo_Close(struct IODriver* driver, int fd)
{
	STREAMER_PRINTF(("FileIo: close(%d)\n", fd));

#if defined(_WIN32)
	if ((fd < 0) || (fd >= STREAMER_MAX_FILEHANDLES))
	{
		STREAMER_PRINTF(("FileIo: Invalid file handle\n"));
		return -1;
	}

	if (s_handles[fd] == INVALID_HANDLE_VALUE)
	{
		STREAMER_PRINTF(("FileIo: File handle not open\n"));
		return -1;
	}

	CloseHandle(s_handles[fd]);
	s_handles[fd] = INVALID_HANDLE_VALUE;
	return 0;
#else
	return close(fd);
#endif
}

int FileIo_Read(struct IODriver* driver, int fd, void* buffer, unsigned int length)
{
#if defined(_WIN32)
	DWORD bytesRead;

	if ((fd < 0) || (fd >= STREAMER_MAX_FILEHANDLES))
	{
		STREAMER_PRINTF(("FileIo: Invalid file handle\n"));
		return -1;
	}

	if (s_handles[fd] == INVALID_HANDLE_VALUE)
	{
		STREAMER_PRINTF(("FileIo: File handle not open\n"));
		return -1;
	}

	if (!ReadFile(s_handles[fd],buffer,(DWORD)length,&bytesRead,0))
	{
		STREAMER_PRINTF(("FileIo: Read request failed (0x%08lx, %d)\n", GetLastError(), GetLastError()));
		return -1;
	}
	return bytesRead;
#else
	return read(fd, buffer, length);
#endif
}

int FileIo_LSeek(struct IODriver* driver, int fd, int offset, StreamerSeekMode whence)
{
#if defined(_WIN32)
	DWORD moveMethods[] = { FILE_BEGIN, FILE_CURRENT, FILE_END };
	LARGE_INTEGER in, out;

	if ((fd < 0) || (fd >= STREAMER_MAX_FILEHANDLES))
	{
		STREAMER_PRINTF(("FileIo: Invalid file handle\n"));
		return -1;
	}

	if (s_handles[fd] == INVALID_HANDLE_VALUE)
	{
		STREAMER_PRINTF(("FileIo: File handle not open\n"));
		return -1;
	}

	in.QuadPart = offset;
	if (!SetFilePointerEx(s_handles[fd], in, &out, moveMethods[whence]))
		return -1;

	return out.LowPart;
#else
	return lseek(fd,offset,whence);
#endif
}
