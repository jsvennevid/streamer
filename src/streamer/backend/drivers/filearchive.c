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

static const FileArchiveEntry* FileArchive_FindByName(FileArchiveDriver* driver, const char* filename);
static const FileArchiveEntry* FileArchive_FindByHash(FileArchiveDriver* driver, const FileArchiveHash* hash);

static int FileArchive_LoadTOC(FileArchiveDriver* driver);
static uint32_t FileArchive_LocateFooter(FileArchiveDriver* driver);

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

	if (FileArchive_LoadTOC(driver) < 0)
	{
		STREAMER_PRINTF(("FileArchive: Could not load TOC\n"));
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
	FileArchiveDriver* local = (FileArchiveDriver*)driver;
	const FileArchiveEntry* entry;
	int i;

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

	if (*filename == '@')
	{
		FileArchiveHash hash;
		const char* begin = filename + 1;

		memset(&hash, 0, sizeof(hash));
		for (i = 0; (i < sizeof(hash.data) * 2) && *begin; ++i, ++begin)
		{
			uint8_t value;

			if ((*begin >= '0') && (*begin <= '9'))
			{
				value = *begin - '0';
			}
			else if ((*begin >= 'A') && (*begin <= 'F'))
			{
				value = 10 + (*begin - 'A');
			}
			else if ((*begin >= 'a') && (*begin <= 'f'))
			{
				value = 10 + (*begin - 'a');
			}
			else
			{
				break;
			}

			hash.data[i >> 1] |= (value << (((i & 1)^1) * 4));
		}

		if (i != sizeof(hash.data) * 2)
		{
			STREAMER_PRINTF(("FileArchive: Invalid hash length (%d != %d)\n", i, (int)sizeof(hash.data) * 2));
			return -1;
		}

		entry = FileArchive_FindByHash(local, &hash);
	}
	else
	{
		entry = FileArchive_FindByName(local, filename);
	}

	if (entry == NULL)
	{
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

static const FileArchiveEntry* FileArchive_FindByName(FileArchiveDriver* driver, const char* filename)
{
	const FileArchiveContainer* container;
	const FileArchiveEntry* entry;
	const char* begin = filename;
	const char* end = begin + strlen(filename);
	unsigned int i, n;

	container = (const FileArchiveContainer*)(((const char*)driver->toc) + driver->toc->containers);
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

			container = (const FileArchiveContainer*)(((const char*)driver->toc) + offset);
			name = container->name != FILEARCHIVE_INVALID_OFFSET ? ((const char*)driver->toc) + container->name : "";

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
			return NULL;
		}

		begin = curr + 1;
	}
	while (1);

	if (begin == end)
	{
		STREAMER_PRINTF(("FileArchive: Invalid filename when opening '%s'\n", filename));
		return NULL;
	}

	entry = (const FileArchiveEntry*)(((const char*)driver->toc) + container->files);
	for (i = 0, n = container->count; i < n; ++i, ++entry)
	{
		const char* name = entry->name != FILEARCHIVE_INVALID_OFFSET ? ((const char*)driver->toc) + entry->name : "";
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
		return NULL;
	}

	return entry;
}

static const FileArchiveEntry* FileArchive_FindByHash(FileArchiveDriver* driver, const FileArchiveHash* hash)
{
	const FileArchiveHash* begin = (const FileArchiveHash*)(((const uint8_t*)(driver->toc)) + driver->toc->hashes);
	const FileArchiveHash* end = begin + driver->toc->fileCount;
	const FileArchiveEntry* files = (const FileArchiveEntry*)(((const uint8_t*)(driver->toc)) + driver->toc->files);
	const FileArchiveHash* curr = begin;

	for (; begin < end; ++begin)
	{
		if (!memcmp(hash, begin, sizeof(FileArchiveHash)))
		{
			break;
		}

	}

	if (begin == end)
	{
		STREAMER_PRINTF(("FileArchive: Could not find hash\n"));
		return NULL;
	}

	return files + (curr - begin);
}

