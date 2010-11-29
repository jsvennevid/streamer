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
#include "../filearchive.h"

#define FILEARCHIVE_MAX_HANDLES 8

typedef struct FileArchiveHandle FileArchiveHandle;
typedef struct FileArchiveDriver FileArchiveDriver;

struct FileArchiveHandle
{
	const FileArchiveEntry* file;

	struct
	{
		uint32_t original;
		uint32_t compressed;
	} offset;

	struct
	{
		uint32_t offset;
		uint32_t fill;
		uint8_t* data;
	} buffer;
};

struct FileArchiveDriver
{
	IODriver interface;

	FileArchiveHeader* toc;
	uint32_t base;

	struct
	{
		uint32_t offset;
		uint32_t fill;
		int32_t owner;
		uint8_t* data;
	} cache;

	struct
	{
		IODriver* driver;
		int fd;
	} native;

	FileArchiveHandle handles[FILEARCHIVE_MAX_HANDLES];
};

IODriver* FileArchive_Create(IODriver* native, const char* file);

#endif

