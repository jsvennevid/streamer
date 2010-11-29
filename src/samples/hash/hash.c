#include <streamer/streamer.h>
#include <stdio.h>

#include "md5.h"

#if defined(STREAMER_WIN32)
#include <windows.h>
#pragma warning(disable: 4127)
#elif defined(STREAMER_UNIX)
#include <unistd.h>
#endif

#include <stdlib.h>
#include <time.h>
#include <string.h>

int waitForStreamerRequest(int fd)
{
	int ret;
	while ((ret = streamerPoll(fd)) == StreamerResult_Pending)
	{
#if defined(STREAMER_WIN32)
		SleepEx(0, TRUE);
#elif defined(STREAMER_UNIX)
		struct timespec req = { 0, 1000 };
		nanosleep(&req, 0);
#endif
	}
	return ret;
}

int main(int argc, char* argv[])
{
	const char* archiveEnd;
	const char* filename;
	int ret, fd;

	if (argc < 2)
	{
		fprintf(stderr, "\nStreamer hash sample - hash file on disk or in a file archive\n\n");
		fprintf(stderr, "Usage: hash [<archive>:]<file>\n\n");
		fprintf(stderr, "Outputs MD5 sum of specified file identical to the one from md5sum\n\n");
		return 0;
	}

	if ((archiveEnd = strrchr(argv[1], ':')) != NULL)
	{
		char* archive = malloc((archiveEnd - argv[1]) + 1);
		memcpy(archive, argv[1], archiveEnd - argv[1]);
		archive[archiveEnd - argv[1]] = '\0';

		ret = streamerInitialize(StreamerTransport_FileIo, StreamerContainer_FileArchive, "", archive);

		free(archive);
		filename = archiveEnd + 1;
	}
	else
	{
		ret = streamerInitialize(StreamerTransport_FileIo, StreamerContainer_Direct, "", "");
		filename = argv[1];
	}

	if (ret < 0)
	{
		fprintf(stderr, "Failed to initialize streamer.\n");
		return 1;
	}

	do
	{
		char buf[1024];
		int totalRead = 0;
		md5_state_t state;
		md5_byte_t digest[16];

		fd = streamerOpen(filename, StreamerOpenMode_Read);
		if (fd < 0)
		{
			fprintf(stderr, "Failed to initiate open request\n");
			break;
		}

		ret = waitForStreamerRequest(fd);
		if (ret < 0)
		{
			fprintf(stderr, "Failed to open file \"%s\"\n", filename);
			break;
		}

		md5_init(&state);

		do
		{
			ret = streamerRead(fd, buf, sizeof(buf));
			if (ret < 0)
			{
				fprintf(stderr, "Read request failed initializing\n");
				totalRead = -1;
				break;
			}

			ret = waitForStreamerRequest(fd);
			if (ret < 0)
			{
				fprintf(stderr, "Read request failed\n");
				totalRead = -1;
				break;
			}

			md5_append(&state, (const md5_byte_t*)buf, ret);
		}
		while (ret > 0);

		if (totalRead == -1)
		{
			break;
		}

		md5_finish(&state, digest);

		fprintf(stdout, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x %s\n",
			digest[0], digest[1], digest[2], digest[3],
			digest[4], digest[5], digest[6], digest[7],
			digest[8], digest[9], digest[10], digest[11],
			digest[12], digest[13], digest[14], digest[15],
			filename);

		ret = streamerClose(fd);
		if (ret < 0)
		{
			fprintf(stderr, "Close request failed initializing\n");
			break;
		}

		ret = waitForStreamerRequest(fd);
		if (ret < 0)
		{
			fprintf(stderr, "Close request failed\n");
			break;
		}
	}
	while (0);

	if (streamerShutdown() < 0)
	{
		fprintf(stderr, "Failed to shut down streamer\n");
		return 1;
	}

	return 0;
}
