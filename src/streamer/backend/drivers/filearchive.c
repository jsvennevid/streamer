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

#include "filearchive.h"
#include <fastlz/fastlz.h>


#if defined(_IOP)
#include "../iop/irx_imports.h"
#else
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#define FILEARCHIVE_CACHE_SIZE (128 * 1024)
#define FILEARCHIVE_BUFFER_SIZE (16 * 1024)

static void FileArchive_Destroy(struct IODriver* driver);
static int FileArchive_Open(struct IODriver* driver, const char* filename, StreamerOpenMode mode);
static int FileArchive_Close(struct IODriver* driver, int fd);
static int FileArchive_Read(struct IODriver* driver, int fd, void* buffer, unsigned int length);
static int FileArchive_LSeek(struct IODriver* driver, int fd, int offset, StreamerSeekMode whence);

static int FileArchive_LoadHeader(FileArchiveDriver* driver);
static int FileArchive_LocateHeader(FileArchiveDriver* driver);

static int FileArchive_FillCache(FileArchiveDriver* driver, FileArchiveHandle* handle, const FileArchiveEntry* file, int minFill);

IODriver* FileArchive_Create(IODriver* native, const char* file)
{
	int i;
#if defined(_IOP)
	uint8_t* buffer = AllocSysMemory(ALLOC_FIRST, sizeof(FileArchiveDriver) + FILEARCHIVE_MAX_HANDLES * FILEARCHIVE_BUFFER_SIZE + FILEARCHIVE_CACHE_SIZE, 0);
#else
	uint8_t* buffer = malloc(sizeof(FileArchiveDriver) + FILEARCHIVE_MAX_HANDLES * FILEARCHIVE_BUFFER_SIZE + FILEARCHIVE_CACHE_SIZE);
#endif
	FileArchiveDriver* driver = (FileArchiveDriver*)buffer;
	buffer += sizeof(FileArchiveDriver);

	memset(driver, 0, sizeof(FileArchiveDriver));

	driver->interface.destroy = FileArchive_Destroy;
	driver->interface.open = FileArchive_Open;
	driver->interface.close = FileArchive_Close;
	driver->interface.read = FileArchive_Read;
	driver->interface.lseek = FileArchive_LSeek;

	if (native->align && native->align(native) > 0)
	{
		STREAMER_PRINTF(("FileArchive: Cannot handle pre-aligned I/O\n"));
		FileArchive_Destroy(&(driver->interface));
		return 0;
	}

	driver->native.fd = native->open(native, file, StreamerOpenMode_Read);
	if (driver->native.fd < 0)
	{
		STREAMER_PRINTF(("FileArchive: Could not open native file '%s'\n", file));
		FileArchive_Destroy(&(driver->interface));
		return 0;
	}
	driver->native.driver = native;

	for (i = 0; i < FILEARCHIVE_MAX_HANDLES; ++i)
	{
		driver->handles[i].buffer.data = buffer;
		buffer += FILEARCHIVE_BUFFER_SIZE;
	}
	driver->cache.data = buffer;

	if (FileArchive_LoadHeader(driver) < 0)
	{
		STREAMER_PRINTF(("FileArchive: Could not load header\n"));
		FileArchive_Destroy(&(driver->interface));
		return 0;
	}

	STREAMER_PRINTF(("FileArchive: Driver created\n"));

	driver->cache.owner = -1;
	return &(driver->interface);
}

static void FileArchive_Destroy(struct IODriver* driver)
{
	FileArchiveDriver* local = (FileArchiveDriver*)driver;

	if (local->native.driver && (local->native.fd >= 0))
	{
		local->native.driver->close(local->native.driver, local->native.fd);
	}

#if defined(_IOP)
	FreeSysMemory(driver);
#else
	free(driver);
#endif

	STREAMER_PRINTF(("FileArchive: Driver destroyed\n"));
}

