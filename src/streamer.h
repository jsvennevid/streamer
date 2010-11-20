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
#ifndef streamer_common_streamer_h
#define streamer_common_streamer_h

#include "backend/drivers/io.h"

#if defined(_MSC_VER)
#pragma warning(disable: 4505)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

int streamerInitialize(StreamerTransport transport, StreamerContainer container, const char* root, const char* file);
int streamerShutdown();
int streamerPoll(int fd);
int streamerOpen(const char* filename, StreamerOpenMode mode);
int streamerClose(int fd);
int streamerRead(int fd, void* buffer, unsigned int length);
int streamerLSeek(int fd, int offset, StreamerSeekMode whence);

#if defined(__cplusplus)
}
#endif

#endif