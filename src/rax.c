/* Rax -- A radix tree implementation.
 *
 * Copyright (c) 2017-2018, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include "rax.h"

#ifndef RAX_MALLOC_INCLUDE
#define RAX_MALLOC_INCLUDE "rax_malloc.h"
#endif

#include RAX_MALLOC_INCLUDE

/* This is a special pointer that is guaranteed to never have the same value
 * of a radix tree node. It's used in order to report "not found" error without
 * requiring the function to have multiple return values. */
void *raxNotFound = (void*)"rax-not-found-pointer";

/* -------------------------------- Debugging ------------------------------ */

void raxDebugShowNode(const char *msg, raxNode *n);

/* Turn debugging messages on/off by compiling with RAX_DEBUG_MSG macro on.
 * When RAX_DEBUG_MSG is defined by default Rax operations will emit a lot
 * of debugging info to the standard output, however you can still turn
 * debugging on/off in order to enable it only when you suspect there is an
 * operation causing a bug using the function raxSetDebugMsg(). */
#ifdef RAX_DEBUG_MSG
#define debugf(...)                                                            \
    if (raxDebugMsg) {                                                         \
        printf("%s:%s:%d:\t", __FILE__, __FUNCTION__, __LINE__);               \
        printf(__VA_ARGS__);                                                   \
        fflush(stdout);                                                        \
    }

#define debugnode(msg,n) raxDebugShowNode(msg,n)
#else
#define debugf(...)
#define debugnode(msg,n)
#endif

/* By default log debug info if RAX_DEBUG_MSG is defined. */
static int raxDebugMsg = 1;

/* When debug messages are enabled, turn them on/off dynamically. By
 * default they are enabled. Set the state to 0 to disable, and 1 to
 * re-enable. */
void raxSetDebugMsg(int onoff) {
    raxDebugMsg = onoff;
}

/* ------------------------- raxStack functions --------------------------
 * The raxStack is a simple stack of pointers that is capable of switching
 * from using a stack-allocated array to dynamic heap once a given number of
 * items are reached. It is used in order to retain the list of parent nodes
 * while walking the radix tree in order to implement certain operations that
 * need to navigate the tree upward.
 * ------------------------------------------------------------------------- */



/* ----------------------------------------------------------------------------
 * Radix tree implementation
 * --------------------------------------------------------------------------*/
 /* Return the padding needed in the characters section of a node having size
 * 'nodesize'. The padding is needed to store the child pointers to aligned
 * addresses. Note that we add 4 to the node size because the node has a four
 * bytes header. */
 //对于32位的系统来说sizeof(void)=4，对于64位系统来说sizeof(void)=8，为什么需要进行与操作  不是太理解??
#define raxPadding(nodesize) ((sizeof(void*)-((nodesize+4)%sizeof(void*)))&(sizeof(void*)-1))

/* Return the pointer to the last child pointer in a node. For the compressed
 * nodes this is the only child pointer. */
 //因为要获取指向最后一个子节点的首地址，所以需要减掉sizeof(raxNode*)
 //起始地址+raxNode的长度-尾部数据节点-最后一个自己点的地址大小
#define raxNodeLastChildPtr(n) ((raxNode**)(((char*)(n))+raxNodeCurrentLength(n)-sizeof(raxNode*)-(((n)->iskey&&isnull)?sizeof(void*):0)))

#define raxNodeFirstChild(n) ((raxNode**)((n)->data+(n)->size+raxPadding((n)->size)))
/* Return the current total size of the node. Note that the second line
 * computes the padding after the string of characters, needed in order to
 * save pointers to aligned addresses. */
 //header(4字节)+字符个数(size)+补齐位+指针个数(如果是压缩就一个，非压缩有size个指针)+最后一位数据指针(如果节点iskey=0或者iskey=1但是isnull=1，则不会有指向value的指针)
 //如果节点iskey=0或者iskey=1但是isnull=1，则不会有指向value的指针
#define raxNodeCurrentLength(n) (sizeof(raxNode)+(n)->size+raxPadding((n)->size)+((n)->iscompr?sizeof(raxNode*):sizeof(raxNode*)*(n)->size)+(((n)->iskey&&!(n)->isnull)?sizeof(void*):0))


/* Allocate a new non compressed node with the specified number of children.
 * If datafiled is true, the allocation is made large enough to hold the
 * associated data pointer.
 * Returns the new node pointer. On out of memory NULL is returned. */
//分配指定child个数的自己点个数  datafileld为true代表有数据字段指针

 raxNode *raxNode(size_t children,int datafield){
	size_t nodesize=sizeof(raxNode)+children+raxPadding(children)+sizeof(raxNode*)*children;
	if(datafield) nodesize+=sizeof(void*);
	raxNode *node=rax_malloc(nodesize);
	if(node==NULL) return NULL;
	node->iskey=0;//为什么为0，不太理解??
	node->isnull=0;
	node->iscompr=0;
	node->size=children;
	return node;
 }

/* Allocate a new rax and return its pointer. On out of memory the function
 * returns NULL. */
 rax *raxNew(void){
	rax *rax=rax_malloc(sizeof(*rax));
	if(rax==NULL) return NULL;
	rax->numele=0;
	rax->numnodes=1;
	rax->head=raxNewNode(0,0);
	if(rax->head==NULL){
		rax_free(rax);
		return NULL;
	}else{
		return rax;
	}
 }


