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
#define _CRT_SECURE_NO_WARNINGS
#include "backend.h"
#include "drivers/driver.h"
#include "drivers/filearchive.h"
#include "drivers/fileio.h"
#include "drivers/zip.h"
#include "drivers/cdvd.h"

#if defined(STREAMER_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(STREAMER_PS2)
#include "iop/irx_imports.h"
#elif defined(STREAMER_UNIX)
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#endif

#if defined(_MSC_VER)
#pragma warning(disable: 4201 4127)
#endif

typedef struct EntryHeader EntryHeader;
struct EntryHeader
{
	EntryHeader* m_prev;
	EntryHeader* m_next;
};

typedef enum
{
	EntryMode_Free = 0,
	EntryMode_File,
	EntryMode_Directory,
	EntryMode_Busy = 0x80
} EntryMode;

typedef struct QueueEntry
{
	EntryHeader m_header;

	volatile EntryMode m_mode;
	int m_target;

	StreamerCallMethod m_method;
	StreamerOperation m_operation;

	void* m_buffer;	// target buffer for transfer
	void* m_head;	// buffer for head of transfer (SifDma only)
	void* m_tail;	// buffer for tail of transfer (SifDma only)

	union
	{
		int m_offset;
		StreamerOpenMode m_openMode;
	};
	union
	{
		int m_length;
		StreamerSeekMode m_whence;
	};

	int m_dma;
	int m_result;

	char m_filename[256];	
} QueueEntry;

static IODriver* s_driver = 0;
static QueueEntry s_files[STREAMER_MAX_FILEHANDLES];

static EntryHeader s_active;
static EntryHeader s_pending;

#if defined(STREAMER_PS2)
static void* s_streamBuffer = 0;
static void* s_transferBuffer = 0;
static int s_activeDmaTransfer = -1;
#endif

#define STREAMER_BUFFER_SIZE (128 * 1024)
#define STREAMER_BUFFER_SIZE_ALIGN (STREAMER_BUFFER_SIZE + 64)

#if defined(STREAMER_WIN32)
static CRITICAL_SECTION s_queueCs;
#elif defined(STREAMER_PS2)
static int s_queueSemaphore = -1;
#elif defined(STREAMER_UNIX)
static pthread_mutex_t s_queueMutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static void entryInitialize(EntryHeader* header)
{
	header->m_prev = header->m_next = header;
}

static void entryAttach(EntryHeader* target, EntryHeader* header)
{
	header->m_prev = target->m_prev;
	header->m_next = target;

	header->m_prev->m_next = header;
	header->m_next->m_prev = header;
}

static void entryDetach(EntryHeader* header)
{
	header->m_prev->m_next = header->m_next;
	header->m_next->m_prev = header->m_prev;

	header->m_prev = header->m_next = header;
}

void lockStreamerQueue()
{
#if defined(STREAMER_WIN32)
	EnterCriticalSection(&s_queueCs);
#elif defined(STREAMER_PS2)
	WaitSema(s_queueSemaphore);
#elif defined(STREAMER_UNIX)
	pthread_mutex_lock(&s_queueMutex);
#else
#error Implement locking for your platform
#endif
}

void unlockStreamerQueue()
{
#if defined(STREAMER_WIN32)
	LeaveCriticalSection(&s_queueCs);
#elif defined(STREAMER_PS2)
	SignalSema(s_queueSemaphore);
#elif defined(STREAMER_UNIX)
	pthread_mutex_unlock(&s_queueMutex);
#else
#error Implement unlocking for your platform
#endif
}

static int rescheduleStreamerQueue(QueueEntry* entry)
{
	lockStreamerQueue();
	{
		if (!(entry->m_mode & EntryMode_Busy))
		{
			unlockStreamerQueue();
			return 0;
		}

		entryDetach(&(entry->m_header));
		entryAttach(&s_active, &(entry->m_header));
	}
	unlockStreamerQueue();

	return (s_active.m_next == &(entry->m_header)) && (s_pending.m_next == &s_pending);
}