static int FileArchive_Open(struct IODriver* driver, const char* filename, StreamerOpenMode mode)
{
	const char* begin, * end;
	FileArchiveDriver* local = (FileArchiveDriver*)driver;
	const FileArchiveContainer* container;
	const FileArchiveEntry* entry;
	int i,n;

	STREAMER_PRINTF(("FileArchive: open(\"%s\", %d)\n", filename, mode));

	if (local->toc == NULL)
	{
		STREAMER_PRINTF(("FileArchive: Archive TOC not available\n"));
		return -1;
	}

	if (mode != StreamerOpenMode_Read)
	{
		STREAMER_PRINTF(("FileArchive: Invalid mode when opening file\n"));
		return -1;
	}

	begin = filename;
	end = filename + strlen(filename);

	container = (const FileArchiveContainer*)(((const char*)local->toc) + local->toc->containers);
	do
	{
		uint32_t offset;
		const char* curr = begin;
		while ((curr != end) && (*curr != '/'))
		{
			++curr;
		}

		if (curr == end)
		{
			break;
		}

		for (offset = container->children; offset != FILEARCHIVE_INVALID_OFFSET; offset = container->next)		
		{
			const char* name;

			container = (const FileArchiveContainer*)(((const char*)local->toc) + offset);
			name = container->name != FILEARCHIVE_INVALID_OFFSET ? ((const char*)local->toc) + container->name : "";

			if (strlen(name) != (size_t)(curr-begin))
			{
				continue;
			}

			if (!memcmp(name, begin, (curr-begin)))
			{
				break;
			}
		}

		if (offset == FILEARCHIVE_INVALID_OFFSET)
		{
			STREAMER_PRINTF(("FileArchive: Could not locate container for '%s'\n", filename));
			return -1;
		}

		begin = curr + 1;
	}
	while (1);

	if (begin == end)
	{
		STREAMER_PRINTF(("FileArchive: Invalid filename when opening '%s'\n", filename));
		return -1;
	}

	entry = (const FileArchiveEntry*)(((const char*)local->toc) + container->files);
	for (i = 0, n = container->count; i < n; ++i, ++entry)
	{
		const char* name = entry->name != FILEARCHIVE_INVALID_OFFSET ? ((const char*)local->toc) + entry->name : "";
		if (strlen(name) != (size_t)(end - begin))
		{
			continue;
		}

		if (!memcmp(name, begin, end-begin))
		{
			break;
		}
	}

	if (i == n)
	{
		STREAMER_PRINTF(("FileArchive: Could not find file '%s'\n", filename));
		return -1;
	}

	for (i = 0; i < FILEARCHIVE_MAX_HANDLES; ++i)
	{
		FileArchiveHandle* handle = &(local->handles[i]);

		if (handle->file)
		{
			continue;
		}

		handle->file = entry;

		handle->offset.original = 0;
		handle->offset.compressed = 0;

		handle->buffer.offset = 0;
		handle->buffer.fill = 0;

		return i;
	}

	STREAMER_PRINTF(("FileArchive: No file handle available\n"));
	return -1;
}

static int FileArchive_Close(struct IODriver* driver, int fd)
{
	FileArchiveDriver* local = (FileArchiveDriver*)driver;

	STREAMER_PRINTF(("FileArchive: close(%d)\n", fd));

	if ((fd < 0) || (fd >= FILEARCHIVE_MAX_HANDLES))
	{
		STREAMER_PRINTF(("FileArchive: Invalid file handle\n"));
		return -1;
	}

	if (!local->handles[fd].file)
	{
		STREAMER_PRINTF(("FileArchive: File not opened\n"));
		return -1;
	}

	if (local->cache.owner == fd)
	{
		local->cache.owner = -1;
	}

	local->handles[fd].file = 0;
	return 0;
}