static int FileArchive_LoadTOC(FileArchiveDriver* driver)
{
	uint32_t tail = FILEARCHIVE_INVALID_OFFSET;
	FileArchiveFooter footer;
	int ret;

	tail = FileArchive_LocateFooter(driver);
	if (tail == FILEARCHIVE_INVALID_OFFSET)
	{
		STREAMER_PRINTF(("FileArchive: Failed to locate footer for archive\n"));
		return -1;
	}

	ret = driver->native.driver->lseek(driver->native.driver, driver->native.fd, tail, StreamerSeekMode_Set);
	if (ret < 0)
	{
		STREAMER_PRINTF(("FileArchive: Failed seeking to tail\n"));
		return -1;
	}

	ret = driver->native.driver->read(driver->native.driver, driver->native.fd, &footer, sizeof(footer));
	if (ret != sizeof(footer))
	{
		STREAMER_PRINTF(("FileArchive: Failed reading tail\n"));
		return -1;
	}

	if (footer.cookie != FILEARCHIVE_MAGIC_COOKIE)
	{
		STREAMER_PRINTF(("FileArchive: Mismatching magic cookie\n"));
		return -1;
	}

	if ((footer.toc > tail) || (footer.data > tail))
	{
		STREAMER_PRINTF(("FileArchive: Invalid data location\n"));
		return -1;
	}

	if (footer.size.original > (tail - footer.toc))
	{
		STREAMER_PRINTF(("FileArchive: Invalid TOC size\n"));
	}

	ret = driver->native.driver->lseek(driver->native.driver, driver->native.fd, tail - footer.toc, StreamerSeekMode_Set);
	if (ret < 0)
	{
		STREAMER_PRINTF(("FileArchive: Could not seek to TOC\n"));
		return -1;
	}

#if defined(_IOP)
	driver->toc = AllocSysMemory(ALLOC_FIRST, footer.size.original, 0);
#else
	driver->toc = malloc(footer.size.original);
#endif
	if (!driver->toc)
	{
		STREAMER_PRINTF(("FileArchive: Failed to allocate memory for TOC\n"));
		return -1;
	}

	if (footer.compression == FILEARCHIVE_COMPRESSION_NONE)
	{
		ret = driver->native.driver->read(driver->native.driver, driver->native.fd, driver->toc, footer.size.original);
		if ((uint32_t)ret != footer.size.original)
		{
			STREAMER_PRINTF(("FileArchive: Failed to read TOC\n"));
			return -1;
		}
	}
	else
	{
		uint32_t length = footer.size.compressed;
		uint32_t offset = 0, cacheUsage = 0;

		switch (footer.compression)
		{
			case FILEARCHIVE_COMPRESSION_NONE: case FILEARCHIVE_COMPRESSION_FASTLZ: break;
			default:
			{
				STREAMER_PRINTF(("FileArchive: Unsupported compression scheme used for TOC\n"));
				return -1;
			}
			break;
		}

		while ((offset != footer.size.original) && (length > 0))
		{
			size_t maxRead = (FILEARCHIVE_CACHE_SIZE - cacheUsage) > length ? length : (FILEARCHIVE_CACHE_SIZE - cacheUsage);
			uint8_t* begin;
			uint8_t* end;

			ret = driver->native.driver->read(driver->native.driver, driver->native.fd, driver->cache.data + cacheUsage, maxRead);
			if ((uint32_t)ret != maxRead)
			{
				STREAMER_PRINTF(("FileArchive: Short read while reading TOC block\n"));
				return -1;
			}

			begin = driver->cache.data;
			end = begin + maxRead + cacheUsage;

			while (begin != end)
			{
				FileArchiveCompressedBlock block;

				if ((end - begin) < sizeof(FileArchiveCompressedBlock))
				{
					break;
				}

				memcpy(&block, begin, sizeof(FileArchiveCompressedBlock));

				if (block.compressed & FILEARCHIVE_COMPRESSION_SIZE_IGNORE)
				{
					if ((block.compressed & FILEARCHIVE_COMPRESSION_SIZE_MASK) != block.original)
					{
						STREAMER_PRINTF(("FileArchive: Invalid compressed TOC block\n"));
						return -1;
					}

					if (block.original > ((end - begin) - sizeof(FileArchiveCompressedBlock)))
					{
						break;
					}

					memcpy(((uint8_t*)driver->toc) + offset, begin + sizeof(FileArchiveCompressedBlock), block.original);
				}
				else
				{
					if (block.compressed > ((end - begin) - sizeof(FileArchiveCompressedBlock)))
					{
						break;
					}

					switch (footer.compression)
					{
						case FILEARCHIVE_COMPRESSION_FASTLZ:
						{
							ret = fastlz_decompress(begin + sizeof(FileArchiveCompressedBlock), block.compressed, ((uint8_t*)driver->toc) + offset, FILEARCHIVE_BUFFER_SIZE > (footer.size.original - offset) ? (footer.size.original - offset) : FILEARCHIVE_BUFFER_SIZE);
							if (ret != block.original)
							{
								STREAMER_PRINTF(("FileArchive: Failed to decompress TOC block\n"));
								return -1;
							}
						}
						break;
					}
				}

				offset += block.original;
				begin += sizeof(FileArchiveCompressedBlock) + (block.compressed & FILEARCHIVE_COMPRESSION_SIZE_MASK);
			}

			memcpy(driver->cache.data, begin, end-begin);
			cacheUsage = end-begin;

			length -= maxRead;
		}

		if ((offset != footer.size.original) || (length > 0))
		{
			STREAMER_PRINTF(("FileArchive: Failed to read compressed TOC\n"));
			return -1;
		}
	}

	driver->base = tail - footer.data;
	return 0;
}

static uint32_t FileArchive_LocateFooter(FileArchiveDriver* driver)
{
	int eof, target, offset, ret, location;

	eof = driver->native.driver->lseek(driver->native.driver, driver->native.fd, 0, StreamerSeekMode_End);
	if (eof < 0)
	{
		STREAMER_PRINTF(("FileArchive: Failed seeking to end of file\n"));
		return FILEARCHIVE_INVALID_OFFSET;
	}

	target = eof > FILEARCHIVE_CACHE_SIZE ? eof - FILEARCHIVE_CACHE_SIZE : 0;

	offset = driver->native.driver->lseek(driver->native.driver, driver->native.fd, target, StreamerSeekMode_Set);
	if (offset != target)
	{
		STREAMER_PRINTF(("FileArchive: Failed seeking to tail of file\n"));
		return FILEARCHIVE_INVALID_OFFSET;
	}

	ret = driver->native.driver->read(driver->native.driver, driver->native.fd, driver->cache.data, eof - target);
	if (ret != (eof - target))
	{
		STREAMER_PRINTF(("FileArchive: Failed reading buffer for tail\n"));
		return FILEARCHIVE_INVALID_OFFSET;
	}

	location = -1;
	for (offset = (eof - target) - 4; offset >= 0; --offset)
	{
		uint32_t magic;
		memcpy(&magic, driver->cache.data + offset, sizeof(magic)); 

		if ((magic != FILEARCHIVE_MAGIC_COOKIE) || (offset < 4))
		{
			continue;
		}

		location = target + offset;
		break;
	}

	if (location < 0)
	{
		STREAMER_PRINTF(("FileArchive: Failed locating tail\n"));
		return FILEARCHIVE_INVALID_OFFSET;
	}

	return location;
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

