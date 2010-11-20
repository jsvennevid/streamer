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
#include "rpc.h"
#include "../backend.h"
#include "../drivers/driver.h"

#include "irx_imports.h"

#include <sifrpc.h>
#include <sifcmd.h>
#include <errno.h>

static SifRpcDataQueue_t dataQueue;
static SifRpcServerData_t serverData;
static void* rpcBuffer = 0;
static int s_remoteHandles[STREAMER_MAX_FILEHANDLES];
static StreamerClientState* s_response = 0;

#define RPC_BUFFER_SIZE (4096)

void* RpcServer(int action, void* data, int size)
{
	switch (action)
	{
		case StreamerRpc_Iop_Initialize:
		{
			int i;
			for (i = 0; i < STREAMER_MAX_FILEHANDLES; ++i)
			{
				s_remoteHandles[i] = -1;
			}

			volatile StreamerInitArguments* args = (StreamerInitArguments*)data;
			args->result = streamerInitialize(args->mode, args->container, (const char*)args->root, (const char*)args->file);
			s_response = args->response;
			return data;
		}
		break;

		case StreamerRpc_Iop_Open:
		{
			volatile StreamerOpenArguments* args = (StreamerOpenArguments*)data;
			int result;

			s_remoteHandles[args->result] = args->remote;
			internalStreamerIssueResponse(args->remote, StreamerResult_Pending, StreamerCallMethod_SifRpc);

			result = internalStreamerOpen((char*)args->filename, args->mode, StreamerCallMethod_SifRpc);
			if (args->result >= 0)
			{
				internalStreamerSetEventFlag();
			}
			else
			{
				s_remoteHandles[args->result] = -1;
				internalStreamerIssueResponse(args->remote, StreamerResult_Error, StreamerCallMethod_SifRpc);
			}
			return data;
		}
		break;

		case StreamerRpc_Iop_Close:
		{
			volatile StreamerArguments* args = (StreamerArguments*)data;

			int remote, result;
			for (remote = 0; remote < STREAMER_MAX_FILEHANDLES; ++remote)
			{
				if (s_remoteHandles[remote] == args->fd)
				{
					break;
				}
			}

			internalStreamerIssueResponse(args->fd, StreamerResult_Pending, StreamerCallMethod_SifRpc);

			result = (remote < STREAMER_MAX_FILEHANDLES) ? internalStreamerClose(remote, StreamerCallMethod_SifRpc) : StreamerResult_Error;
			if (args->result >= 0)
			{
				internalStreamerSetEventFlag();
			}
			else
			{
				internalStreamerIssueResponse(args->fd, StreamerResult_Error, StreamerCallMethod_SifRpc);
			}
			return data;
		}
		break;

		case StreamerRpc_Iop_Read:
		{
			volatile StreamerReadArguments* args = (StreamerReadArguments*)data;

			int remote, result;
			for (remote = 0; remote < STREAMER_MAX_FILEHANDLES; ++remote)
			{
				if (s_remoteHandles[remote] == args->fd)
				{
					break;
				}
			}

			internalStreamerIssueResponse(args->fd, StreamerResult_Pending, StreamerCallMethod_SifRpc);

			result = (remote < STREAMER_MAX_FILEHANDLES) ? internalStreamerRead(args->fd, (void*)args->buffer, args->length, (void*)args->head, (void*)args->tail, StreamerCallMethod_SifRpc) : StreamerResult_Error;
			if (args->result >= 0)
			{
				internalStreamerSetEventFlag();
			}
			else
			{
				internalStreamerIssueResponse(args->fd, StreamerResult_Error, StreamerCallMethod_SifRpc);
			}
			return data;
		}
		break;

		case StreamerRpc_Iop_LSeek:
		{
			volatile StreamerSeekArguments* args = (StreamerSeekArguments*)data;

			int remote, result;
			for (remote = 0; remote < STREAMER_MAX_FILEHANDLES; ++remote)
			{
				if (s_remoteHandles[remote] == args->fd)
				{
					break;
				}
			}

			internalStreamerIssueResponse(args->fd, StreamerResult_Pending, StreamerCallMethod_SifRpc);

			result = (remote < STREAMER_MAX_FILEHANDLES) ? internalStreamerLSeek(args->fd, args->offset, args->whence, StreamerCallMethod_SifRpc) : StreamerResult_Error;
			if (args->result >= 0)
			{
				internalStreamerSetEventFlag();
			}
			else
			{
				internalStreamerIssueResponse(args->fd, StreamerResult_Error, StreamerCallMethod_SifRpc);
			}
			return data;
		}
		break;

		default:
		{
			volatile StreamerArguments* args = (StreamerArguments*)data;
			args->result = -EBADF;
			return data;
		}
		break;
	}
}

int rpcThread(void* argv)
{
	STREAMER_PRINTF(("Streamer: IOP-RPC thread started\n"));

	if ((rpcBuffer = AllocSysMemory(ALLOC_FIRST, RPC_BUFFER_SIZE, 0)) == 0)
	{
		STREAMER_PRINTF(("Streamer: Failed to allocate memory for RPC buffer\n"));
		SleepThread();
	}

	sceSifInitRpc(0);

	sceSifSetRpcQueue(&dataQueue, GetThreadId());
	sceSifRegisterRpc(&serverData, STREAMER_RPCID_IOP, RpcServer, (void*)rpcBuffer, 0, 0, &dataQueue);
	WakeupThread((int)argv);
	sceSifRpcLoop(&dataQueue);

	return 0;
}

int internalStreamerStartRpcIOP()
{
	int rpc, result;
	iop_thread_t thread;

	thread.attr = TH_C;
	thread.option = 0;
	thread.thread = (void*)&rpcThread;
	thread.stacksize = 8192;
	thread.priority = 35;

	if ((rpc = CreateThread(&thread)) <= 0)
	{
		STREAMER_PRINTF(("Streamer: failed to create EE-RPC thread (%d)\n", rpc));
		return -1;
	}

	if ((result = StartThread(rpc,(void*)GetThreadId())) < 0)
	{
		STREAMER_PRINTF(("Streamer: failed to start EE-RPC thread (%d)\n", result));
		return -1;
	}
	SleepThread();

	STREAMER_PRINTF(("Streamer: IOP-RPC Initialized\n"));
	return 0;
}

void internalStreamerIssueResponse(int fd, int result, StreamerCallMethod method)
{
	if ((fd < 0) || (fd > STREAMER_MAX_FILEHANDLES))
	{
		STREAMER_PRINTF(("Streamer (IOP): Invalid response handle %d\n", fd));
		return;
	}

	switch (method)
	{
		default:
		break;

		case StreamerCallMethod_SifRpc:
		{
			int remote = s_remoteHandles[fd];
			if ((remote < 0) || (remote > STREAMER_MAX_FILEHANDLES))
			{
				STREAMER_PRINTF(("Streamer (IOP): Invalid EE response handle %d\n", remote));
				return;
			}

			int resultBlock[4];
			int queue, interrupts;
			SifDmaTransfer_t sd;

			resultBlock[0] = result;
			resultBlock[1] = result;
			resultBlock[2] = result;
			resultBlock[3] = result;

			sd.src = resultBlock;
			sd.dest = &(s_response[remote]);
			sd.size = sizeof(resultBlock);
			sd.attr = 0;

			CpuSuspendIntr(&interrupts);
			while (!(queue = sceSifSetDma(&sd,1)));
			CpuResumeIntr(interrupts);

			while (sceSifDmaStat(queue) >= 0);
		}
		break;
	}
}