static int FileArchive_Read(struct IODriver* driver, int fd, void* buffer, unsigned int length)
{
	FileArchiveDriver* local = (FileArchiveDriver*)driver;
	FileArchiveHandle* handle;
	const FileArchiveEntry* file;
	int compression;

	if ((fd < 0) || (fd >= FILEARCHIVE_MAX_HANDLES))
	{
		STREAMER_PRINTF(("FileArchive: Invalid file handle\n"));
		return -1;
	}

	handle = &(local->handles[fd]);
	file = handle->file;
	if (!file)
	{
		STREAMER_PRINTF(("FileArchive: File handle not opened\n"));
		return -1;
	}

	compression = handle->file->compression;
	if (compression == FILEARCHIVE_COMPRESSION_NONE)
	{
		int maxRead = (file->size.original - handle->offset.original) < length ? (file->size.original - handle->offset.original) : length;
		int result;

		result = local->native.driver->lseek(local->native.driver, local->native.fd, local->base + file->data + handle->offset.original, StreamerSeekMode_Set);
		if (result < 0)
		{
			STREAMER_PRINTF(("FileArchive: Failed seeking to uncompressed location (%d)\n", result));
			return result;
		}

		result = local->native.driver->read(local->native.driver, local->native.fd, buffer, maxRead);
		if (result != maxRead)
		{
			STREAMER_PRINTF(("FileArchive: Failed reading %d uncompressed bytes from archive (%d)\n", maxRead, result));
			return -1;
		}

		handle->offset.original += result;
		return result;
	}
	else
	{
		int actual = 0;

		if (local->cache.owner != fd)
		{
			local->cache.offset = 0;
			local->cache.fill = 0;
			local->cache.owner = fd;
		}

		length = length < (unsigned int)(file->size.original - handle->offset.original) ? length : (file->size.original - handle->offset.original);
		while (length > 0)
		{
			int maxRead, bufferRead;

			if (handle->buffer.fill == handle->buffer.offset)
			{
				FileArchiveCompressedBlock block;
				uint32_t cacheUsage;

				if (FileArchive_FillCache(local, handle, file, sizeof(FileArchiveCompressedBlock)) < 0)
				{
					STREAMER_PRINTF(("FileArchive: Error while filling compression cache\n"));
					return -1;
				}

				memcpy(&block, local->cache.data + local->cache.offset, sizeof(FileArchiveCompressedBlock));

				if (block.original > FILEARCHIVE_BUFFER_SIZE)
				{
					STREAMER_PRINTF(("FileArchive: Decompressed block too large (max: %d, was: %d)\n", FILEARCHIVE_BUFFER_SIZE, block.original));
					return -1;
				}

				if (FileArchive_FillCache(local, handle, file, sizeof(unsigned short)*2 + (block.compressed & FILEARCHIVE_COMPRESSION_SIZE_MASK)) < 0)
				{
					STREAMER_PRINTF(("FileArchive: Error while filling compression cache\n"));
					return -1;
				}

				if (block.compressed & FILEARCHIVE_COMPRESSION_SIZE_IGNORE)
				{
					if ((block.compressed & FILEARCHIVE_COMPRESSION_SIZE_MASK) != block.original)
					{
						STREAMER_PRINTF(("FileArchive: Uncompressed block size mismatch\n"));
						return -1;
					}

					memcpy(handle->buffer.data, local->cache.data + local->cache.offset + sizeof(FileArchiveCompressedBlock), block.original);
				}
				else
				{
					switch (compression)
					{
						case FILEARCHIVE_COMPRESSION_FASTLZ:
						{
							int result = fastlz_decompress(local->cache.data + local->cache.offset + sizeof(FileArchiveCompressedBlock), block.compressed, handle->buffer.data, FILEARCHIVE_BUFFER_SIZE);
							if (result != block.original)
							{
								STREAMER_PRINTF(("FileArchive: Failed to decompress fastlz block\n"));
								return -1;
							}
						}
						break;

						default:
						{
							STREAMER_PRINTF(("FileArchive: Unsupported compression scheme\n"));
							return -1;
						}
						break;
					}
				}

				cacheUsage = (block.compressed & FILEARCHIVE_COMPRESSION_SIZE_MASK) + sizeof(FileArchiveCompressedBlock);
				handle->offset.compressed += cacheUsage;
				local->cache.offset += cacheUsage;

				handle->buffer.offset = 0;
				handle->buffer.fill = block.original;
			}

			bufferRead = handle->buffer.fill - handle->buffer.offset;
			maxRead = (unsigned int)bufferRead > length ? length : bufferRead;

			if (!maxRead)
			{
				break;
			}

			memcpy(buffer, handle->buffer.data + handle->buffer.offset, maxRead);

			buffer = ((char*)buffer) + maxRead;
			length -= maxRead;
			actual += maxRead;

			handle->buffer.offset += maxRead;
			handle->offset.original += maxRead;
		}

		return actual;
	}
}

