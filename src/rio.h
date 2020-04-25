/*
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
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


#ifndef __REDIS_RIO_H
#define __REDIS_RIO_H

#include <stdio.h>
#include <stdint.h>
#include "sds.h"
/*  struct rio中声明了统一的io操作接口，并且包含一个底层io对象的union结构。
使用不同的底层io初始化rio实例后，调用rio的抽象接口即会调用对应底层io的实现。
以面向对象的思想即是，rio为抽象类，它拥有三个子类:buffer、file及fdset，这三
个子类实现了抽象类声明的接口。使用者可以使用它们的父类rio进行编程，实现多态性。
*/

struct _rio {
    /* Backend functions.
     * Since this functions do not tolerate short writes or reads the return
     * value is simplified to: zero on error, non zero on complete success. */
    size_t (*read)(struct _rio *, void *buf, size_t len);
    size_t (*write)(struct _rio *, const void *buf, size_t len);
    off_t (*tell)(struct _rio *);
    int (*flush)(struct _rio *);
    /* The update_cksum method if not NULL is used to compute the checksum of
     * all the data that was read or written so far. The method should be
     * designed so that can be called with the current checksum, and the buf
     * and len fields pointing to the new block of data to add to the checksum
     * computation. */
    void (*update_cksum)(struct _rio *, const void *buf, size_t len);

    /* The current checksum */
    uint64_t cksum;

    /* number of bytes read or written */
    size_t processed_bytes;

    /* maximum single read or write chunk size */
    size_t max_processing_chunk;

    /* Backend-specific vars. */
    union {
        /* In-memory buffer target. */
	/* 以buffer为底层io的rio实例，
	write操作将参数buf中的数据copy到一个sds中(redis的字符串实现)。
	反之，它的read操作将会从一个sds中读取数据到参数buf指向的地址中。
  抽象接口不支持seek操作，因此写操作仅能append，而读操作也只能从当前位置读数据。
  */
        struct {
            sds ptr;
            off_t pos;//记录读写操作在buff中的当前位置
        } buffer;
        /* Stdio file pointer target. */
		/*
		以file为底层io的rio实例，write操作将参数buf中的数据写入到文件中，
		而read操作则将file中的数据读到参数buf指向的内存地址中。
		file对象的抽象接口实现只需要简单的调用c语言的库函数即可。
		同样由于抽象接口未声明seek操作，它的具体实现也没有实现seek操作
		这里的buffered记录了写操作的累计数据量，而autosync为设置一个同步值，
		当buffered值超过autosync值后，会执行sync操作使数据同步到磁盘上，
		sync操作后将buffered值清零。
		*/
        struct {
            FILE *fp;
            off_t buffered; /* Bytes written since last fsync. */
            off_t autosync; /* fsync after 'autosync' bytes written. */
        } file;
        /* Multiple FDs target (used to write to N sockets). */
		/*
		fdset的write操作可以将一份数据向多个socket发送。对fdset的抽象大大地简化了
		redis的master向它的多个slave发送同步数据的 io操作fdset不支持read操作
		。此外，它使用了类似buffer的一个sds实例作为缓存，数据首先被写入到该缓存中，
		当缓存中的数据超过一定数量，或者调用了flush操作，再将缓存中的数据发送到所有
		的socket中
		*/
        struct {
            int *fds;       /* File descriptors. 所有的目标socket的文件描述符集合*/
            int *state;     /* Error state of each fd. 0 (if ok) or errno.state记录了这些文件描述符的状态(是否发生写错误) */
            int numfds;		/*numfds记录了集合的大小，*/
            off_t pos;		/*pos代表buf中下一个应该发送的数据的位置*/
            sds buf;		/*buf为缓存*/
        } fdset;
    } io;
};

typedef struct _rio rio;

/* The following functions are our interface with the stream. They'll call the
 * actual implementation of read / write / tell, and will update the checksum
 * if needed. */

static inline size_t rioWrite(rio *r, const void *buf, size_t len) {
    while (len) {
        size_t bytes_to_write = (r->max_processing_chunk && r->max_processing_chunk < len) ? r->max_processing_chunk : len;
        if (r->update_cksum) r->update_cksum(r,buf,bytes_to_write);
        if (r->write(r,buf,bytes_to_write) == 0)
            return 0;
        buf = (char*)buf + bytes_to_write;
        len -= bytes_to_write;
        r->processed_bytes += bytes_to_write;
    }
    return 1;
}

static inline size_t rioRead(rio *r, void *buf, size_t len) {
    while (len) {
        size_t bytes_to_read = (r->max_processing_chunk && r->max_processing_chunk < len) ? r->max_processing_chunk : len;
        if (r->read(r,buf,bytes_to_read) == 0)
            return 0;
        if (r->update_cksum) r->update_cksum(r,buf,bytes_to_read);
        buf = (char*)buf + bytes_to_read;
        len -= bytes_to_read;
        r->processed_bytes += bytes_to_read;
    }
    return 1;
}

static inline off_t rioTell(rio *r) {
    return r->tell(r);
}

static inline int rioFlush(rio *r) {
    return r->flush(r);
}

/* 每一个底层对象都需要实现它需要支持的接口，实例化rio时，
rio结构中的函数指针将指向底io的实现。Redis是C语言实现，
因此针对 三个底层io声明了三个对应的初始化函数：
这三个函数将初始化rio实例中的函数指针为它对应的抽象接口实现，
并初始化union结构指向正确的底层io对象。


*/
void rioInitWithFile(rio *r, FILE *fp);
void rioInitWithBuffer(rio *r, sds s);
void rioInitWithFdset(rio *r, int *fds, int numfds);

void rioFreeFdset(rio *r);

size_t rioWriteBulkCount(rio *r, char prefix, long count);
size_t rioWriteBulkString(rio *r, const char *buf, size_t len);
size_t rioWriteBulkLongLong(rio *r, long long l);
size_t rioWriteBulkDouble(rio *r, double d);

struct redisObject;
int rioWriteBulkObject(rio *r, struct redisObject *obj);

void rioGenericUpdateChecksum(rio *r, const void *buf, size_t len);
void rioSetAutoSync(rio *r, off_t bytes);

#endif
