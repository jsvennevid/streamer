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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdarg.h>

void streamer_dprintf(const char* fmt, ...)
{
	va_list ap;
	char buffer[512];

	va_start(ap, fmt);
	vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, ap);
	va_end(ap);

	OutputDebugString(buffer);
	fprintf(stderr, buffer);
}

static volatile BOOL s_shutdown = FALSE;
static HANDLE s_thread = 0;
static HANDLE s_event = 0;

static DWORD WINAPI streamerThread(LPVOID arg)
{
	STREAMER_PRINTF(("Streamer: Thread started\n"));
 
	while (s_shutdown != TRUE)
	{
		switch (internalStreamerIdle())
		{
			case StreamerResult_Pending:
			{
				SleepEx(0, TRUE);
			}
			break;

			default:
			{
				WaitForSingleObject(s_event, INFINITE);
			}
			break;
		}
	}

	s_shutdown = FALSE;

	STREAMER_PRINTF(("Streamer: Thread stopped\n"));
	return 0;
}

int streamerInitialize(StreamerTransport transport, StreamerContainer container, const char* root, const char* file)
{
	s_event = CreateEvent(0, FALSE, FALSE, 0);
	if (!s_event)
	{
		STREAMER_PRINTF(("Streamer: Failed creating event\n"));
		return StreamerResult_Error;
	}

	s_thread = CreateThread(0, 0, streamerThread, 0, CREATE_SUSPENDED, 0);
	if (!s_thread)
	{
		STREAMER_PRINTF(("Streamer: Failed creating thread\n"));
		CloseHandle(s_event);
		s_event = 0;
		return StreamerResult_Error;
	}

	if (internalStreamerInitialize(transport, container, root, file) < 0)
	{
		STREAMER_PRINTF(("Streamer: Failed initializing streamer engine\n"));
		CloseHandle(s_thread);
		CloseHandle(s_event);
		s_event = s_thread = 0;
		return StreamerResult_Error;
	}

	ResumeThread(s_thread);
	return StreamerResult_Ok;
}

int streamerShutdown()
{
	if (s_thread)
	{
		s_shutdown = TRUE;
		internalStreamerSetEventFlag();

		while (s_shutdown != FALSE)
		{
			SleepEx(1, TRUE);
		}

		CloseHandle(s_thread);
		CloseHandle(s_event);
		s_thread = s_event = 0;
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
	SetEvent(s_event);
}

void internalStreamerIssueCompletion(int fd, int operation, int result, StreamerCallMethod method)
{
}

