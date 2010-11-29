#include <streamer.h>

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

#if defined(STREAMER_WIN32)
#include <windows.h>
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

int streamerTest()
{
	int ret, fd;
	int result = 1;
	char *buf = NULL;
	int size = 1024 * 1024;
	time_t a,b;

	ret = streamerInitialize(StreamerTransport_FileIo, StreamerContainer_Direct, "", ""); 
	if (ret < 0)
	{
		fprintf(stderr, "Failed to initialize streamer\n");
		return 1;
	}

	buf = malloc(size);
	if (buf == NULL)
	{
		fprintf(stderr, "Failed to allocate buffer memory\n");
		return 1;
	}

	do
	{
		int totalRead = 0;

		fd = streamerOpen("test.file", StreamerOpenMode_Read);
		if (fd < 0)
		{
			fprintf(stderr, "Streamer open() request failed to initialize (%d)\n", fd);
			break;
		}

		ret = waitForStreamerRequest(fd);
		if (ret < 0)
		{
			fprintf(stderr, "Streamer open() request failed (%d)\n", ret);
			break;
		}

		fprintf(stderr, "Successfully opened test.file (fd %d), reading...\n", fd);

		a = time(0);
		do
		{
			ret = streamerRead(fd, buf, size);
			if (ret < 0)
			{
				fprintf(stderr, "Streamer read() request failed to initialize after %d bytes (%d)\n", totalRead, ret);
				totalRead = -1;
				break;
			}

			ret = waitForStreamerRequest(fd);
			if (ret < 0)
			{
				fprintf(stderr, "Streamer read() request failed after %d bytes (%d)\n", totalRead, ret);
				totalRead = -1;
				break;
			}

			totalRead += ret;

			//fprintf(stderr, "Read %d bytes, %d total\n", ret, totalRead);
		}
		while (ret > 0);
		b = time(0);

		fprintf(stderr, "Read %d bytes in %d seconds\n", totalRead, (int)(b-a));

		ret = streamerClose(fd);
		if (ret < 0)
		{
			fprintf(stderr,"Streamer close() request failed to initialize (%d)\n", ret);
			break;
		}

		ret = waitForStreamerRequest(fd);
		if (ret < 0)
		{
			fprintf(stderr, "Streamer close() request failed (%d)\n", ret);
			break;
		}

		result = 0;
	}
	while (result != 0);

	free(buf);

	ret = streamerShutdown();
	if (ret < 0)
	{
		fprintf(stderr, "Failed to shut down streamer\n");
		return -1;
	}

	return result;
}

int main()
{
	FILE* fp;
	char buf[1024];
	int i, ret;

	fprintf(stderr, "Creating 100MB test file...\n");

	fp = fopen("test.file", "wb");
	if (fp == NULL)
	{
		fprintf(stderr, "Failed to open file\n");
		return 1;
	}

	memset(buf, 0, sizeof(buf));
	for (i = 0; i < 1024 * 100; ++i)
	{
		fwrite(buf, 1, sizeof(buf), fp);
	}

	fclose(fp);

	ret = streamerTest();
	if (ret < 0)
	{
		fprintf(stderr, "Streamer test failed\n");
	}


#if defined(STREAMER_WIN32)
	DeleteFile("test.file");
#elif defined(STREAMER_UNIX)
	unlink("test.file");
#endif

	return 0;
}

