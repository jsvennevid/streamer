#include "filearchive.h"

#include <fastlz/fastlz.h>

#if defined(_WIN32)
#pragma warning(disable: 4996)
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <dirent.h>
#include <sys/stat.h>
#endif

typedef enum
{
	State_Options,
	State_Archive,
	State_Files
} State;

struct FileEntry
{
	int compression;
	int offset;

	struct
	{
		int original;
		int compressed;
	} size;

	char* path;
	char* internalPath;
};

struct FileList
{
	unsigned int count;
	unsigned int capacity;
	struct FileEntry* files;
};

static int addSpecFile(struct FileList* files, const char* specFile, int compression)
{
	fprintf(stderr, "create: Support for spec files not complete\n");
	return -1;
}

static int addFile(struct FileList* files, const char* path, const char* internalPath, int compression)
{
	struct FileEntry* entry;
#if defined(_WIN32)
	DWORD attrs;
#else
	struct stat fs;
#endif

	if ('@' == *path)
	{
		return addSpecFile(files, path+1, compression);
	}

#if defined(_WIN32)
	if ((attrs = GetFileAttributes(path)) == INVALID_FILE_ATTRIBUTES)
	{
		fprintf(stderr, "create: Could not get attributes for file \"%s\"\n", path);
		return -1;
	}

	if (attrs & FILE_ATTRIBUTE_DIRECTORY)
	{
		WIN32_FIND_DATA data;
		char searchPath[MAX_PATH];
		HANDLE dh;
		
		sprintf_s(searchPath, sizeof(searchPath), "%s\\*", path);
		dh = FindFirstFile(searchPath, &data);
		if (dh != INVALID_HANDLE_VALUE)
		{
			do
			{
				char newPath[MAX_PATH];
				char newInternalPath[MAX_PATH];

				if (!strcmp(".", data.cFileName) || !strcmp("..", data.cFileName))
				{
					continue;
				}

				sprintf_s(newPath, sizeof(newPath), "%s\\%s", path, data.cFileName);
				sprintf_s(newInternalPath, sizeof(newPath), "%s\\%s", internalPath, data.cFileName);

				if (addFile(files, newPath, newInternalPath, compression) < 0)
				{
					FindClose(dh);
					return -1;
				}
			}
			while (FindNextFile(dh, &data));

			FindClose(dh);
		}

		return 0;
	}
#else
	if (stat(path, &fs) < 0)
	{
		fprintf(stderr, "create: Could not stat file \"%s\"\n", path);
		return -1;
	}

	if (fs.st_mode & S_IFDIR)
	{
		DIR* dir = opendir(path);
		struct dirent* dirEntry;

		if (dir == NULL)
		{
			fprintf(stderr, "create: Could not open directory \"%s\"\n", path);
			return -1;
		}

		while ((dirEntry = readdir(dir)) != NULL)
		{
			char newPath[PATH_MAX];
			char newInternalPath[PATH_MAX];

			if (!strcmp(".", dirEntry->d_name) || !strcmp("..", dirEntry->d_name))
			{
				continue;
			}

			snprintf(newPath, sizeof(newPath), "%s/%s", path, dirEntry->d_name);
			snprintf(newInternalPath, sizeof(newPath), "%s/%s", internalPath, dirEntry->d_name);

			if (addFile(files, newPath, newInternalPath, compression) < 0)
			{
				closedir(dir);
				return -1;
			}
		}

		closedir(dir);
		return 0;
	}
#endif

	if (files->count == files->capacity)
	{
		int newCapacity = files->capacity < 128 ? 128 : files->capacity * 2;
		files->files = realloc(files->files, newCapacity * sizeof(struct FileEntry));
		memset(files->files + sizeof(struct FileEntry) * files->capacity, 0, sizeof(struct FileEntry) * (newCapacity - files->capacity));
		files->capacity = newCapacity;
	}

	entry = &(files->files[files->count++]);

	entry->compression = compression;
	entry->path = strdup(path);
	entry->internalPath = strdup(internalPath);

	return 0;
}

static void freeFiles(struct FileList* files)
{
	unsigned int i;
	for (i = 0; i < files->count; ++i)
	{
		free(files->files[i].path);
		free(files->files[i].internalPath);
	}

	free(files->files);
}

static int compress_block(const void* input, int length, void* output, int maxLength, int compression)
{
	switch (compression)
	{
		case FILEARCHIVE_COMPRESSION_NONE: return -1;
		case FILEARCHIVE_COMPRESSION_FASTLZ:
		{
			int result;

			if (length < 16)
			{
				return -1;
			}

			result = fastlz_compress_level(2, input, length, output);
			if (result >= length)
			{
				return -1;
			}
			return result;
		}
		break;
	}

	return -1;
}

