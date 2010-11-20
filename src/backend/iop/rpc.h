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
#ifndef streamer_backend_rpc_h
#define streamer_backend_rpc_h

#define STREAMER_RPCID_IOP	0x54424c01

#include "../drivers/io.h"

typedef enum
{
	StreamerRpc_Iop_Initialize = 1,
	StreamerRpc_Iop_Open = 2,
	StreamerRpc_Iop_Close = 3,
	StreamerRpc_Iop_Read = 4,
	StreamerRpc_Iop_LSeek = 6,
	StreamerRpc_Iop_DOpen = 7,
	StreamerRpc_Iop_DClose = 8,
	StreamerRpc_Iop_DRead = 9,
} StreamerRpc_Iop;

typedef struct StreamerClientState
{
	int result;
	int pad[16-1];
} StreamerClientState;

typedef struct StreamerArguments
{
	union
	{
		int fd;
		int result;
	};

	int extra[3];
} StreamerArguments;

typedef struct StreamerInitArguments
{
	union
	{
		int mode;
		int result;
	};

	int container;
	StreamerClientState* response;

	char root[256];
	char file[256];
} StreamerInitArguments;

typedef struct StreamerOpenArguments
{
	union
	{
		int mode;
		int result;
	};

	int remote;

	char filename[256];
	int pad[2];
} StreamerOpenArguments;

typedef struct StreamerReadArguments
{
	union
	{
		int fd;
		int result;
	};

	void* buffer;
	int length;
	void* head;
	void* tail;
	int pad[3];
} StreamerReadArguments;

typedef struct StreamerSeekArguments
{
	union
	{
		int fd;
		int result;
	};

	int offset;
	int whence;
	int pad;
} StreamerSeekArguments;

#if defined(_IOP)
int internalStreamerStartRpcIOP();
void internalStreamerInitializeResponse(void* response);
void internalStreamerIssueResponse(int fd, int result, StreamerCallMethod method);
#endif

#endif
