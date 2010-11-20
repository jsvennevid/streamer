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
#include "lzo.h"

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

#define FILEARCHIVE_MAX_CONTAINERS (64)

#define FILEARCHIVE_STATIC_MEMORY_SIZE (FILEARCHIVE_MAX_HANDLES * FILEARCHIVE_BUFFER_SIZE + FILEARCHIVE_CACHE_SIZE)

#define FILEARCHIVE_HEADER_SIGNATURE (('Z' << 24) | ('F' << 16) | ('A' << 8) | ('R'))
#define FILEARCHIVE_TAIL_SIGNATURE (('Z' << 24) | ('T' << 16) | ('A' << 8) | ('I'))

#define FILEARCHIVE_COMPRESSION_UNCOMPRESSED (0)
#define FILEARCHIVE_COMPRESSION_LZO (('L' << 24) | ('Z' << 16) | ('O' << 8) | ('0'))

#define FILEARCHIVE_COMPRESSION_SIZE_IGNORE 0x8000
#define FILEARCHIVE_COMPRESSION_SIZE_MASK 0x7fff

void FileArchive_Destroy(struct IODriver* driver);
int FileArchive_Open(struct IODriver* driver, const char* filename, StreamerOpenMode mode);
int FileArchive_Close(struct IODriver* driver, int fd);
int FileArchive_Read(struct IODriver* driver, int fd, void* buffer, unsigned int length);
int FileArchive_LSeek(struct IODriver* driver, int fd, int offset, StreamerSeekMode whence);

int FileArchive_LoadHeader(FileArchiveDriver* driver);
int FileArchive_LocateHeader(FileArchiveDriver* driver);
int FileArchive_ParseHeader(FileArchiveDriver* driver, const void* header, int length);
int FileArchive_ParseEntry(FileArchiveDriver* driver, const void* entry, int length);

FileArchiveContainer* FileArchive_FindContainer(FileArchiveContainer* curr, const char* begin, const char* end);
int FileArchive_FillCache(FileArchiveDriver* driver, FileArchiveHandle* handle, FileArchiveEntry* file, int minFill);

IODriver* FileArchive_Create(IODriver* native, const char* file)
{
	int i;
#if defined(_IOP)
	FileArchiveDriver* driver = AllocSysMemory(ALLOC_FIRST, sizeof(FileArchiveDriver), 0);
#else
	FileArchiveDriver* driver = malloc(sizeof(FileArchiveDriver));
#endif
	memset(driver, 0, sizeof(FileArchiveDriver));

	driver->m_interface.destroy = FileArchive_Destroy;
	driver->m_interface.open = FileArchive_Open;
	driver->m_interface.close = FileArchive_Close;
	driver->m_interface.read = FileArchive_Read;
	driver->m_interface.lseek = FileArchive_LSeek;

	if (native->align && native->align(native) > 0)
	{
		STREAMER_PRINTF(("FileArchive: Cannot handle pre-aligned I/O\n"));
		FileArchive_Destroy(&(driver->m_interface));
		return 0;
	}

	driver->m_native.m_fd = native->open(native, file, StreamerOpenMode_Read);
	if (driver->m_native.m_fd < 0)
	{
		STREAMER_PRINTF(("FileArchive: Could not open native file '%s'\n", file));
		FileArchive_Destroy(&(driver->m_interface));
		return 0;
	}
	driver->m_native.m_driver = native;

#if defined(_IOP)
	driver->m_static = AllocSysMemory(ALLOC_FIRST, FILEARCHIVE_STATIC_MEMORY_SIZE, 0);
#else
	driver->m_static = malloc(FILEARCHIVE_STATIC_MEMORY_SIZE);
#endif
	if (!driver->m_static)
	{
		STREAMER_PRINTF(("FileArchive: Failed allocating %d bytes for cache and buffers\n", FILEARCHIVE_STATIC_MEMORY_SIZE));
		FileArchive_Destroy(&(driver->m_interface));
	}

	driver->m_cache = driver->m_static + FILEARCHIVE_MAX_HANDLES * FILEARCHIVE_BUFFER_SIZE;
	for (i = 0; i < FILEARCHIVE_MAX_HANDLES; ++i)
	{
		driver->m_handles[i].m_buffer = driver->m_static + i * FILEARCHIVE_BUFFER_SIZE;
	}

	if (FileArchive_LoadHeader(driver) < 0)
	{
		STREAMER_PRINTF(("FileArchive: Could not load header\n"));
		FileArchive_Destroy(&(driver->m_interface));
		return 0;
	}

	STREAMER_PRINTF(("FileArchive: Driver created\n"));

	driver->m_cacheOwner = -1;
	return &(driver->m_interface);
}

