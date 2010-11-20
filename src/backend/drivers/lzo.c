/*

Copyright (c) 2006-2010 Jesper Svennevid

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/
#include "lzo.h"
#include "driver.h"

#if defined(_IOP)
#include "../iop/irx_imports.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#if defined(_IOP)
#define STREAMER_UNLIKELY(x) __builtin_expect(x, 0) 
#else
#define STREAMER_UNLIKELY(x) x
#endif

#define LZO_M2_MAX_OFFSET 0x0800

#define LZO_BOOL int
#define LZO_FALSE (0)
#define LZO_TRUE (1)

int streamer_lzo_decompress(void* output, unsigned int outSize, const void* input, unsigned int inSize)
{
	int t = 0;
	const unsigned char* pos = 0;
	const unsigned char* ipEnd = (const unsigned char*)input + inSize;
	unsigned char* opEnd = (unsigned char*)output + outSize;
	const unsigned char* ip = (const unsigned char*)input;
	unsigned char* op = (unsigned char*)output;
	LZO_BOOL match = LZO_FALSE;
	LZO_BOOL matchNext = LZO_FALSE;
	LZO_BOOL matchDone = LZO_FALSE;
	LZO_BOOL copyMatch = LZO_FALSE;
	LZO_BOOL firstLiteralRun = LZO_FALSE;
	LZO_BOOL foundEnd = LZO_FALSE;

	if (*ip > 17)
	{
		t = (unsigned int)(*ip++ - 17);
		if (t < 4)
		{
			matchNext = LZO_TRUE;
		}
		else
		{
			if (STREAMER_UNLIKELY((opEnd - op) < t))
			{
				STREAMER_PRINTF(("lzo: Output overrun\n"));
				return -1;
			}

			if (STREAMER_UNLIKELY((ipEnd - ip) < t + 1))
			{
				STREAMER_PRINTF(("lzo: Input overrun\n"));
				return -1;
			}

			do
			{
				*op++ = *ip++;
			}
			while (--t > 0);

			firstLiteralRun = LZO_TRUE;
		}
	}

	while (!foundEnd && (ip < ipEnd))
	{
		if (!matchNext && !firstLiteralRun)
		{
			t = *ip++;
			if (t >= 16)
			{
				match = LZO_TRUE;
			}
			else
			{
				int x;

				if (t == 0)
				{
					if (STREAMER_UNLIKELY((ipEnd - ip) < 1))
					{
						STREAMER_PRINTF(("lzo: Input overrun\n"));
						return -1;
					}

					while (*ip == 0)
					{
						t += 255;
						++ip;
						if (STREAMER_UNLIKELY((ipEnd - ip) < 1))
						{
							STREAMER_PRINTF(("lzo: Input overrun\n"));
							return -1;
						}
					}
					t += (unsigned int)(15 + *ip++);
				}

				if (STREAMER_UNLIKELY((opEnd - op) < t + 3))
				{
					STREAMER_PRINTF(("lzo: Output overrun\n"));
					return -1;
				}

				if (STREAMER_UNLIKELY((ipEnd - ip) < t + 4))
				{
					STREAMER_PRINTF(("lzo: Input overrun\n"));
					return -1;
				}

				for (x = 0; x < 4; ++x, ++op, ++ip)
				{
					*op = *ip;
				}

				if (--t > 0)
				{
					if (t >= 4)
					{
						do
						{
							int x;
							for (x = 0; x < 4; ++x, ++op, ++ip)
							{
								*op = *ip;
							}
							t -= 4;
						}
						while (t >= 4);

						if (t > 0)
						{
							do
							{
								*op++ = *ip++;
							}
							while (--t > 0);
						}
					}
					else
					{
						do
						{
							*op++ = *ip++;
						}
						while (--t > 0);
					}
				}
			}
		}
		if (!match && !matchNext)
		{
			firstLiteralRun = LZO_FALSE;

			t = *ip++;
			if (t >= 16)
			{
				match = LZO_TRUE;
			}
			else
			{
				pos = op - (1 + LZO_M2_MAX_OFFSET);
				pos -= t >> 2;
				pos -= *ip++ << 2;

				if (STREAMER_UNLIKELY(pos < (unsigned char*)output || pos >= op))
				{
					STREAMER_PRINTF(("lzo: Look-behind overrun\n"));
					return -1;
				}

				if (STREAMER_UNLIKELY((opEnd - op) < 3))
				{
					STREAMER_PRINTF(("lzo: Output overrun\n"));
					return -1;
				}

				*op++ = *pos++;
				*op++ = *pos++;
				*op++ = *pos++;
				matchDone = LZO_TRUE;
			}
		}

		match = LZO_FALSE;

		do
		{
			if (t >= 64)
			{
				pos = op - 1;
				pos -= (t >> 2) & 7;
				pos -= *ip++ << 3;
				t = (t >> 5) - 1;

				if (STREAMER_UNLIKELY(pos < (unsigned char*)output || pos >= op))
				{
					STREAMER_PRINTF(("lzo: Look-behind overrun\n"));
					return -1;
				}

				if (STREAMER_UNLIKELY((opEnd - op) < t + 2))
				{
					STREAMER_PRINTF(("lzo: Output overrun\n"));
					return -1;
				}

				copyMatch = LZO_TRUE;
			}
			else if (t >= 32)
			{
				t &= 31;
				if (t == 0)
				{
					if (STREAMER_UNLIKELY((ipEnd - ip) < 1))
					{
						STREAMER_PRINTF(("lzo: Input overrun\n"));
						return -1;
					}

					while (*ip == 0)
					{
						t += 255;
						++ip;
						if (STREAMER_UNLIKELY((ipEnd - ip) < 1))
						{
							STREAMER_PRINTF(("lzo: Input overrun\n"));
							return -1;
						}
					}
					t += (unsigned int)(31 + *ip++);
				}
				pos = op - 1;
				pos -= ((ip[1] << 8) | ip[0]) >> 2;
				ip += 2;
			}
			else if (t >= 16)
			{
				pos = op;
				pos -= (t & 8) << 11;

				t &= 7;
				if (t == 0)
				{
					if (STREAMER_UNLIKELY((ipEnd - ip) < 1))
					{
						STREAMER_PRINTF(("lzo: Input overrun\n"));
						return -1;
					}

					while (*ip == 0)
					{
						t += 255;
						++ip;
						if (STREAMER_UNLIKELY((ipEnd - ip) < 1))
						{
							STREAMER_PRINTF(("lzo: Input overrun\n"));
							return -1;
						}
					}
					t += (unsigned int)(7 + *ip++);
				}
				pos -= ((ip[1] << 8) | ip[0]) >> 2;
				ip += 2;
				if (pos == op)
				{
					foundEnd = LZO_TRUE;
				}
				else
				{
					pos -= 0x4000;
				}
			}
			else
			{
				pos = op - 1;
				pos -= t >> 2;
				pos -= *ip++ << 2;

				if (STREAMER_UNLIKELY(pos < (unsigned char*)output || pos >= op))
				{
					STREAMER_PRINTF(("lzo: Look-behind overrun\n"));
					return -1;
				}

				if (STREAMER_UNLIKELY((opEnd - op) < 2))
				{
					STREAMER_PRINTF(("lzo: Output overrun\n"));
					return -1;
				}

				*op++ = *pos++;
				*op++ = *pos++;
				matchDone = LZO_TRUE;
			}
			if (!foundEnd && !matchDone && !copyMatch)
			{
				if (STREAMER_UNLIKELY(pos < (unsigned char*)output || pos >= op))
				{
					STREAMER_PRINTF(("lzo: Look-behind overrun\n"));
					return -1;
				}

				if (STREAMER_UNLIKELY((opEnd - op) < t + 2))
				{
					STREAMER_PRINTF(("lzo: Output overrun\n"));
					return -1;
				}
			}
			if (!foundEnd && (t >= (2 * 4 - 2)) && ((op - pos) >= 4) && !matchDone && !copyMatch)
			{
				int x;
				for (x = 0; x < 4; ++x, ++op, ++pos)
				{
					*op = *pos;
				}

				t -= 2;

				do
				{
					int x;
					for (x = 0; x < 4; ++x, ++op, ++pos)
					{
						*op = *pos;
					}
					t -= 4;

				} while (t >= 4);

				if (t > 0)
				{
					do
					{
						*op++ = *pos++;
					} while (--t > 0);
				}
			}
			else if(!foundEnd && !matchDone)
			{
				copyMatch = LZO_FALSE;

				*op++ = *pos++;
				*op++ = *pos++;

				do
				{
					*op++ = *pos++;
				}
				while (--t > 0);
			}

			if (!foundEnd && !matchNext)
			{
				matchDone = LZO_FALSE;

				t = (unsigned int)(ip[-2] & 3);
				if (t == 0)
				{
					break;
				}
			}

			if (!foundEnd)
			{
				matchNext = LZO_FALSE;
				if (STREAMER_UNLIKELY((opEnd - op) < t))
				{
					STREAMER_PRINTF(("lzo: Output overrun\n"));
					return -1;
				}

				if (STREAMER_UNLIKELY((ipEnd - ip) < t + 1))
				{
					STREAMER_PRINTF(("lzo: Input overrun\n"));
					return -1;
				}

				*op++ = *ip++;

				if (t > 1)
				{
					*op++ = *ip++;
					if (t > 2)
					{
						*op++ = *ip++;
					}
				}
				t = *ip++;
			}
		}
		while (!foundEnd && (ip < ipEnd));
	}

	if (STREAMER_UNLIKELY(!foundEnd))
	{
		STREAMER_PRINTF(("lzo: EOF marker not found\n"));
		return -1;
	}
	else
	{
		if (STREAMER_UNLIKELY(ip > ipEnd))
		{
			STREAMER_PRINTF(("lzo: Input overrun\n"));
			return -1;
		}
		else if (STREAMER_UNLIKELY(ip < ipEnd))
		{
			STREAMER_PRINTF(("lzo: Input not consumed\n"));
			return -1;
		}
	}

	return 0;
}
