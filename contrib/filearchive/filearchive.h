#ifndef STREAMER_CONTRIB_FILEARCHIVE_H
#define STREAMER_CONTRIB_FILEARCHIVE_H

#include <stdint.h>

typedef struct FileArchiveContainer FileArchiveContainer;
typedef struct FileArchiveEntry FileArchiveEntry;
typedef struct FileArchiveCompressedBlock FileArchiveCompressedBlock;
typedef struct FileArchiveHeader FileArchiveHeader;

struct FileArchiveContainer
{
	uint32_t parent;	// FileArchiveContainer
	uint32_t children;	// FileArchiveContainer
	uint32_t next;		// FileArchiveContainer

	uint32_t files;		// FileArchiveEntry

	char name[256];
};

struct FileArchiveEntry
{
	uint32_t container;	// FileArchiveContainer
	uint32_t next;		// FileArchiveEntry

	uint32_t compression;
	uint32_t offset;

	struct
	{
		uint32_t original;
		uint32_t compressed;
	} size;

	uint16_t blockSize;
	uint16_t maxCompressedBlock;

	char name[256];
};

struct FileArchiveCompressedBlock
{
	uint16_t original;
	uint16_t compressed; // FILEARCHIVE_COMPRESSION_SIZE_IGNORE == block uncompressed
};

struct FileArchiveHeader
{
	uint32_t cookie;
	uint32_t version;
	uint32_t size;
	uint32_t flags;
};

#define FILEARCHIVE_VERSION_1 (1)
#define FILEARCHIVE_VERSION_CURRENT	FILEARCHIVE_VERSION_CURRENT

#define FILEARCHIVE_MAGIC_COOKIE (('F' << 24) | ('A' << 16) | ('R' << 8) | ('C'))

#define FILEARCHIVE_COMPRESSION_NONE (0)
#define FILEARCHIVE_COMPRESSION_FASTLZ (('F' << 24) | ('L' << 16) | ('Z' << 8) | ('0'))

#define FILEARCHIVE_COMPRESSION_SIZE_IGNORE 0x8000
#define FILEARCHIVE_COMPRESSION_SIZE_MASK 0x7fff

#endif

