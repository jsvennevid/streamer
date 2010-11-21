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
#include "../backend/iop/rpc.h"

#include "../backend/drivers/driver.h"
#include "../backend/backend.h"

#if defined(STREAMER_PS2_SCE)
#include <eekernel.h>
#include <sifdev.h>
#include <sif.h>
#else
#include <kernel.h>
#include <tamtypes.h>
#include <fileio.h>
#include <loadfile.h>
#include <iopheap.h>
#include <sbv_patches.h>
#define sceSifInitRpc SifInitRpc
#define sceSifInitIopHeap SifInitIopHeap
#define sceSifAllocIopHeap SifAllocIopHeap
#define sceSifSetDma SifSetDma
#define sceSifDmaStat SifDmaStat
#define sceSifLoadModuleBuffer SifLoadModuleBuffer
#define sceSifFreeIopHeap SifFreeIopHeap
#define sceSifLoadModule SifLoadModule
#endif

#include <alloca.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <sifrpc.h>
#include <errno.h>
#include <string.h>

extern int _iop_reboot_count;
#if defined(STREAMER_PS2_SCE)
static sceSifClientData cd0;
#else
static SifRpcClientData_t cd0;
#define sceSifCallRpc SifCallRpc
#define SIF_RPCM_NOWAIT SIF_RPC_M_NOWAIT
#endif

typedef struct StreamerClient
{
	char __attribute__((aligned(64))) m_head[64];
	char __attribute__((aligned(64))) m_tail[64];

	void* m_buffer; // buffer used for reading
	int m_operation;
	volatile int m_rpc;

	StreamerClientState* m_state;
} __attribute__((aligned(64))) StreamerClient;

static int s_initialized = 0;
static int s_loaded = 0;
static int StreamerSema = -1;
static StreamerClient __attribute__((aligned(64))) s_clients[STREAMER_MAX_FILEHANDLES];
static volatile StreamerClientState __attribute__((aligned(64))) s_response[STREAMER_MAX_FILEHANDLES];

static int loadStreamerModule();

void end_func(void* data)
{
	StreamerClient* client = (StreamerClient*)data;
	client->m_rpc = 0;
}

int streamerInitialize(StreamerTransport mode, StreamerContainer container, const char* root, const char* file)
{
#if !defined(STREAMER_PS2_SCE)
	static int _rb_count = 0;

	if (_rb_count != _iop_reboot_count)
	{
		_rb_count = _iop_reboot_count;

		s_initialized = 0;	
		s_loaded = 0;
	}	
#endif

	if (s_initialized)
	{
		return StreamerResult_Ok;
	}

	if (loadStreamerModule() < 0)
	{
		STREAMER_PRINTF(("Streamer (EE): Failed loading streamer module"));
		return StreamerResult_Error;
	}

	if (StreamerSema < 0)
	{
#if defined(STREAMER_PS2_SCE)
		struct SemaParam params;
		params.initCount = 1;
		params.maxCount = 1;
		params.option = 0;
#else
		ee_sema_t params;
		params.init_count = 1;
		params.max_count = 1;
		params.option = 0;
#endif
		StreamerSema = CreateSema(&params);
		if (StreamerSema < 0)
		{
			STREAMER_PRINTF(("Streamer (EE): Could not create semaphore\n"));
			return StreamerResult_Error;
		}
	}

	int res;
#if defined(STREAMER_PS2_SCE)
	while (((res = sceSifBindRpc(&cd0, STREAMER_RPCID_IOP, 0)) >= 0) && !cd0.serve)
#else
	while (((res = SifBindRpc(&cd0, STREAMER_RPCID_IOP, 0)) >= 0) && !cd0.server)
#endif
	{
#if defined(STREAMER_PS2_SCE)
		DelayThread(10);
#else
		nopdelay();
#endif
	}

	if (res < 0)
	{
		STREAMER_PRINTF(("Streamer: Failed to bind to IOP-RPC\n"));
		return res;
	}	

	volatile StreamerInitArguments* args = (StreamerInitArguments*)((((ptrdiff_t)alloca(sizeof(StreamerInitArguments) + 64)) + 63) & ~63);
	args->mode = mode;
	args->container = container;
	args->response = (StreamerClientState*)s_response;
	strcpy((char*)args->root, root);
	strcpy((char*)args->file, file);

	WaitSema(StreamerSema);

	if ((res = sceSifCallRpc(&cd0, StreamerRpc_Iop_Initialize, 0, (void*)args, sizeof(StreamerInitArguments), (void*)args, sizeof(int), 0, 0)) < 0)
	{
		SignalSema(StreamerSema);
		return res;
	}

	memset(&s_response, 0, sizeof(s_response));
	int i;
	for (i = 0; i < STREAMER_MAX_FILEHANDLES; ++i)
	{
		s_clients[i].m_operation = -1;
		s_clients[i].m_state = (StreamerClientState*)(((unsigned int)&(s_response[i])) | 0x20000000);
		s_clients[i].m_state->result = StreamerResult_Error;
	}

	SignalSema(StreamerSema);

	s_initialized = 1;
	return 0;
}

