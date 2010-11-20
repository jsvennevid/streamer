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
#include "zip.h"

#if defined(STREAMER_ZIP_SUPPORT)

#if defined(_IOP)
#include "../iop/irx_imports.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

int Zip_LoadCentralDirectory(ZipIODriver* driver);

enum
{
	Zip_MaxCommentSize = 0xffff,
};

IODriver* Zip_Create(IODriver* native, const char* file)
{
#if defined(_IOP)
	ZipIODriver* driver = AllocSysMemory(ALLOC_FIRST, sizeof(ZipIODriver), 0);
#else
	ZipIODriver* driver = malloc(sizeof(ZipIODriver));
#endif

	driver->m_interface.destroy = Zip_Destroy;
	driver->m_interface.open = Zip_Open;
	driver->m_interface.close = Zip_Close;
	driver->m_interface.read = Zip_Read;
	driver->m_interface.lseek = Zip_LSeek;

	driver->m_interface.dopen = 0;
	driver->m_interface.dclose = 0;
	driver->m_interface.dread = 0;

	driver->m_interface.align = 0;

	driver->m_container = 0;

	if (native->align && native->align(native) > 0)
	{
		STREAMER_PRINTF(("Zip: Cannot handle pre-aligned I/O\n"));
		Zip_Destroy(&(driver->m_interface));
		return 0;
	}

	driver->m_fd = native->open(native,file,StreamerOpenMode_Read);
	if (driver->m_fd < 0)
	{
		STREAMER_PRINTF(("Zip: Failed to open archive\n"));
		Zip_Destroy(&(driver->m_interface));
		return 0;
	}
	driver->m_container = native;

	if (!Zip_LoadCentralDirectory(driver))
	{
		STREAMER_PRINTF(("Zip: Could not locate central directory\n"));
		Zip_Destroy(&(driver->m_interface));
		return 0;
	}

	STREAMER_PRINTF(("Zip: Driver initialized successfully\n"));
	return &(driver->m_interface);
}

void Zip_Destroy(IODriver* driver)
{
	ZipIODriver* local = (ZipIODriver*)driver;

	if (local->m_container && (local->m_fd >= 0))
	{
		local->m_container->close(local->m_container,local->m_fd);
	}

#if defined(_IOP)
	FreeSysMemory(driver);
#else
	free(driver);
#endif
	STREAMER_PRINTF(("Zip: Driver destroyed\n"));
}

int Zip_Open(struct IODriver* driver, const char* filename, StreamerOpenMode mode)
{
	return -1;
}

int Zip_Close(struct IODriver* driver, int fd)
{
	return -1;
}

int Zip_Read(struct IODriver* driver, int fd, void* buffer, unsigned int length)
{
	return -1;
}

int Zip_LSeek(struct IODriver* driver, int fd, int offset, StreamerSeekMode whence)
{
	return -1;
}

typedef struct
{
	unsigned short m_version;
	unsigned short m_versionNeeded;
	unsigned short m_flags;
	unsigned short m_compressionMethod;
	unsigned int m_dosDate;
	unsigned int m_crc;
	unsigned int m_compressedSize;
	unsigned int m_uncompressedSize;
	unsigned short m_filenameSize;
	unsigned short m_fileExtraSize;
	unsigned short m_fileCommentSize;
	unsigned short m_diskStart;
	unsigned short m_internalFa;
	unsigned int m_externalFa;
	unsigned int m_offset;
} FileHeader;

int Zip_LoadCentralDirectory(ZipIODriver* driver)
{
	IODriver* native = driver->m_container;
	int fd = driver->m_fd;

	int maxRead, offset;
	unsigned int bufferSize;
	unsigned char* buffer;
	int fileOffset, fileIndex;

	// find central directory

	int fileSize = native->lseek(native,fd,0,StreamerSeekMode_End);
	if (fileSize < 0)
	{
		STREAMER_PRINTF(("Zip: Could not seek to EOF\n"));
		return 0;
	}

	STREAMER_PRINTF(("Zip: Archive is %d bytes\n",fileSize));
	maxRead = Zip_MaxCommentSize < fileSize ? fileSize : Zip_MaxCommentSize;
	bufferSize = 1024 + 4;
	buffer = driver->m_streamBuffer;

	driver->m_centralDirectoryOffset = 0;

	for (offset = 4; offset < maxRead;)
	{
		int pos, size, i;

		if ((int)(offset + bufferSize) > maxRead)
		{
			offset = maxRead;
		}
		else
		{
			offset += bufferSize;
		}

		pos = fileSize - offset;
		size = (int)bufferSize < (fileSize - pos) ? (int)bufferSize : (fileSize-pos);

		if (native->lseek(native,fd,pos,StreamerSeekMode_Set) < 0)
		{
			STREAMER_PRINTF(("Zip: Failed seeking while looking for central directory\n"));
			return 0;
		}

		if (native->read(native,fd,buffer,size) < size)
		{
			STREAMER_PRINTF(("Zip: Failed reading while looking for central directory\n"));
			return 0;
		}

		for (i = size-3; i > 0; --i)
		{
			const unsigned char* l = buffer + i; 

			if ((l[0] == 0x50) && (l[1] == 0x4b) && (l[2] == 0x05) && (l[3] == 0x06))
				driver->m_centralDirectoryOffset = pos + i;
		}
	}

	if (!driver->m_centralDirectoryOffset)
	{
		STREAMER_PRINTF(("Zip: Could not find central directory\n"));
		return 0;
	}	

	// load central directory footer

	if (native->lseek(native,fd,driver->m_centralDirectoryOffset,StreamerSeekMode_Set) < 0)
	{
		STREAMER_PRINTF(("Zip: Failed seeking to central directory\n"));
		return 0;
	}

	if (native->read(native,fd,&(driver->m_centralDirectory), sizeof(driver->m_centralDirectory)) != sizeof(driver->m_centralDirectory))
	{
		STREAMER_PRINTF(("Zip: Failed reading central directory\n"));
		return 0;
	}

	if (driver->m_centralDirectory.m_disk || driver->m_centralDirectory.m_diskCD || (driver->m_centralDirectory.m_numEntries != driver->m_centralDirectory.m_numEntriesCD))
	{
		STREAMER_PRINTF(("Zip: Unsupported Zip format\n"));
		return 0;
	}

	if (driver->m_centralDirectoryOffset < (int)(driver->m_centralDirectory.m_offset + driver->m_centralDirectory.m_size))
	{
		STREAMER_PRINTF(("Zip: Invalid central directory\n"));
		return 0;
	}

	// load TOC

	fileOffset = driver->m_centralDirectory.m_offset;
	for (fileIndex = 0; fileIndex < driver->m_centralDirectory.m_numEntries; ++fileIndex)
	{
		//FileHeader* header = driver->m_streamBuffer;
	}

	STREAMER_PRINTF(("Zip: Function not complete yet..."));
	return 0;
}
#else
#if defined(_MSC_VER)
#pragma warning(disable: 4127 4100)
#endif

#if defined(_IOP)
#include "../iop/irx_imports.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

IODriver* Zip_Create(IODriver* native, const char* file)
{
	STREAMER_PRINTF(("Zip: Support disabled\n"));
	return 0;
}
#endif