static int FileArchive_LSeek(struct IODriver* driver, int fd, int offset, StreamerSeekMode whence)
{
	FileArchiveDriver* local = (FileArchiveDriver*)driver;
	FileArchiveHandle* handle;
	const FileArchiveEntry* file;
	int newOffset = 0;

	STREAMER_PRINTF(("FileArchive: lseek(%d, %d, %d)\n", fd, offset, whence));

	if ((fd < 0) || (fd >= FILEARCHIVE_MAX_HANDLES))
	{
		STREAMER_PRINTF(("FileArchive: Invalid file handle\n"));
		return -1;
	}

	handle = &(local->handles[fd]);
	file = handle->file;
	if (!file)
	{
		STREAMER_PRINTF(("FileArchive: File handle not opened\n"));
		return -1;
	}

	if (file->compression == FILEARCHIVE_COMPRESSION_NONE)
	{
		switch (whence)
		{
			case StreamerSeekMode_Set: newOffset = offset; break;
			case StreamerSeekMode_Current: newOffset = handle->offset.original + offset; break;
			case StreamerSeekMode_End: newOffset = file->size.original + offset; break;
		}

		if ((newOffset < 0) || (newOffset > (int)file->size.original))
		{
			STREAMER_PRINTF(("FileArchive: Seeking out of bounds\n"));
			return -1;
		}

		handle->offset.original = newOffset;
	}
	else
	{
		// TODO: since compression is block-based, we can do seeking (although it'll be somewhat expensive) - investigate

		if (offset != 0)
		{
			STREAMER_PRINTF(("FileArchive: Can only seek to beginning or end of compressed files\n"));
			return -1;
		}

		if (fd == local->cache.owner)
		{
			local->cache.owner = -1;
		}

		switch (whence)
		{
			case StreamerSeekMode_Set:
			{
				handle->offset.original = 0;
				handle->offset.compressed = 0;
			}
			break;

			case StreamerSeekMode_Current:
			{
				STREAMER_PRINTF(("FileArchive: Cannot seek within compressed files\n"));
				return -1;
			}
			break;

			case StreamerSeekMode_End:
			{
				handle->offset.original = file->size.original;
				handle->offset.compressed = file->size.compressed;
			}
			break;
		}

		handle->buffer.offset = 0;
		handle->buffer.fill = 0;
	}

	return handle->offset.original;
}

static int FileArchive_LoadHeader(FileArchiveDriver* driver)
{
	STREAMER_PRINTF(("FileArchive: Loading header not re-implemented\n"));
	return -1;
/*
	int location, ret, size, entries;
	const unsigned char* curr;

	location = FileArchive_LocateHeader(driver);
	if (location < 0)
	{
		STREAMER_PRINTF(("FileArchive: Failed locating header for archive\n"));
		return -1;
	}

	driver->m_base = location;

	ret = driver->m_native.m_driver->lseek(driver->m_native.m_driver, driver->m_native.m_fd, location, StreamerSeekMode_Set);
	if (ret < 0)
	{
		STREAMER_PRINTF(("FileArchive: Failed seekign to header\n"));
		return -1;
	}

	size = driver->m_native.m_driver->read(driver->m_native.m_driver, driver->m_native.m_fd, driver->m_cache, FILEARCHIVE_CACHE_SIZE);
	if (size < 0)
	{
		STREAMER_PRINTF(("FileArchive: Failed reading header\n"));
		return -1;
	}

	entries = FileArchive_ParseHeader(driver, driver->m_cache, size);
	if (entries < 0)
	{
		STREAMER_PRINTF(("FileArchive: Failed parsing header\n"));
		return -1;
	}

	curr = (driver->m_cache + sizeof(unsigned int) * 8);
	size -= sizeof(unsigned int) * 8;

	while (entries-- > 0)
	{
		int nameSize, actualSize, ret;

		if (size < sizeof(unsigned int)*12)
		{
			int extra;
			memcpy(driver->m_cache, curr, size);

			extra = driver->m_native.m_driver->read(driver->m_native.m_driver, driver->m_native.m_fd, driver->m_cache + size, FILEARCHIVE_CACHE_SIZE - size);
			if (extra < 0)
			{
				STREAMER_PRINTF(("FileArchive: Failed re-filling while parsing file entries\n"));
				return -1;
			}

			size += extra;
			curr = driver->m_cache;

			if (size < sizeof(unsigned int)*12)
			{
				STREAMER_PRINTF(("FileArchive: Entry too short when re-filling\n"));
				return -1;
			}
		}

		memcpy(&nameSize, curr, sizeof(nameSize));
		actualSize = (sizeof(unsigned int) * 8 + sizeof(char) * 16 + nameSize + 15) & ~15;

		if (actualSize > size)
		{
			int extra;
			memcpy(driver->m_cache, curr, size);

			extra = driver->m_native.m_driver->read(driver->m_native.m_driver, driver->m_native.m_fd, driver->m_cache + size, FILEARCHIVE_CACHE_SIZE - size);
			if (extra < 0)
			{
				STREAMER_PRINTF(("FileArchive: Failed re-filling while parsing file entries\n"));
				return -1;
			}

			size += extra;
			curr = driver->m_cache;

			if (size < actualSize)
			{
				STREAMER_PRINTF(("FileArchive: Entry too short when re-filling (had %d, expected %d bytes)\n", size, actualSize));
				return -1;
			}
		}

		ret = FileArchive_ParseEntry(driver, curr, size);
		if (ret < 0)
		{
			STREAMER_PRINTF(("FileArchive: Could not parse file entry\n"));
			return -1;
		}

		curr += actualSize;
		size -= actualSize;
	}

	return 0;
*/
}