#if defined(STREAMER_PS2)
int ps2ReadSifDma(QueueEntry* request)
{
	char* curr = request->m_buffer + request->m_offset;
	char* lead = (char*)(((ptrdiff_t)curr) & ~63);

	unsigned int packet = request->m_length - request->m_offset;
	packet = packet > (STREAMER_BUFFER_SIZE - (curr-lead)) ? (STREAMER_BUFFER_SIZE - (curr-lead)) : packet;

	if (!packet)
	{
		if (s_activeDmaTransfer == (request-s_files))
		{
			while (sceSifDmaStat(request->m_dma) >= 0);
			s_activeDmaTransfer = -1;
		}

		lockStreamerQueue();
		{
			request->m_mode &= ~EntryMode_Busy;
			entryDetach(&(request->m_header));
		}
		unlockStreamerQueue();

		internalStreamerIssueCompletion(request - s_files, StreamerOperation_Read, request->m_result, request->m_method);
		return -1;
	}

	int result = s_driver->read(s_driver, request->m_target, s_streamBuffer + (curr-lead),packet);

	if (result < 0)
	{
		if (s_activeDmaTransfer == (request-s_files))
		{
			while (sceSifDmaStat(request->m_dma) >= 0);
			s_activeDmaTransfer = -1;
		}

		request->m_length = 0;
		request->m_offset = 0;
		request->m_result = result;

		lockStreamerQueue();
		{
			request->m_mode &= ~EntryMode_Busy;
			entryDetach(&(request->m_header));
		}
		unlockStreamerQueue();
		return -1;
	}
	else if (result == 0)
	{
		request->m_result = request->m_offset;

		if (s_activeDmaTransfer == (request-s_files))
		{
			while (sceSifDmaStat(request->m_dma) >= 0);
			s_activeDmaTransfer = -1;
		}

		lockStreamerQueue();
		{
			request->m_mode &= ~EntryMode_Busy;
			entryDetach(&(request->m_header));
		}
		unlockStreamerQueue();

		internalStreamerIssueCompletion(request - s_files, StreamerOperation_Read, request->m_result, request->m_method);
		return -1;
	}
	else if (result > 0)
	{
		unsigned int uploaded = 0;
		unsigned int transfers = 0;
		SifDmaTransfer_t tx[3];
		char* src = s_streamBuffer;

		// head, transfer unaligned part to private buffer

		if (curr != lead)
		{
			unsigned int size = (64-(curr-lead)) < (result-uploaded) ? (64-(curr-lead)) : (result-uploaded);

			tx[transfers].src = src;
			tx[transfers].dest = request->m_head;
			tx[transfers].size = 64;
			tx[transfers].attr = 0;
			++transfers;

			src += 64;
			curr += size;
			uploaded += size;
		}

		// main part of the packet, aligned to cacheline

		if ((result-uploaded) >=  64)
		{
			unsigned int size = (result-uploaded) & ~63;

			tx[transfers].src = src;
			tx[transfers].dest = curr;
			tx[transfers].size = size;
			tx[transfers].attr = 0;
			++transfers;

			src += size;
			curr += size;
			uploaded += size;
		}

		// tail, transfer trailing bytes (less than one cacheline) to private buffer

		if (uploaded < result)
		{
			tx[transfers].src = src;
			tx[transfers].dest = request->m_tail;
			tx[transfers].size = result-uploaded;
			tx[transfers].attr = 0;
			++transfers;					
		}

		if (transfers > 0)
		{
			int queue;
			int interrupts;

			if (s_activeDmaTransfer >= 0)
			{
				while (sceSifDmaStat(s_files[s_activeDmaTransfer].m_dma) >= 0);
				s_activeDmaTransfer = -1;
			}

//			WaitVblankStart();

			CpuSuspendIntr(&interrupts);
			while (!(queue = sceSifSetDma(tx,transfers)));
			CpuResumeIntr(interrupts);

			s_activeDmaTransfer = request-s_files;
			request->m_dma = queue;

			char* temp = s_transferBuffer;
			s_transferBuffer = s_streamBuffer;
			s_streamBuffer = temp;
		}
	}

	request->m_offset += result;

	if ((result < packet) || (request->m_offset == request->m_length))
	{
		request->m_result = request->m_offset;

		if (s_activeDmaTransfer != (request-s_files))
		{
			lockStreamerQueue();
			{
				request->m_mode &= ~EntryMode_Busy;
				entryDetach(&(request->m_header));
			}
			unlockStreamerQueue();

			internalStreamerIssueCompletion(request - s_files, StreamerOperation_Read, request->m_result, request->m_method);
		}
		return -1;
	}

	return 0;
}
#endif