void FileArchive_Destroy(struct IODriver* driver)
{
	FileArchiveDriver* local = (FileArchiveDriver*)driver;

	if (local->m_dynamic)
	{
#if defined(_IOP)
		FreeSysMemory(local->m_dynamic);
#else
		free(local->m_dynamic);
#endif
	}

	if (local->m_static)
	{
#if defined(_IOP)
		FreeSysMemory(local->m_static);
#else
		free(local->m_static);
#endif
	}

	if (local->m_native.m_driver && (local->m_native.m_fd >= 0))
	{
		local->m_native.m_driver->close(local->m_native.m_driver, local->m_native.m_fd);
	}

#if defined(_IOP)
	FreeSysMemory(driver);
#else
	free(driver);
#endif

	STREAMER_PRINTF(("FileArchive: Driver destroyed\n"));
}

int FileArchive_Open(struct IODriver* driver, const char* filename, StreamerOpenMode mode)
{
	const char* begin, * end;
	FileArchiveDriver* local = (FileArchiveDriver*)driver;
	FileArchiveContainer* container = &(local->m_root);
	FileArchiveEntry* entry,* candidate;
	int i;

	STREAMER_PRINTF(("FileArchive: open(\"%s\", %d)\n", filename, mode));

	if (mode != StreamerOpenMode_Read)
	{
		STREAMER_PRINTF(("FileArchive: Invalid mode when opening file\n"));
		return -1;
	}

	begin = filename;
	end = filename + strlen(filename);

	do
	{
		const char* curr = begin;
		while ((curr != end) && (*curr != '/'))
		{
			++curr;
		}

		if (curr == end)
		{
			break;
		}

		container = FileArchive_FindContainer(container, begin, curr);
		if (!container)
		{
			STREAMER_PRINTF(("FileArchive: Could not locate folder for '%s'\n", filename));
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

	for (entry = 0, candidate = container->m_files; candidate; candidate = candidate->m_next)
	{
		const char* a,* b;

		if (strlen(candidate->m_name) != (size_t)(end-begin))
		{
			continue;
		}

		for (a = candidate->m_name, b = begin; (tolower(*a) == tolower(*b)) && (b != end); ++a, ++b);

		if (b != end)
		{
			continue;
		}

		entry = candidate;
		break;
	}

	if (!entry)
	{
		STREAMER_PRINTF(("FileArchive: Could not find file '%s'\n", filename));
		return -1;
	}

	for (i = 0; i < FILEARCHIVE_MAX_HANDLES; ++i)
	{
		FileArchiveHandle* handle = &(local->m_handles[i]);

		if (handle->m_file)
		{
			continue;
		}

		handle->m_file = entry;

		handle->m_offset = 0;
		handle->m_compressedOffset = 0;

		handle->m_bufferOffset = 0;
		handle->m_bufferFill = 0;

		return i;
	}

	STREAMER_PRINTF(("FileArchive: No file handle available\n"));
	return -1;
}

int FileArchive_Close(struct IODriver* driver, int fd)
{
	FileArchiveDriver* local = (FileArchiveDriver*)driver;

	STREAMER_PRINTF(("FileArchive: close(%d)\n", fd));

	if ((fd < 0) || (fd >= FILEARCHIVE_MAX_HANDLES))
	{
		STREAMER_PRINTF(("FileArchive: Invalid file handle\n"));
		return -1;
	}

	if (!local->m_handles[fd].m_file)
	{
		STREAMER_PRINTF(("FileArchive: File not opened\n"));
		return -1;
	}

	if (local->m_cacheOwner == fd)
	{
		local->m_cacheOwner = -1;
	}

	local->m_handles[fd].m_file = 0;
	return 0;
}

int FileArchive_Read(struct IODriver* driver, int fd, void* buffer, unsigned int length)
{
	FileArchiveDriver* local = (FileArchiveDriver*)driver;
	FileArchiveHandle* handle;
	FileArchiveEntry* file;

	if ((fd < 0) || (fd >= FILEARCHIVE_MAX_HANDLES))
	{
		STREAMER_PRINTF(("FileArchive: Invalid file handle\n"));
		return -1;
	}

	handle = &(local->m_handles[fd]);
	file = handle->m_file;
	if (!file)
	{
		STREAMER_PRINTF(("FileArchive: File handle not opened\n"));
		return -1;
	}

	switch (handle->m_file->m_compression)
	{
		case FILEARCHIVE_COMPRESSION_UNCOMPRESSED:
		{
			int maxRead = (file->m_originalSize - handle->m_offset) < length ? (file->m_originalSize - handle->m_offset) : length;
			int result;

			result = local->m_native.m_driver->lseek(local->m_native.m_driver, local->m_native.m_fd, local->m_base + file->m_offset + handle->m_offset, StreamerSeekMode_Set);
			if (result < 0)
			{
				STREAMER_PRINTF(("FileArchive: Failed seeking to uncompressed location (%d)\n", result));
				return result;
			}

			result = local->m_native.m_driver->read(local->m_native.m_driver, local->m_native.m_fd, buffer, maxRead);
			if (result != maxRead)
			{
				STREAMER_PRINTF(("FileArchive: Failed reading %d uncompressed bytes from archive (%d)\n", maxRead, result));
				return -1;
			}

			handle->m_offset += result;
			return result;
		}
		break;

		case FILEARCHIVE_COMPRESSION_LZO:
		{
			int actual = 0;

			if (local->m_cacheOwner != fd)
			{
				local->m_cacheOffset = 0;
				local->m_cacheFill = 0;
				local->m_cacheOwner = fd;
			}

			length = length < (unsigned int)(file->m_originalSize - handle->m_offset) ? length : (file->m_originalSize - handle->m_offset);
			while (length > 0)
			{
				int maxRead, bufferRead;

				if (handle->m_bufferFill == handle->m_bufferOffset)
				{
					unsigned short originalSize, compressedSize, cacheUsage;

					if (FileArchive_FillCache(local, handle, file, sizeof(unsigned short)*2) < 0)
					{
						STREAMER_PRINTF(("FileArchive: Error while filling compression cache\n"));
						return -1;
					}

					memcpy(&originalSize, local->m_cache + local->m_cacheOffset, sizeof(originalSize));
					memcpy(&compressedSize, local->m_cache + local->m_cacheOffset + 2, sizeof(compressedSize));

					if (originalSize > FILEARCHIVE_BUFFER_SIZE)
					{
						STREAMER_PRINTF(("FileArchive: Decompressed block too large (max: %d, was: %d)\n", FILEARCHIVE_BUFFER_SIZE, originalSize));
						return -1;
					}

					if (FileArchive_FillCache(local, handle, file, sizeof(unsigned short)*2 + (compressedSize & FILEARCHIVE_COMPRESSION_SIZE_MASK)) < 0)
					{
						STREAMER_PRINTF(("FileArchive: Error while filling compression cache\n"));
						return -1;
					}

					if (compressedSize & FILEARCHIVE_COMPRESSION_SIZE_IGNORE)
					{
						compressedSize &= FILEARCHIVE_COMPRESSION_SIZE_MASK;

						if (compressedSize != originalSize)
						{
							STREAMER_PRINTF(("FileArchive: Uncompressed block size mismatch\n"));
							return -1;
						}

						memcpy(handle->m_buffer, local->m_cache + local->m_cacheOffset + sizeof(unsigned short)*2, compressedSize);
					}
					else
					{
						if (streamer_lzo_decompress(handle->m_buffer, originalSize, local->m_cache + local->m_cacheOffset + sizeof(unsigned short)*2, compressedSize) < 0)
						{
							STREAMER_PRINTF(("FileArchive: Error while decompressing LZO block\n"));
							return -1;
						}
					}

					cacheUsage = compressedSize + sizeof(unsigned short)*2;
					handle->m_compressedOffset += cacheUsage;
					local->m_cacheOffset += cacheUsage;

					handle->m_bufferOffset = 0;
					handle->m_bufferFill = originalSize;
				}

				bufferRead = handle->m_bufferFill - handle->m_bufferOffset;
				maxRead = (unsigned int)bufferRead > length ? length : bufferRead;

				if (!maxRead)
				{
					break;
				}

				memcpy(buffer, handle->m_buffer + handle->m_bufferOffset, maxRead);

				buffer = ((char*)buffer) + maxRead;
				length -= maxRead;
				actual += maxRead;

				handle->m_bufferOffset += maxRead;
				handle->m_offset += maxRead;
			}

			return actual;
		}
		break;
	}

	return -1;
}

int FileArchive_LSeek(struct IODriver* driver, int fd, int offset, StreamerSeekMode whence)
{
	FileArchiveDriver* local = (FileArchiveDriver*)driver;
	FileArchiveHandle* handle;
	FileArchiveEntry* file;
	int newOffset = 0;

	STREAMER_PRINTF(("FileArchive: lseek(%d, %d, %d)\n", fd, offset, whence));

	if ((fd < 0) || (fd >= FILEARCHIVE_MAX_HANDLES))
	{
		STREAMER_PRINTF(("FileArchive: Invalid file handle\n"));
		return -1;
	}

	handle = &(local->m_handles[fd]);
	file = handle->m_file;
	if (!file)
	{
		STREAMER_PRINTF(("FileArchive: File handle not opened\n"));
		return -1;
	}

	switch (file->m_compression)
	{
		case FILEARCHIVE_COMPRESSION_UNCOMPRESSED:
		{
			switch (whence)
			{
				case StreamerSeekMode_Set: newOffset = offset; break;
				case StreamerSeekMode_Current: newOffset = handle->m_offset + offset; break;
				case StreamerSeekMode_End: newOffset = file->m_originalSize + offset; break;
			}

			if ((newOffset < 0) || (newOffset > (int)file->m_originalSize))
			{
				STREAMER_PRINTF(("FileArchive: Seeking out of bounds\n"));
				return -1;
			}

			handle->m_offset = newOffset;
		}
		break;

		case FILEARCHIVE_COMPRESSION_LZO:
		{
			if ((offset != 0) || (whence == StreamerSeekMode_Current))
			{
				STREAMER_PRINTF(("FileArchive: Can only seek to beginning or end of compressed files\n"));
				return -1;
			}

			if (fd == local->m_cacheOwner)
			{
				local->m_cacheOwner = -1;
			}

			switch (whence)
			{
				case StreamerSeekMode_Set:
				{
					handle->m_offset = 0;
					handle->m_compressedOffset = 0;
				}
				break;

				case StreamerSeekMode_End:
				{
					handle->m_offset = file->m_originalSize;
					handle->m_compressedOffset = file->m_compressedSize;
				}
				break;
			}

			handle->m_bufferOffset = 0;
			handle->m_bufferFill = 0;
		}
		break;
	}

	return handle->m_offset;
}

int FileArchive_LoadHeader(FileArchiveDriver* driver)
{
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
}

int FileArchive_LocateHeader(FileArchiveDriver* driver)
{
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
}

int FileArchive_ParseHeader(FileArchiveDriver* driver, const void* header, int length)
{
	unsigned int magic, version, entries;
	unsigned short containers, flags;
	const char* local = (const char*)header;

	if (length < (sizeof(unsigned int) * 8))
	{
		STREAMER_PRINTF(("FileArchive: Header is too small\n"));
		return -1;
	}

	memcpy(&magic, local + 0, sizeof(magic));
	if (magic != FILEARCHIVE_HEADER_SIGNATURE)
	{
		STREAMER_PRINTF(("FileArchive: Header magic mismatches\n"));
		return -1;
	}

	memcpy(&version, local + 4, sizeof(version));
	if (version != 1)
	{
		STREAMER_PRINTF(("FileArchive: Header version not valid\n"));
		return -1;
	}

	memcpy(&entries, local + 8, sizeof(entries));
	memcpy(&containers, local + 12, sizeof(containers));
	memcpy(&flags, local + 14, sizeof(flags));

#if defined(_IOP)
	driver->m_dynamic = AllocSysMemory(ALLOC_FIRST, sizeof(FileArchiveContainer) * containers + sizeof(FileArchiveEntry) * entries, 0);
#else
	driver->m_dynamic = malloc(sizeof(FileArchiveContainer) * containers + sizeof(FileArchiveEntry) * entries);
#endif
	if (!driver->m_dynamic)
	{
		STREAMER_PRINTF(("FileArchive: Failed allocating dynamic memory\n"));
		return -1;
	}

	driver->m_currContainer = (FileArchiveContainer*)driver->m_dynamic;
	driver->m_containersLeft = containers;

	driver->m_currEntry = (FileArchiveEntry*)(driver->m_dynamic + sizeof(FileArchiveContainer) * containers);
	driver->m_entriesLeft = entries;

	return entries;
}

int FileArchive_ParseEntry(FileArchiveDriver* driver, const void* entry, int length)
{
	int nameSize, actualSize;
	const char* local = entry;
	const char* nameBegin, *nameEnd;
	FileArchiveContainer* curr = &(driver->m_root);
	FileArchiveEntry* result;

	memcpy(&nameSize, local + 0, sizeof(nameSize));
	actualSize = (sizeof(unsigned int) * 8 + sizeof(char) * 16 + nameSize + 15) & ~15;

	if (length < actualSize)
	{
		STREAMER_PRINTF(("FileArchive: Out of data when parsing entry\n"));
		return -1;
	}

	nameBegin = local + sizeof(unsigned int) * 8 + sizeof(char) * 16;
	nameEnd = nameBegin + nameSize;

	do
	{
		const char* nameCurr = nameBegin;
		FileArchiveContainer* actual;

		while ((nameCurr != nameEnd) && (*nameCurr != '/'))
		{
			++nameCurr;
		}

		if (nameCurr == nameEnd)
		{
			break;
		}

		actual = FileArchive_FindContainer(curr, nameBegin, nameCurr);
		if (!actual)
		{
			if (driver->m_containersLeft-- <= 0)
			{
				STREAMER_PRINTF(("FileArchive: Ran out of containers while parsing entry\n"));
				return -1;
			}

			actual = driver->m_currContainer++;
			memset(actual, 0, sizeof(FileArchiveContainer));

			strncpy(actual->m_name, nameBegin, nameCurr - nameBegin);
			actual->m_name[(nameCurr - nameBegin) >= sizeof(actual->m_name) ? sizeof(actual->m_name)-1 : (nameCurr - nameBegin)] = '\0';

			actual->m_parent = curr;
			actual->m_next = curr->m_children;
			curr->m_children = actual;
		}

		curr = actual;

		nameBegin = nameCurr + 1;
	}
	while (1);

	if (nameBegin == nameEnd)
	{
		STREAMER_PRINTF(("FileArchive: Invalid filename while parsing entry\n"));
		return -1;
	}

	if (driver->m_entriesLeft-- <= 0)
	{
		STREAMER_PRINTF(("FileArchive: Out of entries while parsing\n"));
		return -1;
	}

	result = driver->m_currEntry++;
	memset(result, 0, sizeof(FileArchiveEntry));

	result->m_container = curr;
	result->m_next = curr->m_files;
	curr->m_files = result;

	memcpy(&(result->m_compression), local + 4, sizeof(result->m_compression));
	memcpy(&(result->m_offset), local + 8, sizeof(result->m_offset));
	memcpy(&(result->m_originalSize), local + 12, sizeof(result->m_originalSize));
	memcpy(&(result->m_compressedSize), local + 16, sizeof(result->m_compressedSize));
	memcpy(&(result->m_blockSize), local + 20, sizeof(result->m_blockSize));
	memcpy(&(result->m_maxCompressedBlock), local + 28, sizeof(result->m_maxCompressedBlock)); 

	strncpy(result->m_name, nameBegin, nameEnd - nameBegin);
	result->m_name[(nameEnd - nameBegin) >= sizeof(result->m_name) ? sizeof(result->m_name)-1 : (nameEnd - nameBegin)] = '\0';

	return 0;
}

FileArchiveContainer* FileArchive_FindContainer(FileArchiveContainer* curr, const char* begin, const char* end)
{
	for (curr = curr->m_children; curr; curr = curr->m_next)
	{
		const char* a,* b;

		if (strlen(curr->m_name) != (size_t)(end - begin))
		{
			continue;
		}

		for (a = curr->m_name, b = begin; (tolower(*a) == tolower(*b)) && (b != end); ++a, ++b);

		if (b != end)
		{
			continue;
		}

		return curr;
	}

	return 0;
}

int FileArchive_FillCache(FileArchiveDriver* driver, FileArchiveHandle* handle, FileArchiveEntry* file, int minFill)
{
	int cacheFill = driver->m_cacheFill - driver->m_cacheOffset;
	int cacheMax, fileMax, readMax, ret;

	if (cacheFill >= minFill)
	{
		return cacheFill;
	}

	cacheMax = FILEARCHIVE_CACHE_SIZE - cacheFill;
	fileMax = file->m_compressedSize - handle->m_compressedOffset - cacheFill;

	readMax = cacheMax > fileMax ? fileMax : cacheMax;

	ret = driver->m_native.m_driver->lseek(driver->m_native.m_driver, driver->m_native.m_fd, driver->m_base + file->m_offset + handle->m_compressedOffset + cacheFill, StreamerSeekMode_Set);
	if (ret < 0)
	{
		STREAMER_PRINTF(("FileArchive: Failed seeking to %d in archive\n", driver->m_base + file->m_offset + handle->m_compressedOffset + cacheFill));
		return -1;
	}

	memcpy(driver->m_cache, driver->m_cache + driver->m_cacheOffset, cacheFill);

	ret = driver->m_native.m_driver->read(driver->m_native.m_driver, driver->m_native.m_fd, driver->m_cache + cacheFill, readMax);
	if (ret != readMax)
	{
		STREAMER_PRINTF(("FileArchive: Failed reading %d bytes from archive (ret: %d)\n", readMax, ret));
		return -1;
	}

	driver->m_cacheOffset = 0;
	driver->m_cacheFill = cacheFill + readMax;

	if ((int)driver->m_cacheFill < minFill)
	{
		STREAMER_PRINTF(("FileArchive: Failed filling cache, wanted %d bytes but could only get %d bytes\n", minFill, driver->m_cacheFill));
		return -1;
	}

	return driver->m_cacheFill;
}
