#include "filearchive.h"

#include <stdio.h>
#include <string.h>

void showHelp(char* command)
{
	fprintf(stderr, "\n");

	if (command == NULL)
	{
		fprintf(stderr, "filearchive <command> ...\n");
		fprintf(stderr, "command = create, help\n\n");
		return;
	}
	else if (!strcmp("help", command))
	{
		fprintf(stderr, "Help is available for the following commands:\n\n");
		fprintf(stderr, "\tcreate\n");
		fprintf(stderr, "\n");
		return;
	}
	else if (!strcmp("create", command))
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

int commandCreate(int argc, char* argv[]);

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		showHelp(NULL);
		return 0;
	}
	else if (!strcmp("help", argv[1]))
	{
		showHelp(argc >= 3 ? argv[2] : argv[1]);
		return 0;
	}
	else if (!strcmp("create", argv[1]))
	{
		if (commandCreate(argc, argv) < 0)
		{
			showHelp("create");
			return 1;
		}
	}
	else
	{
		fprintf(stderr, "Unknown filearchive command.\n");
		showHelp(NULL);
		return 1;
	}

	return 0;
}
