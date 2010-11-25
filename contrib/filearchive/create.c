#include "filearchive.h"

#include <stdio.h>
#include <string.h>

typedef enum
{
	State_Options,
	State_Archive,
	State_Files
} State;

int commandCreate(int argc, char* argv[])
{
	int i;
	State state = State_Options;
	int compression = FILEARCHIVE_COMPRESSION_NONE;

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
			}
			break;
		}
	}

	if (state != State_Files)
	{
		fprintf(stderr, "create: Too few arguments\n");
		return -1;
	}

	return 0;
}