int streamerShutdown()
{
	STREAMER_PRINTF(("Streamer: Shutdown not implemented"));
	return StreamerResult_Error;
}

int streamerPoll(int fd)
{
	if (!s_initialized)
	{
		STREAMER_PRINTF(("Streamer: Driver not initialized\n"));
		return StreamerResult_Error;
	}

	int res = StreamerResult_Error;

	WaitSema(StreamerSema);

	if ((fd >= 0) && (fd < STREAMER_MAX_FILEHANDLES))
	{
		StreamerClient* client = &s_clients[fd];
		if (client->m_rpc > 0)
		{
			SignalSema(StreamerSema);
			return StreamerResult_Pending;
		}

		res = client->m_state->result;

		if (res != StreamerResult_Pending)
		{
			if (client->m_operation > 0)
			{
				switch (client->m_operation)
				{
					case StreamerRpc_Iop_Open:
					{
						if (res < 0)
						{
							client->m_operation = -1;
						}
						else
						{
							client->m_operation = 0;
						}
					}
					break;

					case StreamerRpc_Iop_Close:
					{
						client->m_operation = -1;
					}
					break;

					case StreamerRpc_Iop_Read:
					{
						if (res >= 0)
						{
							char* begin = (char*)(client->m_buffer);
							char* lead = (char*)(((ptrdiff_t)begin) & ~63);
							unsigned int length = res;

							if (begin != lead)
							{
								unsigned int size = (64 - (begin - lead)) < length ? (64 - (begin - lead)) : length;

								memcpy(begin, client->m_head + (begin - lead), size);

								begin += size;
								length -= size;
							}

							if (length & 63)
							{
								unsigned int size = length & 63;
								memcpy(begin + (length - size), client->m_tail, size);
							}
						}

						client->m_operation = 0;
					}
					break;

					default:
					{
						client->m_operation = 0;
					}
					break;
				}
			}
			else if (client->m_operation < 0)
			{
				STREAMER_PRINTF(("EE: filehandle not opened\n"));
			}
			else
			{
				STREAMER_PRINTF(("EE: no operation pending\n"));
			}
		}
	}
	else
	{
		STREAMER_PRINTF(("Streamer: Invalid handle %d\n", fd));
		res = StreamerResult_Error;
	}

	SignalSema(StreamerSema);

	return res;
}

int streamerOpen(const char* filename, StreamerOpenMode mode)
{
	if (!s_initialized)
	{
		STREAMER_PRINTF(("Streamer: Driver not initialized\n"));
		return StreamerResult_Error;
	}

	int res, i;
	volatile StreamerOpenArguments* args = (StreamerOpenArguments*)((((ptrdiff_t)alloca(sizeof(StreamerOpenArguments) + 64)) + 63) & ~63);
	args->mode = mode;
	strcpy((char*)(args->filename),filename);
	StreamerClient* client = 0;

	WaitSema(StreamerSema);

	for (i = 0; i < STREAMER_MAX_FILEHANDLES; ++i)
	{
		if (s_clients[i].m_operation >= 0)
		{
			continue;
		}

		client = &(s_clients[i]);
		break;
	}

	if (!client)
	{
		STREAMER_PRINTF(("Streamer: Out of file handles (EE side)\n"));
		SignalSema(StreamerSema);
		return StreamerResult_Error;
	}

	args->remote = i;

	client->m_operation = StreamerRpc_Iop_Open;
	client->m_rpc = 1;

	if ((res = sceSifCallRpc(&cd0, StreamerRpc_Iop_Open, SIF_RPCM_NOWAIT, (void*)args, sizeof(StreamerOpenArguments), (void*)args, sizeof(int), end_func, client)) < 0)
	{
		client->m_operation = -1;
		SignalSema(StreamerSema);
		return res;
	}

	SignalSema(StreamerSema);

	return i;
}

