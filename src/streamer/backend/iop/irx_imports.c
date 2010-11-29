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
#include "irx_imports.h"

intrman_IMPORTS_start
I_CpuSuspendIntr
I_CpuResumeIntr
intrman_IMPORTS_end

ioman_IMPORTS_start
I_open
I_close
I_read
I_write
I_lseek
ioman_IMPORTS_end

stdio_IMPORTS_start
I_printf
stdio_IMPORTS_end

thbase_IMPORTS_start
I_CreateThread
I_DeleteThread
I_StartThread
I_SleepThread
I_GetThreadId
I_ExitDeleteThread
I_DelayThread
I_RotateThreadReadyQueue
I_WakeupThread
thbase_IMPORTS_end

thevent_IMPORTS_start
I_CreateEventFlag
I_DeleteEventFlag
I_SetEventFlag
I_WaitEventFlag
thevent_IMPORTS_end

thsemap_IMPORTS_start
I_CreateSema
I_DeleteSema
I_SignalSema
I_WaitSema
thsemap_IMPORTS_end

sifman_IMPORTS_start
I_sceSifSetDma
I_sceSifDmaStat
sifman_IMPORTS_end

sifcmd_IMPORTS_start
I_sceSifInitRpc
I_sceSifRegisterRpc
I_sceSifSetRpcQueue
I_sceSifRpcLoop
I_sceSifBindRpc
I_sceSifCallRpc
sifcmd_IMPORTS_end

sysclib_IMPORTS_start
I_tolower
I_strcpy
I_memcpy
I_memset
I_strcat
I_strncpy
I_strlen
sysclib_IMPORTS_end

sysmem_IMPORTS_start
I_AllocSysMemory
I_FreeSysMemory
sysmem_IMPORTS_end

vblank_IMPORTS_start
I_WaitVblankStart
vblank_IMPORTS_end
