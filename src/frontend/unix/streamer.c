#include "../../backend/backend.h"
#include "../../backend/drivers/driver.h"

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

static void internalStreamerSignal();

static void* streamerThread(void* arg)
{
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
				pthread_mutex_lock(&s_condMutex);
				pthread_cond_wait(&s_cond, &s_condMutex);
				pthread_mutex_unlock(&s_condMutex);
			}
			break;
		}
	}

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
		internalStreamerSignal();

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
		internalStreamerSignal();
	}
	return result;
}

int streamerClose(int fd)
{
	int result = internalStreamerClose(fd, StreamerCallMethod_Normal);
	if (result >= 0)
	{
		internalStreamerSignal();
	}
	return result;
}

int streamerRead(int fd, void* buffer, unsigned int length)
{
	int result = internalStreamerRead(fd, buffer, length, 0, 0, StreamerCallMethod_Normal);
	if (result >= 0)
	{
		internalStreamerSignal();
	}
	return result;
}

int streamerLSeek(int fd, int offset, StreamerSeekMode whence)
{
	int result = internalStreamerLSeek(fd, offset, whence, StreamerCallMethod_Normal);
	if (result >= 0)
	{
		internalStreamerSignal();
	}
	return result;
}

static void internalStreamerSignal()
{
	pthread_cond_signal(&s_cond);
}

