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
#ifndef streamer_backend_backend_h
#define streamer_backend_backend_h

#include "../streamer.h"

#if defined(_MSC_VER)
#pragma warning(disable: 4505)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#define STREAMER_MAX_FILEHANDLES (8)

typedef enum
{
	StreamerOperation_Open,
	StreamerOperation_Close,
	StreamerOperation_Read,
	StreamerOperation_LSeek
} StreamerOperation;

int internalStreamerIdle();

int internalStreamerInitialize(StreamerTransport transport, StreamerContainer container, const char* root, const char* file);
int internalStreamerShutdown();
int internalStreamerPoll(int fd);
int internalStreamerOpen(const char* filename, StreamerOpenMode mode, StreamerCallMethod method);
int internalStreamerClose(int fd, StreamerCallMethod method);
int internalStreamerRead(int fd, void* buffer, unsigned int length, void* head, void* tail, StreamerCallMethod method);
int internalStreamerLSeek(int fd, int offset, StreamerSeekMode whence, StreamerCallMethod method);

/**
 *
 * internalStreamerSetEventFlag - Called internally after a successful call into streamer (to trigger event processing)
 *
**/

void internalStreamerSetEventFlag();

/**
 *
 * internalStreamerIssueResponse
 *
 * Trigger setting the response value for the client
 *
**/

void internalStreamerIssueResponse(int fd, int result, StreamerCallMethod method);

/**
 *
 * internalStreamerIssueCompletion - Callback path for RPC
 *
 * This is implemented by the platform
 *
 * \param fd - Handle that completed its work
 * \param operation - What operation that was executed
 * \param result - The result of that operation
 * \param method - Method used for the operation
 *
**/
void internalStreamerIssueCompletion(int fd, int operation, int result, StreamerCallMethod method);

#if defined(__cplusplus)
}
#endif

#endif