int internalStreamerIdle()
{
	QueueEntry* entry;

	if (s_pending.m_prev != &s_pending)
	{
		lockStreamerQueue();
		while (s_pending.m_prev != &s_pending)
		{
			QueueEntry* entry = (QueueEntry*)s_pending.m_next;

			entryDetach(&(entry->m_header));
			entryAttach(&s_active, &(entry->m_header));
		}
		unlockStreamerQueue();
	}

	if (s_active.m_prev == &s_active)
	{
		return StreamerResult_Ok;
	}

	entry = (QueueEntry*)s_active.m_next;
	switch (entry->m_operation)
	{
		case StreamerOperation_Open:
		{
			int fd;

			STREAMER_PRINTF(("Streamer: Opening file \"%s\"\n", entry->m_filename));

			fd = s_driver->open(s_driver, entry->m_filename, entry->m_openMode);

			lockStreamerQueue();
			{
				if (fd < 0)
				{
					STREAMER_PRINTF(("Streamer: Failed opening file\n"));

					entry->m_mode = EntryMode_Free;
					entry->m_result = StreamerResult_Error;
				}
				else
				{
					STREAMER_PRINTF(("Streamer: Opened file, fd %d\n", fd));

					entry->m_mode = EntryMode_File;
					entry->m_target = fd;
					entry->m_result = 0;
				}
				entryDetach(&(entry->m_header));
			}
			unlockStreamerQueue();

			internalStreamerIssueCompletion(entry - s_files, StreamerOperation_Open, entry->m_result, entry->m_method);
		}
		break;

		case StreamerOperation_Close:
		{
			STREAMER_PRINTF(("Closing file %d\n", entry->m_target));

			if (entry->m_target >= 0)
			{
				s_driver->close(s_driver, entry->m_target);
			}

			entry->m_result = StreamerResult_Ok;

			lockStreamerQueue();
			{
				entry->m_mode = EntryMode_Free;
				entryDetach(&(entry->m_header));
			}
			unlockStreamerQueue();

			internalStreamerIssueCompletion(entry - s_files, StreamerOperation_Close, entry->m_result, entry->m_method);
		}
		break;

		case StreamerOperation_Read:
		{
			switch (entry->m_method)
			{
				case StreamerCallMethod_Normal:
				{
					char* curr = ((char*)entry->m_buffer) + entry->m_offset;
					int packet = entry->m_length - entry->m_offset;
					int result;

					packet = packet > STREAMER_BUFFER_SIZE ? STREAMER_BUFFER_SIZE : packet;

					result = s_driver->read(s_driver, entry->m_target, curr, packet);				

					if (result < 0)
					{
						entry->m_result = StreamerResult_Error;

						lockStreamerQueue();
						{
							entry->m_mode &= ~EntryMode_Busy;
							entryDetach(&(entry->m_header));
						}
						unlockStreamerQueue();

						internalStreamerIssueCompletion(entry - s_files, StreamerOperation_Read, entry->m_result, entry->m_method);
						break;
					}

					entry->m_offset += result;

					if ((result < packet) || (entry->m_offset == entry->m_length))
					{
						entry->m_result = entry->m_offset;

						lockStreamerQueue();
						{
							entry->m_mode &= ~EntryMode_Busy;
							entryDetach(&(entry->m_header));
						}
						unlockStreamerQueue();

						internalStreamerIssueCompletion(entry - s_files, StreamerOperation_Read, entry->m_result, entry->m_method);
						break;
					}
				}
				break;

#if defined(STREAMER_PS2)
				case StreamerCallMethod_SifRpc:
				{
					ps2ReadSifDma(entry);
				}
				break;
#endif
			}

			rescheduleStreamerQueue(entry);
		}
		break;

		case StreamerOperation_LSeek:
		{
			int result;

			STREAMER_PRINTF(("Streamer: Seeking file %d\n", entry->m_target));

			result = s_driver->lseek(s_driver, entry->m_target, entry->m_offset, entry->m_whence);
			entry->m_result = result < 0 ? StreamerResult_Error : result;

			lockStreamerQueue();
			{
				entry->m_mode &= ~EntryMode_Busy;
				entryDetach(&(entry->m_header));
			}
			unlockStreamerQueue();			

			internalStreamerIssueCompletion(entry - s_files, StreamerOperation_LSeek, entry->m_result, entry->m_method);
		}
		break;
	}

	return StreamerResult_Pending;
}

