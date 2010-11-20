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
#ifndef streamer_common_io_h
#define streamer_common_io_h

typedef enum
{
	StreamerResult_Ok = 0,
	StreamerResult_Error = -1,
	StreamerResult_Busy = -128,
	StreamerResult_Pending = -255
} StreamerResult;

typedef enum
{
	StreamerOpenMode_Read = 0,
	StreamerOpenMode_Write
} StreamerOpenMode;

typedef enum
{
	StreamerSeekMode_Set = 0,
	StreamerSeekMode_Current,
	StreamerSeekMode_End
} StreamerSeekMode;

typedef enum
{
	StreamerTransport_FileIo,
	StreamerTransport_Cdvd
} StreamerTransport;

typedef enum
{
	StreamerContainer_Direct,
	StreamerContainer_Zip,
	StreamerContainer_FileArchive
} StreamerContainer;

typedef enum
{
	StreamerCallMethod_Normal		// Normal call, no further action required
#if defined(STREAMER_PS2)
	, StreamerCallMethod_SifRpc		// Called over RPC from the EE
#endif
} StreamerCallMethod;

#endif
