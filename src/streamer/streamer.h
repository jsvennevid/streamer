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
#ifndef streamer_common_streamer_h
#define streamer_common_streamer_h

#include "backend/drivers/io.h"

#if defined(_MSC_VER)
#pragma warning(disable: 4505)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/**
 *
 * Initialize streamer
 *
 * \param transport - Native layer used for accessing the physical media
 * \param container - Logical layer used for accessing files
 * \param root - Root path for native layer
 * \param file - Logical file stored in the native layer
 * \return 0 if initialization was successful, <0 if an error occurs
 *
**/
int streamerInitialize(StreamerTransport transport, StreamerContainer container, const char* root, const char* file);

/**
 *
 * Shut down streamer
 *
 * \return 0 if shutdown was successful, <0 if an error occurs
 *
**/
int streamerShutdown();

/**
 *
 * Query if a pending I/O request has completed
 *
 * \param fd - File handle used for the I/O request
 * \return StreamerResult_Pending (-255) returned if operation is pending, otherwise the result of the operation is returned
 *
**/
int streamerPoll(int fd);

/**
 *
 * Open a file for reading or writing
 *
 * \note Call returns immediately after being scheduled, use streamerPoll() to query for the result using the returned file handle
 * \note Returns 0 if operation was successful, <0 if an error occured; in the case of an error the file handle is automatically released internally after the poll
 *
 * \param filename - File name to open
 * \param mode - Access mode to use
 * \return File handle used for file access, or <0 if an error occured
 *
**/
int streamerOpen(const char* filename, StreamerOpenMode mode);

/**
 *
 * Close an open file handle
 *
 * \note Call returns immediately after being scheduled, use streamerPoll() to query for the result of the operation
 * \note Do not fire-and-forget streamerClose() requests, it may leak file handles
 *
 * \param fd - File handle to close
 * \return 0 if request has been scheduled correctly, <0 if an error occured
 *
**/
int streamerClose(int fd);

/**
 *
 * Read data from an open file handle
 *
 * \note Call returns immediately after being scheduled, use streamerPoll() to query for the result of the operation
 * \note Returns the number of bytes read on success, <0 if an error occured
 *
 * \param fd - File handle to read from
 * \param buffer - Buffer to read data into
 * \param length - Maximum amount of data to read into buffer
 *
**/
int streamerRead(int fd, void* buffer, unsigned int length);

/**
 *
 * Seek into an open stream
 *
 * \note Call returns immediately after being scheduled, use streamerPoll() to query for the result of the operation
 * \note Returns the new stream location on success, <0 if an error occured
 *
 * \param fd - File handle to seek in
 * \param offset - Offset to use when seeking
 * \param whence - What seek mode to use
 *
**/
int streamerLSeek(int fd, int offset, StreamerSeekMode whence);

#if defined(__cplusplus)
}
#endif

#endif
