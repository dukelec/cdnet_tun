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
 * Helpers to calculate IP, TCP, and UDP checksums.
 */

#ifndef __IP_CHECKSUM_H__
#define __IP_CHECKSUM_H__

#include "types.h"

#include <netinet/in.h>
#include <sys/types.h>

/* IPv6 ... */

/* Calculates TCP, UDP, or ICMP checksum for IPv6 (in network byte order). */
extern __be16 tcp_udp_v6_checksum(const struct in6_addr *src_ip,
				  const struct in6_addr *dst_ip,
				  u8 protocol, const void *payload, u32 len);

#endif
