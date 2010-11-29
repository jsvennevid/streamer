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
#ifndef streamer_common_fileio_h
#define streamer_common_fileio_h

#include "driver.h"

typedef struct FileIoDriver
{
	IODriver m_interface;
	char m_root[256];
} FileIoDriver;

#if defined(__cplusplus)
extern "C" {
#endif

IODriver* FileIo_Create(const char* root);

void FileIo_Destroy(struct IODriver* driver);
int FileIo_Open(struct IODriver* driver, const char* filename, StreamerOpenMode mode);
int FileIo_Close(struct IODriver* driver, int fd);
int FileIo_Read(struct IODriver* driver, int fd, void* buffer, unsigned int length);
int FileIo_LSeek(struct IODriver* driver, int fd, int offset, StreamerSeekMode whence);

#if defined(__cplusplus)
}
#endif

#endif