static int FileArchive_LocateHeader(FileArchiveDriver* driver)
{
	STREAMER_PRINTF(("FileArchive: LocateHeader not re-implemented\n"));
	return -1;
/*
	int eof, target, offset, ret, location;

	eof = driver->m_native.m_driver->lseek(driver->m_native.m_driver, driver->m_native.m_fd, 0, StreamerSeekMode_End);
	if (eof < 0)
	{
		STREAMER_PRINTF(("FileArchive: Failed seeking to end of file\n"));
		return -1;
	}

	target = eof > FILEARCHIVE_CACHE_SIZE ? eof - FILEARCHIVE_CACHE_SIZE : 0;

	offset = driver->m_native.m_driver->lseek(driver->m_native.m_driver, driver->m_native.m_fd, target, StreamerSeekMode_Set);
	if (offset != target)
	{
		STREAMER_PRINTF(("FileArchive: Failed seeking to tail\n"));
		return -1;
	}

	ret = driver->m_native.m_driver->read(driver->m_native.m_driver, driver->m_native.m_fd, driver->m_cache, eof - target);
	if (ret != (eof - target))
	{
		STREAMER_PRINTF(("FileArchive: Failed reading buffer for tail\n"));
		return -1;
	}

	location = -1;
	for (offset = (eof - target) - 4; offset >= 0; --offset)
	{
		unsigned int magic;
		memcpy(&magic, driver->m_cache + offset, sizeof(magic)); 

		if ((magic != FILEARCHIVE_TAIL_SIGNATURE) || (offset < 4))
		{
			continue;
		}

		memcpy(&location, driver->m_cache + offset - 4, sizeof(location));
		break;
	}

	if (location < 0 || (location > eof))
	{
		STREAMER_PRINTF(("FileArchive: Failed locating tail\n"));
		return -1;
	}

	location = eof - ((eof - target) - (offset - 4)) - location;

	return location;
*/
}

static int FileArchive_FillCache(FileArchiveDriver* driver, FileArchiveHandle* handle, const FileArchiveEntry* file, int minFill)
{
	int cacheFill = driver->cache.fill - driver->cache.offset;
	int cacheMax, fileMax, readMax, ret;

	if (cacheFill >= minFill)
	{
		return cacheFill;
	}

	cacheMax = FILEARCHIVE_CACHE_SIZE - cacheFill;
	fileMax = file->size.compressed - handle->offset.compressed - cacheFill;

	readMax = cacheMax > fileMax ? fileMax : cacheMax;

	ret = driver->native.driver->lseek(driver->native.driver, driver->native.fd, driver->base + file->data + handle->offset.compressed + cacheFill, StreamerSeekMode_Set);
	if (ret < 0)
	{
		STREAMER_PRINTF(("FileArchive: Failed seeking to %d in archive\n", driver->base + file->data + handle->offset.compressed + cacheFill));
		return -1;
	}

	memcpy(driver->cache.data, driver->cache.data + driver->cache.offset, cacheFill);

	ret = driver->native.driver->read(driver->native.driver, driver->native.fd, driver->cache.data + cacheFill, readMax);
	if (ret != readMax)
	{
		STREAMER_PRINTF(("FileArchive: Failed reading %d bytes from archive (ret: %d)\n", readMax, ret));
		return -1;
	}

	driver->cache.offset = 0;
	driver->cache.fill = cacheFill + readMax;

	if ((int)driver->cache.fill < minFill)
	{
		STREAMER_PRINTF(("FileArchive: Failed filling cache, wanted %d bytes but could only get %d bytes\n", minFill, driver->cache.fill));
		return -1;
	}

	return driver->cache.fill;
}