int streamerClose(int fd)
{
	if (!s_initialized)
	{
		STREAMER_PRINTF(("Streamer: Driver not initialized\n"));
		return StreamerResult_Error;
	}

	if ((fd < 0) || (fd >= STREAMER_MAX_FILEHANDLES))
	{
		STREAMER_PRINTF(("Streamer: Invalid file handle (EE side)\n"));
		return StreamerResult_Error;
	}

	StreamerClient* client = &(s_clients[fd]);

	if (client->m_operation < 0)
	{
		STREAMER_PRINTF(("Streamer: File handle not active (EE side)\n"));
		return StreamerResult_Error;
	}

	if (client->m_operation > 0)
	{
		STREAMER_PRINTF(("Streamer: File handle currently busy (EE side)\n"));
		return StreamerResult_Busy;
	}

	int res;
	volatile StreamerArguments* args = (StreamerArguments*)((((ptrdiff_t)alloca(sizeof(StreamerArguments) + 64)) + 63) & ~63);
	args->fd = fd;

	WaitSema(StreamerSema);

	client->m_operation = StreamerRpc_Iop_Close;
	client->m_rpc = 1;

	if ((res = sceSifCallRpc(&cd0, StreamerRpc_Iop_Close, SIF_RPCM_NOWAIT, (void*)args, sizeof(StreamerArguments), (void*)args, sizeof(int), end_func, client)) < 0)
	{
		client->m_operation = 0;
		SignalSema(StreamerSema);
		return res;
	}

	SignalSema(StreamerSema);

	return StreamerResult_Ok;
}

int streamerRead(int fd, void* buffer, unsigned int length)
{
	if (!s_initialized)
	{
		STREAMER_PRINTF(("Streamer: Driver not initialized\n"));
		return StreamerResult_Error;
	}

	if ((fd < 0) || (fd >= STREAMER_MAX_FILEHANDLES))
	{
		STREAMER_PRINTF(("Streamer: Invalid file handle (EE side)\n"));
		return StreamerResult_Error;
	}

	StreamerClient* client = &(s_clients[fd]);

	if (client->m_operation < 0)
	{
		STREAMER_PRINTF(("Streamer: File handle not active (EE side)\n"));
		return StreamerResult_Error;
	}

	if (client->m_operation > 0)
	{
		STREAMER_PRINTF(("Streamer: File handle currently busy (EE side)\n"));
		return StreamerResult_Busy;
	}

	int res;
	volatile StreamerReadArguments* args = (StreamerReadArguments*)((((ptrdiff_t)alloca(sizeof(StreamerReadArguments) + 64)) + 63) & ~63);
	args->fd = fd;
	args->buffer = buffer;
	args->length = length;
	args->head = s_clients[fd].m_head;
	args->tail = s_clients[fd].m_tail;

	if (!(((ptrdiff_t)buffer) & 0x20000000))
	{
		SyncDCache(buffer, ((char*)buffer) + length);
	}

	WaitSema(StreamerSema);

	client->m_operation = StreamerRpc_Iop_Read;
	client->m_rpc = 1;

	if ((res = sceSifCallRpc(&cd0, StreamerRpc_Iop_Read, SIF_RPCM_NOWAIT, (void*)args, sizeof(StreamerReadArguments), (void*)args, sizeof(int), end_func, client)) < 0)
	{
		client->m_operation = 0;
		SignalSema(StreamerSema);
		STREAMER_PRINTF(("Streamer: RPC call failed %d\n", res));
		return StreamerResult_Error;
	}

	s_clients[fd].m_buffer = buffer;

	SignalSema(StreamerSema);

	return StreamerResult_Ok;
}

