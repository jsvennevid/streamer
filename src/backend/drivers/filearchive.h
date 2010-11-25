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
#ifndef streamer_common_filearchive_h
#define streamer_common_filearchive_h

#include "driver.h"

#define FILEARCHIVE_MAX_HANDLES 8

typedef struct FileArchiveContainer FileArchiveContainer;
typedef struct FileArchiveEntry FileArchiveEntry;
typedef struct FileArchiveHandle FileArchiveHandle;

struct FileArchiveContainer
{
	FileArchiveContainer* m_parent;
	FileArchiveContainer* m_children;
	FileArchiveContainer* m_next;

	FileArchiveEntry* m_files;

	char m_name[256];
};

struct FileArchiveEntry
{
	FileArchiveContainer* m_container;
	FileArchiveEntry* m_next;

	unsigned int m_compression;
	unsigned int m_offset;
	unsigned int m_originalSize;
	unsigned int m_compressedSize;
	unsigned int m_blockSize;
	unsigned int m_maxCompressedBlock;

	char m_name[256];
};

struct FileArchiveHandle
{
	FileArchiveEntry* m_file;
	unsigned int m_offset;
	unsigned int m_compressedOffset;

	unsigned int m_bufferOffset;
	unsigned int m_bufferFill;

	unsigned char* m_buffer;
};

typedef struct FileArchiveDriver
{
	IODriver m_interface;

	unsigned char* m_static;
	unsigned char* m_dynamic;

	FileArchiveContainer m_root;
	FileArchiveHandle m_handles[FILEARCHIVE_MAX_HANDLES];

	unsigned char* m_cache;
	unsigned int m_cacheOffset;
	unsigned int m_cacheFill;
	int m_cacheOwner;

	struct
	{
		IODriver* m_driver;
		int m_fd;
	} m_native;

	int m_base;

	FileArchiveContainer* m_currContainer;
	int m_containersLeft;

	FileArchiveEntry* m_currEntry;
	int m_entriesLeft;
} FileArchiveDriver;

#define FILEARCHIVE_VERSION_1 (1)
#define FILEARCHIVE_VERSION_CURRENT FILEARCHIVE_VERSION_1

#define FILEARCHIVE_MAGIC_COOKIE (('Z' << 24) | ('F' << 16) | ('A' << 8) | ('R'))

IODriver* FileArchive_Create(IODriver* native, const char* file);

#define STREAMER_FILEARCHIVE_SUPPORT_FASTLZ

#endif

