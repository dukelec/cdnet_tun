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
 * Our own IPv6 header declarations, so we have something that's
 * portable and somewhat more readable than a typical system header
 * file.
 */

#ifndef __IP_H__
#define __IP_H__

#include "types.h"

#include <netinet/in.h>

struct ipv4 {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    __u8    ihl:4,
        version:4;
#elif __BYTE_ORDER == __BIG_ENDIAN
    __u8    version:4,
        ihl:4;
#else
# error "Please fix endianness defines"
#endif
    __u8    tos;
    __be16  tot_len;
    __be16  id;
    __be16  frag_off;
    __u8    ttl;
    __u8    protocol;
    __sum16 check;
    struct in_addr  src_ip;
    struct in_addr  dst_ip;
};

struct ipv6 {
#if __BYTE_ORDER == __LITTLE_ENDIAN
	__u8			traffic_class_hi:4,
				version:4;
	__u8			flow_label_hi:4,
				traffic_class_lo:4;
	__u16			flow_label_lo;
#elif __BYTE_ORDER == __BIG_ENDIAN
	__u8			version:4,
				traffic_class_hi:4;
	__u8			traffic_class_lo:4,
				flow_label_hi:4;
	__u16			flow_label_lo;
#else
# error "Please fix endianness defines"
#endif

	__be16			payload_len;
	__u8			next_header;
	__u8			hop_limit;

	struct	in6_addr	src_ip;
	struct	in6_addr	dst_ip;
};

/* UDP header. See RFC 768. */
struct udp {
	__be16	src_port;
	__be16	dst_port;
	__be16	len;		/* UDP length in bytes, includes UDP header */
	__sum16 check;		/* UDP checksum */
};



void dump_ip (void *addr, int len);

#endif
