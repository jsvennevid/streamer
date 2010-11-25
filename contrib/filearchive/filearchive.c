#include "filearchive.h"

#include <stdio.h>
#include <string.h>

void showHelp(int argc, char* argv[])
{
	fprintf(stderr, "\n");

	if ((argc < 2) || (strcmp("help", argv[1])))
	{
		fprintf(stderr, "filearchive <command> ...\n");
		fprintf(stderr, "command = create, help\n\n");
		return;
	}

	if (argc < 3)
	{
		fprintf(stderr, "Help is available for the following commands:\n\n");
		fprintf(stderr, "\tcreate\n");
		fprintf(stderr, "\n");
		return;
	}

	if (!strcmp("create", argv[2]))
	{
		fprintf(stderr, "filearchive create [<options>] <archive> [@<spec>] [<path> ...]\n\n");
		fprintf(stderr, "Create a new file archive.\n\n");
		fprintf(stderr, "Options are:\n");
		fprintf(stderr, "\t-z <compression>   Select compression method: fastlz none (default: none) (global/spec)\n");
		fprintf(stderr, "\t-s                 Optimize layout for optical media (align access to block boundaries) (global)\n\n");
		fprintf(stderr, "<archive> = Archive file to create\n");
		fprintf(stderr, "<spec> = File spec to read files description from (files are gathered relative to spec path)\n");
		fprintf(stderr, "<path> ... = Paths to load files from (files are gathered relative to path root)\n");
		fprintf(stderr, "\nSpec file format:\n");
		fprintf(stderr, "\"<file path>\" <options>\n");
		fprintf(stderr, "\n");
		return;
	}

	fprintf(stderr, "Unknown help topic.\n\n");
}

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		showHelp(argc, argv);
		return 0;
	}

	if (!strcmp("help", argv[1]))
	{
		showHelp(argc, argv);
		return 0;
	}
}
