#ifndef _COMMON_H_INC_
#define _COMMON_H_INC_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <poll.h>

#include <pthread.h>
#include <errno.h>
#include <time.h>

#ifdef _DEBUG
#include <MemoryDebug.h>
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define MINER_FOR_INCLUDE

#endif
