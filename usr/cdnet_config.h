/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <duke@dukelec.com>
 */

#ifndef __CDNET_CONFIG_H__
#define __CDNET_CONFIG_H__

#define DEBUG
#define VERBOSE

#define CDUART_IDLE_TIME    (500000 / SYSTICK_US_DIV)

#define CDNET_USE_L2
#define CDNET_DAT_SIZE      2000 // MTU size + compress overhead
#define SEQ_TIMEOUT         (500000 / SYSTICK_US_DIV)

#endif
