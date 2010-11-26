#include "filearchive.h"

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

int addSpecFile(struct FileList* files, const char* specFile, int compression)
{
	fprintf(stderr, "create: Support for spec files not complete\n");
	return -1;
}

int addFile(struct FileList* files, const char* path, const char* internalPath, int compression)
{
	struct FileEntry* entry;
#if defined(_WIN32)
	DWORD attrs;
#else
	struct stat fs;
#endif

	fprintf(stderr, "addFile - path: \"%s\", internal path: \"%s\"\n", path, internalPath);

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
		fprintf(stderr, "create: Adding directories not implemented\n");
		return -1;
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

	entry->path = strdup(path);
	entry->internalPath = strdup(internalPath);

	return 0;
}

void freeFiles(struct FileList* files)
{
	unsigned int i;
	for (i = 0; i < files->count; ++i)
	{
		free(files->files[i].path);
		free(files->files[i].internalPath);
	}

	free(files->files);
}

int commandCreate(int argc, char* argv[])
{
	int i;
	State state = State_Options;
	int compression = FILEARCHIVE_COMPRESSION_NONE;
	struct FileList files = { 0 };

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

			}
			break;

			case State_Archive:
			{
				// archive = argv[i];
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

	freeFiles(&files);
	return 0;
}