#if defined(STREAMER_PS2)
int ps2StreamerInitialize()
{
	iop_sema_t sema;
	sema.attr = 1;
	sema.option = 0;
	sema.initial = 1;
	sema.max = 1;

	s_queueSemaphore = CreateSema(&sema);
	if (s_queueSemaphore < 0)
	{
		STREAMER_PRINTF(("Streamer: Failed initializing queue semaphore\n"));
		return StreamerResult_Error;
	}

	s_streamBuffer = AllocSysMemory(ALLOC_FIRST, STREAMER_BUFFER_SIZE_ALIGN * 2, 0);
	if (!s_streamBuffer)
	{
		STREAMER_PRINTF(("Streamer: Failed allocating stream buffers\n"));
		DeleteSema(s_queueSemaphore);
		return StreamerResult_Error;
	}

	s_transferBuffer = ((char*)s_streamBuffer) + STREAMER_BUFFER_SIZE_ALIGN;

	return StreamerResult_Ok;
}
#endif

int internalStreamerInitialize(StreamerTransport transport, StreamerContainer container, const char* root, const char* file)
{
	IODriver* native = 0;
	IODriver* logic = 0;
	int i;

	switch (transport)
	{
		case StreamerTransport_FileIo:
		{
			native = FileIo_Create(root);
		}
		break;

		case StreamerTransport_Cdvd:
		{
			native = Cdvd_Create();
		}
		break;
	}

	if (!native)
	{
		STREAMER_PRINTF(("Streamer: Failed to initialize native layer\n"));
		return StreamerResult_Error;
	}

	switch (container)
	{
		case StreamerContainer_Direct:
		{
			logic = native;
		}
		break;

		case StreamerContainer_Zip:
		{
			logic = Zip_Create(native, file);
		}
		break;

		case StreamerContainer_FileArchive:
		{
			logic = FileArchive_Create(native, file);
		}
		break;
	}

	if (!logic)
	{
		STREAMER_PRINTF(("Streamer: Failed to initialize logical layer\n"));
		native->destroy(native);
		return StreamerResult_Error;
	}

#if defined(STREAMER_WIN32)
	InitializeCriticalSection(&s_queueCs);
#elif defined(STREAMER_PS2)
	if (ps2StreamerInitialize() < 0)
	{
		STREAMER_PRINTF(("Streamer: Failed to initialize PS2 logic\n"));
		native->destroy(native);
		return StreamerResult_Error;
	}
#endif

	entryInitialize(&s_active);
	entryInitialize(&s_pending);

	s_driver = logic;

	memset(s_files, 0, sizeof(s_files));
	for (i = 0; i < STREAMER_MAX_FILEHANDLES; ++i)
	{
		entryInitialize(&(s_files[i].m_header));
	}

	return StreamerResult_Ok;
}

#if defined(STREAMER_PS2)
int ps2StreamerShutdown()
{
	DeleteSema(s_queueSemaphore);
}
#endif

int internalStreamerShutdown()
{
#if defined(STREAMER_WIN32)
	DeleteCriticalSection(&s_queueCs);
#elif defined(STREAMER_PS2)
	if (ps2StreamerShutdown() < 0)
	{
		return StreamerResult_Error;
	}
#endif

	return StreamerResult_Ok;
}

int internalStreamerPoll(int fd)
{
	int result = StreamerResult_Error;

	if ((fd < 0) || (fd >= STREAMER_MAX_FILEHANDLES))
	{
		STREAMER_PRINTF(("Streamer: Bad file descriptor %d\n", fd));
		return StreamerResult_Error;
	}

	lockStreamerQueue();
	do
	{
		QueueEntry* entry = &s_files[fd];
		int mode = entry->m_mode;

		if (mode & EntryMode_Busy)
		{
			result = StreamerResult_Pending;
			break;
		}

		if (entry->m_target < 0)
		{
			STREAMER_PRINTF(("Streamer: File descriptor %d has no target\n",  fd));
			break;
		}

		result = entry->m_result;
	}
	while (0);
	unlockStreamerQueue();

	return result;
}

int internalStreamerOpen(const char* filename, StreamerOpenMode mode, StreamerCallMethod method)
{
	int result = StreamerResult_Error;

	STREAMER_PRINTF(("Streamer: open(\"%s\", %d)\n", filename, mode));

	lockStreamerQueue();
	do
	{
		QueueEntry* entry = 0;
		int i;

		for (i = 0; i < STREAMER_MAX_FILEHANDLES; ++i)
		{
			if (s_files[i].m_mode == EntryMode_Free)
			{
				entry = &s_files[i];
				result = i;
				break;
			}
		}

		if (!entry)
		{
			STREAMER_PRINTF(("Streamer: Out of available file entries\n"));
			break;
		}

		strcpy(entry->m_filename, filename);
		entry->m_openMode = mode;
		entry->m_mode = EntryMode_Free|EntryMode_Busy;
		entry->m_operation = StreamerOperation_Open;
		entry->m_target = -1;
		entry->m_method = method;

		entryAttach(&s_pending, &(entry->m_header));
	}
	while (0);
	unlockStreamerQueue();

	return result;
}