int streamerLSeek(int fd, int offset, StreamerSeekMode whence)
{
	if (!s_initialized)
	{
		STREAMER_PRINTF(("Streamer: Driver not initialized\n"));
		return StreamerResult_Error;
	}

	if ((fd < 0) || (fd >= STREAMER_MAX_FILEHANDLES))
	{
		STREAMER_PRINTF(("Streamer: Invalid file handle (EE side)\n"));
		return StreamerResult_Error;
	}

	StreamerClient* client = &(s_clients[fd]);

	if (client->m_operation < 0)
	{
		STREAMER_PRINTF(("Streamer: File handle not active (EE side)\n"));
		return StreamerResult_Error;
	}

	if (client->m_operation > 0)
	{
		STREAMER_PRINTF(("Streamer: File handle currently busy (EE side)\n"));
		return StreamerResult_Busy;
	}

	int res;
	volatile StreamerSeekArguments* args = (StreamerSeekArguments*)((((ptrdiff_t)alloca(sizeof(StreamerSeekArguments) + 64)) + 63) & ~63);
	args->fd = fd;
	args->offset = offset;
	args->whence = whence;

	WaitSema(StreamerSema);

	client->m_operation = StreamerRpc_Iop_LSeek;
	client->m_rpc = 1;

	if ((res = sceSifCallRpc(&cd0, StreamerRpc_Iop_LSeek, SIF_RPCM_NOWAIT, (void*)args, sizeof(StreamerSeekArguments), (void*)args, sizeof(int), end_func, client)) < 0)
	{
		client->m_operation = 0;
		SignalSema(StreamerSema);
		return res;
	}

	SignalSema(StreamerSema);

	return StreamerResult_Ok;
}

extern char* _streamer_embedded_irx_start;
extern char* _streamer_embedded_irx_end;
extern int _streamer_embedded_irx_size;

int loadStreamerModule()
{
	int moduleSize, result;
	void* iopMemoryBlock;
	void* eeMemoryBlock;

	if (s_loaded)
	{
		return 0;
	}

	sceSifInitRpc(0);
	sceSifInitIopHeap();

	moduleSize = ((unsigned int)&_streamer_embedded_irx_end) - ((unsigned int)&_streamer_embedded_irx_start);

	iopMemoryBlock = 0;
	eeMemoryBlock = 0;

	do
	{
		void* alignedBlock;
		int id;
#if defined(STREAMER_PS2_SCE)
		sceSifDmaData sd;
#else
		SifDmaTransfer_t sd;
#endif

#if !defined(STREAMER_PS2_SCE)
		sbv_patch_enable_lmb();
#endif

		iopMemoryBlock = sceSifAllocIopHeap(moduleSize);
		if (!iopMemoryBlock)
		{
			break;
		}

		eeMemoryBlock = malloc(moduleSize + 64);
		if (!eeMemoryBlock)
		{
			break;
		}

		alignedBlock = (void*)((((unsigned int)eeMemoryBlock) + 63) & ~63);

		memcpy(alignedBlock, &_streamer_embedded_irx_start, moduleSize);
		SyncDCache(alignedBlock, ((char*)alignedBlock) + moduleSize);

#if defined(STREAMER_PS2_SCE)
		sd.data = (unsigned int)alignedBlock;
		sd.addr = (unsigned int)iopMemoryBlock;
		sd.size = moduleSize;
		sd.mode = 0;
#else
		sd.src = alignedBlock;
		sd.dest = iopMemoryBlock;
		sd.size = moduleSize;
		sd.attr = 0;
#endif

		while (!(id = sceSifSetDma(&sd, 1)));
		while (sceSifDmaStat(id) >= 0);

		if ((result = sceSifLoadModuleBuffer(iopMemoryBlock, 0, 0)) < 0)
		{
			STREAMER_PRINTF(("Streamer (EE): Error occured while loading module from memory (%d)\n", result));
			break;
		}

		STREAMER_PRINTF(("Streamer (EE): Streamer module loaded from memory\n"));
		s_loaded = 1;
	}
	while (0);

	if (iopMemoryBlock)
	{
		sceSifFreeIopHeap(iopMemoryBlock);
	}
	free(eeMemoryBlock);

	if (s_loaded)
	{
		return 0;
	}

	STREAMER_PRINTF(("Streamer (EE): Could not load streamer module from memory\n"));

	if ((result = sceSifLoadModule("host:streamer.irx", 0, 0)) < 0)
	{
		STREAMER_PRINTF(("Streamer (EE): Could not load streamer module from disk\n"));
		return StreamerResult_Error;
	}

	STREAMER_PRINTF(("Streamer (EE): Streamer module loaded from disk\n"));
	s_loaded = 1;

	return StreamerResult_Ok;
}
