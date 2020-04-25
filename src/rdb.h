/*
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __RDB_H
#define __RDB_H
#include<stdio.h>
#include"rio.h"
/* TBD: include only necessary headers. */
//只包含必要的头部
#include"server.h"

/* The current RDB version. When the format changes in a way that is no longer
 * backward compatible this number gets incremented. */
//TODO  还需要理解??
 //当版本不在向后兼容时候，版本号加一
#define RDB_VERSION 9 

/* Defines related to the dump file format. To store 32 bits lengths for short
 * keys requires a lot of space, so we check the most significant 2 bits of
 * the first byte to interpreter the length:
 *
 * 通过读取第一字节的最高 2 位来判断长度
 *
 * 00|000000 => if the two MSB are 00 the len is the 6 bits of this byte
 *              长度编码在这一字节的其余 6 位中
 *
 * 01|000000 00000000 =>  01, the len is 14 byes, 6 bits + 8 bits of next byte
 *                        长度为 14 位，当前字节 6 位，加上下个字节 8 位
 *
 * 10|000000 [32 bit integer] => if it's 01, a full 32 bit len will follow
 *                               长度由后跟的 32 位保存
 *
 * 11|000000 this means: specially encoded object will follow. The six bits
 *           number specify the kind of object that follows.
 *           See the REDIS_RDB_ENC_* defines.
 *           后跟一个特殊编码的对象。字节中的 6 位指定对象的类型。
 *           查看 REDIS_RDB_ENC_* 定义获得更多消息
 *
 * Lenghts up to 63 are stored using a single byte, most DB keys, and may
 * values, will fit inside. 
 *
 * 一个字节（的其中 6 个字节）可以保存的最大长度是 63 （包括在内），
 * 对于大多数键和值来说，都已经足够了。
 */
 #define REDIS_RDB_6BITLEN 0
#define REDIS_RDB_14BITLEN 1
#define REDIS_RDB_32BITLEN 2
#define REDIS_RDB_ENCVAL 3
// 表示读取/写入错误
#define REDIS_RDB_LENERR UINT_MAX

/* When a length of a string object stored on disk has the first two bits
 * set, the remaining two bits specify a special encoding for the object
 * accordingly to the following defines:
 *
 * 当对象是一个字符串对象时，
 * 最高两个位之后的两个位（第 3 个位和第 4 个位）指定了对象的特殊编码
 */
#define REDIS_RDB_ENC_INT8 0        /* 8 bit signed integer */
#define REDIS_RDB_ENC_INT16 1       /* 16 bit signed integer */
#define REDIS_RDB_ENC_INT32 2       /* 32 bit signed integer */
#define REDIS_RDB_ENC_LZF 3         /* string compressed with FASTLZ */

/* Map object types to RDB object types. Macros starting with OBJ_ are for
 * memory storage and may change. Instead RDB types must be fixed because
 * we store them on disk. */
//对象类型到rdb数据库中的对象类型的映射，OBJ开头的宏
//是用于内存存储的可能会改变，但是存储在磁盘上的rdb类型是固定的
#define RDB_TYPE_STRING 0
#define RDB_TYPE_LIST   1
#define RDB_TYPE_SET    2
#define RDB_TYPE_ZSET   3
#define RDB_TYPE_HASH   4
#define RDB_TYPE_ZSET_2 5 /* ZSET version 2 with doubles stored in binary. */
#define RDB_TYPE_MODULE 6
#define RDB_TYPE_MODULE_2 7 /* Module value with annotations for parsing without
                               the generating module being loaded. */
/* NOTE: WHEN ADDING NEW RDB TYPE, UPDATE rdbIsObjectType() BELOW */


/* Object types for encoded objects. */
#define RDB_TYPE_HASH_ZIPMAP    9
#define RDB_TYPE_LIST_ZIPLIST  10
#define RDB_TYPE_SET_INTSET    11
#define RDB_TYPE_ZSET_ZIPLIST  12
#define RDB_TYPE_HASH_ZIPLIST  13
#define RDB_TYPE_LIST_QUICKLIST 14
#define RDB_TYPE_STREAM_LISTPACKS 15
/* NOTE: WHEN ADDING NEW RDB TYPE, UPDATE rdbIsObjectType() BELOW */

/* Test if a type is an object type. */
//测试是否是对象类型
#define rdbIsObjectType(t) ((t >= 0 && t <= 7) || (t >= 9 && t <= 15))


/* Special RDB opcodes (saved/loaded with rdbSaveType/rdbLoadType). */
//特殊操作码
#define RDB_OPCODE_MODULE_AUX 247   /* Module auxiliary data. */
#define RDB_OPCODE_IDLE       248   /* LRU idle time. */
#define RDB_OPCODE_FREQ       249   /* LFU frequency. */
#define RDB_OPCODE_AUX        250   /* RDB aux field. */
#define RDB_OPCODE_RESIZEDB   251   /* Hash table resize hint. */
#define RDB_OPCODE_EXPIRETIME_MS 252    /* Expire time in milliseconds. */
#define RDB_OPCODE_EXPIRETIME 253       /* Old expire time in seconds. */
#define RDB_OPCODE_SELECTDB   254   /* DB number of the following keys. */
#define RDB_OPCODE_EOF        255   /* End of the RDB file. */

/* Module serialized values sub opcodes */
#define RDB_MODULE_OPCODE_EOF   0   /* End of module value. */
#define RDB_MODULE_OPCODE_SINT  1   /* Signed integer. */
#define RDB_MODULE_OPCODE_UINT  2   /* Unsigned integer. */
#define RDB_MODULE_OPCODE_FLOAT 3   /* Float. */
#define RDB_MODULE_OPCODE_DOUBLE 4  /* Double. */
#define RDB_MODULE_OPCODE_STRING 5  /* String. */

/* rdbLoad...() functions flags. */
#define RDB_LOAD_NONE   0
#define RDB_LOAD_ENC    (1<<0)
#define RDB_LOAD_PLAIN  (1<<1)
#define RDB_LOAD_SDS    (1<<2)

#define RDB_SAVE_NONE 0
#define RDB_SAVE_AOF_PREAMBLE (1<<0)

int rdbSaveType(rio *rdb, unsigned char type);

#endif