int internalStreamerClose(int fd, StreamerCallMethod method)
{
	int result = StreamerResult_Error;

	STREAMER_PRINTF(("Streamer: close(%d)\n", fd));

	if ((fd < 0) || (fd >= STREAMER_MAX_FILEHANDLES))
	{
		STREAMER_PRINTF(("Streamer: Bad file descriptor %d\n", fd));
		return StreamerResult_Error;
	}

	lockStreamerQueue();
	do
	{
		QueueEntry* entry = &s_files[fd];
		int mode = entry->m_mode;

		if ((mode & ~EntryMode_Busy) != EntryMode_File)
		{
			STREAMER_PRINTF(("Streamer: File descriptor %d is not a file\n", fd));
			break;
		}

		if (mode & EntryMode_Busy)
		{
			STREAMER_PRINTF(("Streamer: File descriptor %d is busy\n", fd));
			result = StreamerResult_Busy;
			break;
		}

		if (entry->m_target < 0)
		{
			STREAMER_PRINTF(("Streamer: File descriptor %d has no target\n",  fd));
			break;
		}

		entry->m_mode |= EntryMode_Busy;
		entry->m_operation = StreamerOperation_Close;
		entry->m_method = method;
		entryAttach(&s_pending, &(entry->m_header));

		result = StreamerResult_Ok;
	}
	while (0);
	unlockStreamerQueue();

	return result;
}

int internalStreamerRead(int fd, void* buffer, unsigned int length, void* head, void* tail, StreamerCallMethod method)
{
	int result = StreamerResult_Error;

	if ((fd < 0) || (fd >= STREAMER_MAX_FILEHANDLES))
	{
		STREAMER_PRINTF(("Streamer: Bad file descriptor %d\n", fd));
		return StreamerResult_Error;
	}

	lockStreamerQueue();
	do
	{
		QueueEntry* entry = &s_files[fd];
		int mode = entry->m_mode;

		if ((mode & ~EntryMode_Busy) != EntryMode_File)
		{
			STREAMER_PRINTF(("Streamer: File descriptor %d is not a file\n", fd));
			break;
		}

		if (mode & EntryMode_Busy)
		{
			STREAMER_PRINTF(("Streamer: File descriptor %d is busy\n", fd));
			result = StreamerResult_Busy;
			break;
		}

		if (entry->m_target < 0)
		{
			STREAMER_PRINTF(("Streamer: File descriptor %d has no target\n",  fd));
			break;
		}

		entry->m_buffer = buffer;
		entry->m_length = length;
		entry->m_method = method;
		entry->m_head = head;
		entry->m_tail = tail;
		entry->m_offset = 0;
		entry->m_mode |= EntryMode_Busy;
		entry->m_operation = StreamerOperation_Read;

		entryAttach(&s_pending, &(entry->m_header));

		result = StreamerResult_Ok;
	}
	while (0);
	unlockStreamerQueue();

	return result;
}

int internalStreamerLSeek(int fd, int offset, StreamerSeekMode whence, StreamerCallMethod method)
{
	int result = StreamerResult_Error;

	STREAMER_PRINTF(("Streamer: lseek(%d, %d, %d)\n", fd, offset, whence));

	lockStreamerQueue();
	do
	{
		QueueEntry* entry = &s_files[fd];
		int mode = entry->m_mode;

		if ((mode & ~EntryMode_Busy) != EntryMode_File)
		{
			STREAMER_PRINTF(("Streamer: File descriptor %d is not a file\n", fd));
			break;
		}

		if (mode & EntryMode_Busy)
		{
			STREAMER_PRINTF(("Streamer: File descriptor %d is busy\n", fd));
			result = StreamerResult_Busy;
			break;
		}

		if (entry->m_target < 0)
		{
			STREAMER_PRINTF(("Streamer: File descriptor %d has no target\n",  fd));
			break;
		}

		entry->m_offset = offset;
		entry->m_whence = whence;
		entry->m_operation = StreamerOperation_LSeek;
		entry->m_mode |= EntryMode_Busy;
		entry->m_method = method;

		entryAttach(&s_pending, &(entry->m_header));

		result = StreamerResult_Ok;
	}
	while (0);
	unlockStreamerQueue();

	return result;
}
