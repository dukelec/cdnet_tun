/*
 * Copyright 2013 Google Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
/*
 * Author: ncardwell@google.com (Neal Cardwell)
 *
 * Declarations for types used widely throughout this tool.
 */

#ifndef __TYPES_H__
#define __TYPES_H__

/* The files that include this file need to include it before
 * including stdio.h in order to ensure that the declaration of
 * asprintf is visible. So our .h files attempt to follow a
 * convention of including types.h first, before everything else.
 */
#define _GNU_SOURCE		/* for asprintf */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdbool.h>


/* We use some unconventional formatting here to avoid checkpatch.pl
 * warnings about having to use the __packed macro, which is typically
 * only available in the kernel.
 */
#ifndef __packed
#define __packed __attribute__ ((packed))
#endif

/* We use kernel-style names for standard integer types. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef signed char s8;
typedef signed short s16;
typedef signed int s32;
typedef signed long long s64;

typedef u8 __u8;
typedef u16 __u16;
typedef u32 __u32;
typedef u64 __u64;

/* We also use kernel-style names for endian-specific unsigned types. */
typedef u16 __le16;
typedef u16 __be16;
typedef u32 __le32;
typedef u32 __be32;
typedef u64 __le64;
typedef u64 __be64;

typedef u16 __sum16;
typedef u32 __wsum;


#define ARRAY_SIZE(array_name)  (sizeof(array_name) / sizeof(array_name[0]))

/* Most functions in this codebase return one of these two values to let the
 * caller know whether there was a problem.
 */
enum status_t {
	STATUS_OK  = 0,
	STATUS_ERR = -1,
	STATUS_WARN = -2,	/* a non-fatal error or warning */
};


/* Length of output buffer for inet_ntop, plus prefix length (e.g. "/128"). */
#define ADDR_STR_LEN ((INET_ADDRSTRLEN + INET6_ADDRSTRLEN)+5)


/* Convert microseconds to a floating-point seconds value. */
static inline double usecs_to_secs(s64 usecs)
{
	return ((double)usecs) / 1.0e6;
}

/* Convert a timeval to microseconds. */
static inline s64 timeval_to_usecs(const struct timeval *tv)
{
	return ((s64)tv->tv_sec) * 1000000LL + (s64)tv->tv_usec;
}


static inline bool is_valid_u8(s64 x)
{
	return (x >= 0) && (x <= UCHAR_MAX);
}

static inline bool is_valid_u16(s64 x)
{
	return (x >= 0) && (x <= USHRT_MAX);
}

static inline bool is_valid_u32(s64 x)
{
	return (x >= 0) && (x <= UINT_MAX);
}

#endif /* __TYPES_H__ */
