#include <streamer/streamer.h>
#include <stdio.h>

#include <sha1/sha1.h>

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
		fprintf(stderr, "Outputs SHA-1 sum of specified file identical to the one from sha1sum\n\n");
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
		int totalRead = 0, i;
		SHA1Context state;

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

		SHA1Reset(&state);

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

			SHA1Input(&state, (const unsigned char*)buf, ret);
		}
		while (ret > 0);

		if (totalRead == -1)
		{
			break;
		}

		SHA1Result(&state);

		for (i = 0; i < 20; ++i)
		{
			fprintf(stdout, "%02x", (state.Message_Digest[i / 4] >> ((3-(i & 3)) * 8)) & 0xff);
		}
		fprintf(stdout, " %s\n", filename);

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

