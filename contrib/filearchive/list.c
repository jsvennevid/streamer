#include "filearchive.h"

#include <fastlz/fastlz.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* buildContainerName(const FileArchiveContainer* container, const void* toc, unsigned int extraSize)
{
	const char* name = container->name != FILEARCHIVE_INVALID_OFFSET ? ((const char*)toc) + container->name : "";
	int len = strlen(name);
	char* buf;

	if (container->parent != FILEARCHIVE_INVALID_OFFSET)
	{
		buf = buildContainerName((const FileArchiveContainer*)(((const char*)toc) + container->parent), toc, extraSize + len + 1);

		sprintf(buf, "%s%s/", buf, name);
		return buf;
	}

	buf = malloc(len + 1 + extraSize);
	memcpy(buf, name, len + 1);

	return buf;
}

static int decompressStream(FILE* inp, void* out, size_t compressedSize, size_t originalSize, uint32_t compression, uint32_t blockSize)
{
	char* buffer = malloc(blockSize) + sizeof(FileArchiveCompressedBlock);
	size_t bufferUse = 0;
	size_t curr, used;

	if (compression == FILEARCHIVE_COMPRESSION_NONE)
	{
		if (fread(out, 1, originalSize, inp) != originalSize)
		{
			return -1;
		}

		return 0;
	}

	for (curr = 0; curr != compressedSize;)
	{
		size_t maxSize = (compressedSize - curr) < ((blockSize - bufferUse) + sizeof(FileArchiveCompressedBlock)) ? compressedSize - curr : (blockSize - bufferUse) + sizeof(FileArchiveCompressedBlock);
		FileArchiveCompressedBlock* block = (FileArchiveCompressedBlock*)buffer;

		if (fread(buffer + bufferUse, 1, maxSize, inp) != maxSize)
		{
			return -1;
		}
		bufferUse += maxSize;

		if (block->compressed & FILEARCHIVE_COMPRESSION_SIZE_IGNORE)
		{
			memcpy(out, buffer + sizeof(FileArchiveCompressedBlock), block->original);
			used = block->original + sizeof(FileArchiveCompressedBlock);
		}
		else
		{
			switch (compression)
			{
				case FILEARCHIVE_COMPRESSION_FASTLZ: 
				{
					if (fastlz_decompress(buffer + sizeof(FileArchiveCompressedBlock), block->compressed, out, block->original) != block->original)
					{
						return -1;
					}
				}
				break;

				default:
				{
					return -1;
				}
				break;
			}
			used = block->compressed + sizeof(FileArchiveCompressedBlock);
		}

		memmove(buffer, buffer + used, bufferUse - used);
		bufferUse -= used;

		out = ((char*)out) + used;
		curr += used;
	}

	return 0;
} 

static int listArchive(const char* path)
{
	FILE* inp = NULL;
	int result = -1;
	FileArchiveFooter footer;
	unsigned int i;

	FileArchiveHeader* header = NULL;
	char* toc = NULL;

	do
	{
		if ((inp = fopen(path, "rb")) == NULL)
		{
			fprintf(stderr, "dir: Failed to open archive \"%s\"\n", path);
			break;
		}

		if (fseek(inp, -sizeof(FileArchiveFooter), SEEK_END) < 0)
		{
			fprintf(stderr, "dir: Could not seek to end of file\n");
			break;
		}

		if (fread(&footer, 1, sizeof(footer), inp) != sizeof(footer))
		{
			fprintf(stderr, "dir: Failed to read footer\n");
			break;
		}

		if (footer.cookie != FILEARCHIVE_MAGIC_COOKIE)
		{
			fprintf(stderr, "dir: Mismatching cookie\n");
			break;
		}

		toc = malloc(footer.size.original);
		if (toc == NULL)
		{
			fprintf(stderr, "dir: Failed allocating TOC (%u bytes)\n", footer.size.original);
			break;
		}

		if (fseek(inp, -(sizeof(FileArchiveFooter) + footer.toc), SEEK_END) < 0)
		{
			fprintf(stderr, "dir: Could not seek to TOC\n");
			break;
		}

		if (decompressStream(inp, toc, footer.size.compressed, footer.size.original, footer.compression, FILEARCHIVE_COMPRESSION_BLOCK_SIZE) < 0)
		{
			fprintf(stderr, "dir: Failed to load TOC\n");
			break;
		}

		header = (FileArchiveHeader*)toc;
		if (header->cookie != FILEARCHIVE_MAGIC_COOKIE)
		{
			fprintf(stderr, "dir: Header cookie mismatch\n");
			break;
		}

		if (header->version > FILEARCHIVE_VERSION_CURRENT)
		{
			fprintf(stderr, "dir: Unsupported version %u (maximum supported: %u)\n", (unsigned int)header->version, (unsigned int)FILEARCHIVE_VERSION_CURRENT);
			break;
		}

		if (header->size != footer.size.original)
		{
			fprintf(stderr, "dir: Header size mismatch from footer block (%u != %u)\n", (unsigned int)header->size, (unsigned int)footer.size.original);
			break;
		}

		fprintf(stderr, "File archive version %u, %u containers and %u files in TOC\n", header->version, header->containerCount, header->fileCount);

		for (i = 0; i < header->containerCount; ++i)
		{
			const FileArchiveContainer* container = ((const FileArchiveContainer*)(toc + header->containers)) + i; 
			char* name;
			unsigned int j;

			name = buildContainerName(container, toc, 0);

			fprintf(stderr, "%s: (%u files)\n", name, (unsigned int)container->count);

			for (j = 0; j < container->count; ++j)
			{
				const FileArchiveEntry* file = ((const FileArchiveEntry*)(toc + container->files)) + j;
				const char* name = toc + file->name;

				fprintf(stderr, "   %s (%u bytes, %.2f%% ratio)\n", name, file->size.original, (1.0f-(((float)file->size.compressed) / ((float)file->size.original))) * 100.0f);
			}

			free(name);
		}

		result = 0;
	}
	while (0);

	fclose(inp);
	free(toc);
	return result;
}

int commandList(int argc, const char* argv[])
{
	int i;

	if (argc < 3)
	{
		return -1;
	}

	for (i = 2; i < argc; ++i)
	{
		if (listArchive(argv[i]) < 0)
		{
			return -1;
		}
	}

	return 0;
}