static int writeFiles(FILE* outp, struct FileList* files, int blockAlignment, uint32_t offset)
{
	unsigned int i;
	FILE* inp = NULL;

	int blockSize = 16384;
	char* input = malloc(blockSize);
	char* output = malloc(blockSize * 2);

	for (i = 0; i < files->count; ++i)
	{
		struct FileEntry* entry = &(files->files[i]);
		size_t totalRead = 0, totalWritten = 0;
		size_t blockRead;

		inp = fopen(entry->path, "rb");
		if (inp == NULL)
		{
			fprintf(stderr, "create: Failed opening \"%s\" for reading\n", entry->path);
			break;
		}

		if ((blockAlignment != 0) && ((offset % blockAlignment) != 0))
		{
			uint32_t bytes = offset % blockAlignment;
			uint32_t current = 0;

			memset(input, 0, 1024);

			while (current < bytes)
			{
				uint32_t bytesMax = bytes - current > 1024 ? 1024 : bytes-current;
				if (fwrite(input, 1, bytesMax, outp) != bytesMax)
				{
					fprintf(stderr, "create: Failed writing %u bytes to archive.\n", (unsigned int)bytesMax);
					break;
				}

				current += bytesMax;
			}

			if (current != bytes)
			{
				break;
			}
		}

		while (inp && (blockRead = fread(input, 1, blockSize, inp)) > 0)
		{
			FileArchiveCompressedBlock block;
			int result;

			totalRead += blockRead;

			if (entry->compression == FILEARCHIVE_COMPRESSION_NONE)
			{
				if (fwrite(input, 1, blockRead, outp) != blockRead)
				{
					fprintf(stderr, "create: Failed writing %u bytes to archive\n", (unsigned int)blockRead);
					fclose(inp);
					inp = NULL;
				}
				totalWritten += blockRead;
				continue;
			}
			
			result = compress_block(input, blockRead, output, blockSize * 2, entry->compression);

			if (result < 0)
			{
				block.compressed = FILEARCHIVE_COMPRESSION_SIZE_IGNORE;
				if (fwrite(&block, 1, sizeof(block), outp) != sizeof(block))
				{
					fprintf(stderr, "create: Failed writing %u bytes to archive\n", (unsigned int)sizeof(block));
					fclose(inp);
					inp = NULL;
					break;
				}

				if (fwrite(input, 1, blockRead, outp) != blockRead)
				{
					fprintf(stderr, "create: Failed writing %u bytes to archive\n", (unsigned int)blockRead);
					fclose(inp);
					inp = NULL;
					break;
				}

				totalWritten += sizeof(block) + blockRead;
			}
			else
			{
				block.compressed = (uint16_t)result;
				if (fwrite(&block, 1, sizeof(block), outp) != sizeof(block))
				{
					fprintf(stderr, "create: Failed writing %u bytes to archive\n", (unsigned int)sizeof(block));
					fclose(inp);
					inp = NULL;
					break;
				}

				if (fwrite(output, 1, block.compressed, outp) != block.compressed)
				{
					fprintf(stderr, "create: Failed writing %u bytes to archive\n", (unsigned int)result);
					fclose(inp);
					inp = NULL;
					break;
				}

				totalWritten += sizeof(block) + result;
			}
		}

		entry->offset = offset;
		entry->size.original = totalRead;
		entry->size.compressed = totalWritten;
		offset += totalWritten;

		if (inp)
		{
			fprintf(stderr, "Added file \"%s\" (%u bytes) (as \"%s\", %u bytes), %s\n", entry->path, (unsigned int)totalRead, entry->internalPath, (unsigned int)totalWritten, entry->compression != FILEARCHIVE_COMPRESSION_NONE ? "(compressed)" : "(raw)");

			fclose(inp);
			inp = NULL;
		}
	}

	free(input);
	free(output);

	if (inp != NULL)
	{
		fclose(inp);
	}

	return (i == files->count) ? 0 : -1;
}

int commandCreate(int argc, char* argv[])
{
	int i;
	State state = State_Options;
	int compression = FILEARCHIVE_COMPRESSION_NONE;
	int blockAlignment = 0;
	struct FileList files = { 0 };
	const char* archive = NULL;
	FILE* archp = NULL;

	for (i = 2; i < argc; ++i)
	{
		switch (state)
		{
			case State_Options:
			{
				if ('-' != argv[i][0])
				{
					state = State_Archive;
					--i;
					break;
				}

				if (!strcmp("-z", argv[i]))
				{
					if (argc == (i+1))
					{
						fprintf(stderr, "create: Missing argument for -z compression argument\n");
						return -1;
					}
					++i;

					if (!strcmp("fastlz", argv[i]))
					{
						compression = FILEARCHIVE_COMPRESSION_FASTLZ;
					}
					else if (!strcmp("none", argv[i]))
					{
						compression = FILEARCHIVE_COMPRESSION_NONE;
					}
					else
					{
						fprintf(stderr, "create: Unknown compression method \"%s\"\n", argv[i]);
						return -1;
					}
				}
				else if (!strcmp("-s", argv[i]))
				{
					blockAlignment = 2048;
				}
				else
				{
					fprintf(stderr, "create: Unknown option \"%s\"\n", argv[i]);
					return -1;
				}

			}
			break;

			case State_Archive:
			{
				archive = argv[i];
				state = State_Files;
			}
			break;

			case State_Files:
			{
				const char* internalPath = argv[i];
				const char* sep = strrchr(internalPath, '/');
#if defined(_WIN32)
				const char* sep2 = strrchr(internalPath, '\\');
#endif
				if (sep)
				{
					internalPath = sep+1;
				}
#if defined(_WIN32)
				if ((!sep && sep2) || (sep2 > sep))
				{
					internalPath = sep2+1;
				}
#endif
				if (addFile(&files, argv[i], internalPath, compression) < 0)
				{
					fprintf(stderr, "create: Failed adding file \"%s\"\n", argv[i]);
					freeFiles(&files);
					return -1;
				}
			}
			break;
		}
	}

	if (state != State_Files)
	{
		fprintf(stderr, "create: Too few arguments\n");
		freeFiles(&files);
		return -1;
	}

	if (files.count == 0)
	{
		fprintf(stderr, "create: No files available for use\n");
		freeFiles(&files);
		return -1;
	}

	archp = fopen(archive, "wb");
	if (archp == NULL)
	{
		fprintf(stderr, "create: Could not open archive \"%s\" for writing\n", archive);
		freeFiles(&files);
		return -1;
	}

	if (writeFiles(archp, &files, blockAlignment, 0) < 0)
	{
		fprintf(stderr, "create: Failed writing files to archive\n");
		fclose(archp);
		freeFiles(&files);
		return -1;
	}

	freeFiles(&files);
	return 0;
}
