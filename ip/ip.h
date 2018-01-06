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


/* ICMPv6 hader. See RFC 4443. */
struct icmpv6 {
	__u8		type;
	__u8		code;
	__sum16		checksum;
	union {
		struct {
			__be32	unused;
		} unreachable;
		struct {
			__be32	mtu;
		} packet_too_big;
		struct {
			__be32	unused;
		} time_exceeded;
		struct {
			__be32	pointer;
		} parameter_problem;
	} message;
};


/* UDP header. See RFC 768. */
struct udp {
	__be16	src_port;
	__be16	dst_port;
	__be16	len;		/* UDP length in bytes, includes UDP header */
	__sum16 check;		/* UDP checksum */
};


/* A portable TCP header definition (Linux and *BSD use different names). */
struct tcp {
	__be16	src_port;
	__be16	dst_port;
	__be32	seq;
	__be32	ack_seq;
#  if __BYTE_ORDER == __LITTLE_ENDIAN
	__u16	res1:2,
		res2:2,
		doff:4,
		fin:1,
		syn:1,
		rst:1,
		psh:1,
		ack:1,
		urg:1,
		ece:1,
		cwr:1;
#  elif __BYTE_ORDER == __BIG_ENDIAN
	__u16	doff:4,
		res2:2,
		res1:2,
		cwr:1,
		ece:1,
		urg:1,
		ack:1,
		psh:1,
		rst:1,
		syn:1,
		fin:1;
#  else
#   error "Adjust your defines"
#  endif
	__be16	window;
	__sum16	check;
	__be16	urg_ptr;
};

void dump_ip (void *addr, int len);

#endif
