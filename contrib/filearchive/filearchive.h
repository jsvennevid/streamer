#ifndef STREAMER_CONTRIB_FILEARCHIVE_H
#define STREAMER_CONTRIB_FILEARCHIVE_H

#include <stdint.h>

typedef struct FileArchiveContainer FileArchiveContainer;
typedef struct FileArchiveEntry FileArchiveEntry;
typedef struct FileArchiveCompressedBlock FileArchiveCompressedBlock;
typedef struct FileArchiveHeader FileArchiveHeader;

struct FileArchiveContainer
{
	uint32_t parent;	// Offset to parent container (FileArchiveContainer)
	uint32_t children;	// Offset to child containers (FileArchiveContainer)
	uint32_t next;		// Offset to next sibling container (FileArchiveContainer)

	uint32_t files;		// Offset to first file in container (FileArchiveEntry)
	uint32_t count;		// number of entries

	uint32_t name;		// Offset to container name
};

struct FileArchiveEntry
{
	uint32_t data;			// Offset to file data
	uint32_t name;			// Offset to name

	uint32_t compression;		// Compression method used in file

	struct
	{
		uint32_t original;	// original file size when uncompressed
		uint32_t compressed;	// compressed file size in archive
	} size;

	uint16_t blockSize;		// block size required for decompression
	uint16_t largestBlock;		// size of the largest compressed block
};

struct FileArchiveCompressedBlock
{
	uint16_t original;
	uint16_t compressed; 		// FILEARCHIVE_COMPRESSION_SIZE_IGNORE == block uncompressed
};

struct FileArchiveHeader
{
	uint32_t cookie;		// Magic cookie
	uint32_t version;		// Version of archive
	uint32_t size;			// Size of header TOC
	uint32_t flags;			// Flags

	// Version 1

	uint32_t containers;		// Offset to containers (first container == root folder)
	uint32_t containerCount;	// Number of containers in archive

	uint32_t files;			// Offset to list of files
	uint32_t fileCount;		// Number of files in archive
};

struct FileArchiveFooter
{
	uint32_t cookie;		// Magic cookie
	uint32_t header;		// Offset to header (relative to end of footer)
	uint32_t data;			// Offset to data (relative to end of footer)

	uint32_t compression;		// Compression used on header

	struct
	{
		uint32_t original;	// Header size, uncompressed
		uint32_t compressed;	// Header size, compressed
	} size;
};

#define FILEARCHIVE_VERSION_1 (1)
#define FILEARCHIVE_VERSION_CURRENT	FILEARCHIVE_VERSION_CURRENT

#define FILEARCHIVE_MAGIC_COOKIE (('F' << 24) | ('A' << 16) | ('R' << 8) | ('C'))

#define FILEARCHIVE_COMPRESSION_NONE (0)
#define FILEARCHIVE_COMPRESSION_FASTLZ (('F' << 24) | ('L' << 16) | ('Z' << 8) | ('0'))

#define FILEARCHIVE_COMPRESSION_SIZE_IGNORE 0x8000
#define FILEARCHIVE_COMPRESSION_SIZE_MASK 0x7fff

#endif

