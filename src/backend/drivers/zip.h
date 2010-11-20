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
#ifndef streamer_common_zip_h
#define streamer_common_zip_h

#include "driver.h"

#if defined(STREAMER_ZIP_SUPPORT)

typedef struct ZipFileEntry ZipFileEntry;
typedef struct ZipEntryHeader ZipEntryHeader;

struct ZipFileEntry
{
	char m_filename[256];
} Zip;

struct ZipEntryHeader
{
	int dummy;
};

typedef struct ZipIODriver
{
	IODriver m_interface;

	int m_centralDirectoryOffset;
	int m_bytesBeforeZipFile;

	struct
	{
		unsigned int m_signature;
		unsigned short m_disk;
		unsigned short m_diskCD;
		unsigned short m_numEntries;
		unsigned short m_numEntriesCD;
		unsigned int m_size;
		unsigned int m_offset;
		unsigned short m_commentSize;
	} m_centralDirectory;

	ZipFileEntry m_root;
	ZipEntryHeader* m_entries;

	unsigned char m_streamBuffer[2048*2];

	int m_fd;
	IODriver* m_container;
} ZipIODriver;

IODriver* Zip_Create(IODriver* native, const char* file);

void Zip_Destroy(struct IODriver* driver);
int Zip_Open(struct IODriver* driver, const char* filename, StreamerOpenMode mode);
int Zip_Close(struct IODriver* driver, int fd);
int Zip_Read(struct IODriver* driver, int fd, void* buffer, unsigned int length);
int Zip_LSeek(struct IODriver* driver, int fd, int offset, StreamerSeekMode whence);

#else

IODriver* Zip_Create(IODriver* native, const char* file);

#endif

#endif
