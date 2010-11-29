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
#include "../backend/backend.h"
#include "../backend/drivers/driver.h"

#include <stdint.h>
#include <stdio.h>

#if defined(__linux__)
#define __USE_GNU
#endif
#include <pthread.h>

static volatile uint32_t s_shutdown = 0;
static pthread_mutex_t s_condMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_cond = PTHREAD_COND_INITIALIZER;
static pthread_t s_thread = 0;

static void* streamerThread(void* arg)
{
	pthread_mutex_lock(&s_condMutex);
	while (!s_shutdown)
	{
		switch (internalStreamerIdle())
		{
			case StreamerResult_Pending:
			{
#if defined(__APPLE__)
				pthread_yield_np();
#else
				pthread_yield();
#endif
			}
			break;

			default:
			{
				pthread_cond_wait(&s_cond, &s_condMutex);
			}
			break;
		}
	}
	pthread_mutex_unlock(&s_condMutex);

	s_shutdown = 1;
	pthread_exit(0);
}

int streamerInitialize(StreamerTransport transport, StreamerContainer container, const char* root, const char* file)
{
	int ret;

	if (internalStreamerInitialize(transport, container, root, file) < 0)
	{
		STREAMER_PRINTF(("Streamer: Failed initializing streamer engine\n"));
		return StreamerResult_Error;
	}

	ret = pthread_create(&s_thread, 0, streamerThread, 0);
	if (ret != 0)
	{
		STREAMER_PRINTF(("Streamer: Failed creating thread (%d)\n", ret));
		return StreamerResult_Error;
	}

	return StreamerResult_Ok;
}

int streamerShutdown()
{
	if (s_thread)
	{
		s_shutdown = 1;
		internalStreamerSetEventFlag();

		pthread_join(s_thread, 0);

		s_thread = 0; s_shutdown = 0;
	}

	if (internalStreamerShutdown() < 0)
	{
		STREAMER_PRINTF(("Streamer: Failed shutting down streamer engine\n"));
		return StreamerResult_Error;
	}

	return StreamerResult_Ok;
}

int streamerPoll(int fd)
{
	return internalStreamerPoll(fd);
}

int streamerOpen(const char* filename, StreamerOpenMode mode)
{
	int result = internalStreamerOpen(filename, mode, StreamerCallMethod_Normal);
	if (result >= 0)
	{
		internalStreamerSetEventFlag();
	}
	return result;
}

int streamerClose(int fd)
{
	int result = internalStreamerClose(fd, StreamerCallMethod_Normal);
	if (result >= 0)
	{
		internalStreamerSetEventFlag();
	}
	return result;
}

int streamerRead(int fd, void* buffer, unsigned int length)
{
	int result = internalStreamerRead(fd, buffer, length, 0, 0, StreamerCallMethod_Normal);
	if (result >= 0)
	{
		internalStreamerSetEventFlag();
	}
	return result;
}

int streamerLSeek(int fd, int offset, StreamerSeekMode whence)
{
	int result = internalStreamerLSeek(fd, offset, whence, StreamerCallMethod_Normal);
	if (result >= 0)
	{
		internalStreamerSetEventFlag();
	}
	return result;
}

void internalStreamerSetEventFlag()
{
	pthread_mutex_lock(&s_condMutex);
	pthread_cond_signal(&s_cond);
	pthread_mutex_unlock(&s_condMutex);
}

void internalStreamerIssueCompletion(int fd, int operation, int result, StreamerCallMethod method)
{
}
