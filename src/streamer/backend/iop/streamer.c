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
#include "../drivers/driver.h"
#include "../backend.h"

#include "irx_imports.h"

static volatile int s_shutdown = 0;
static int s_thread = 0;
static int s_event = -1;

int streamerThread(void* argv)
{
	STREAMER_PRINTF(("Streamer: Thread started\n"));
	WakeupThread((int)argv);

	while (!s_shutdown)
	{
		switch (internalStreamerIdle())
		{
			case StreamerResult_Pending:
			{
				RotateThreadReadyQueue(0);
			}
			break;

			default:
			{
				u32 result;
				WaitEventFlag(s_event, 1, WEF_AND|WEF_CLEAR, &result);
			}
			break;
		}
	}

	s_shutdown = 0;

	STREAMER_PRINTF(("Streamer: Thread stopped\n"));
	return 0;
}

int streamerInitialize(StreamerTransport transport, StreamerContainer container, const char* root, const char* file)
{
	int result;

	iop_thread_t thread;
	iop_event_t event;

	event.attr = 0;
	event.option = 0;
	event.bits = 0;

	s_event = CreateEventFlag(&event);
	if (s_event < 0)
	{
		STREAMER_PRINTF(("Streamer: Failed to create event (err: %d)\n", s_event));
		return StreamerResult_Error;
	}

	thread.attr = TH_C;
	thread.option = 0;
	thread.thread = (void*)&streamerThread;
	thread.stacksize = 8192;
	thread.priority = 20;

	s_thread = CreateThread(&thread);
	if (s_thread <= 0)
	{
		STREAMER_PRINTF(("Streamer: Failed to create thread (err: %d).\n", s_thread));
		DeleteEventFlag(s_event);
		s_event = -1;
		return StreamerResult_Error;
	}

	if (internalStreamerInitialize(transport, container, root, file) < 0)
	{
		STREAMER_PRINTF(("Streamer: Failed initializing streamer engine\n"));
		DeleteThread(s_thread);
		DeleteEventFlag(s_event);
		s_thread = 0;
		s_event = -1;
		return StreamerResult_Error;
	}

	if ((result = StartThread(s_thread, (void*)GetThreadId())) < 0)
	{
		STREAMER_PRINTF(("Streamer: failed to start streamer thread. (%d)\n", result));
		DeleteThread(s_thread);
		DeleteEventFlag(s_event);
		s_thread = 0;
		s_event = -1;
		return StreamerResult_Error;
	}
	SleepThread();

	return StreamerResult_Ok;
}

int streamerShutdown()
{
	STREAMER_PRINTF(("Streamer: Shutting down streamer is unsupported on PS2\n"));

	// TODO: add support for shutting down

	return StreamerResult_Error;
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
	if (s_event >= 0)
	{
		SetEventFlag(s_event, 1);
	}
}

void internalStreamerIssueCompletion(int fd, int operation, int result, StreamerCallMethod method)
{
	switch (method)
	{
		default:
		break;

		case StreamerCallMethod_SifRpc:
		{
			internalStreamerIssueResponse(fd, result, method);
		}
		break;
	}
}
