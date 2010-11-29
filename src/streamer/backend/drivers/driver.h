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
#ifndef streamer_common_driver_h
#define streamer_common_driver_h

#include "../../io.h"

typedef struct IODriver
{
	void (*destroy)(struct IODriver* driver);

	int (*open)(struct IODriver* driver, const char* filename, StreamerOpenMode mode);
	int (*close)(struct IODriver* driver, int fd);
	int (*read)(struct IODriver* driver, int fd, void* buffer, unsigned int length);
	int (*lseek)(struct IODriver* driver, int fd, int offset, StreamerSeekMode whence);

	int (*dopen)(struct IODriver* driver, const char* pathname);
	int (*dclose)(struct IODriver* driver, int fd);
	int (*dread)(struct IODriver* driver, int fd, const char* buffer, unsigned int length);

	int (*align)(struct IODriver* driver);
} IODriver;

#if defined(_MSC_VER)
#pragma warning(disable: 4100 4127)
#endif

#if !defined(STREAMER_FINAL)
#if defined(STREAMER_WIN32)
extern void streamer_dprintf(const char* fmt, ...);
#define STREAMER_PRINTF(x) do { streamer_dprintf x; } while (0)
#else
#define STREAMER_PRINTF(x) do { printf x; } while (0)
#endif
#else
#define STREAMER_PRINTF(x)
#endif

#endif
