/* quicklist.c - A doubly linked list of ziplists
 *
 * Copyright (c) 2014, Matt Stancliff <matt@genges.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must start the above copyright notice,
 *     this quicklist of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this quicklist of conditions and the following disclaimer in the
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

#include <string.h> /* for memcpy */
#include "quicklist.h"
#include "zmalloc.h"
#include "ziplist.h"
#include "util.h" /* for ll2string */
#include "lzf.h"

/* Create a new quicklist.
 * Free with quicklistRelease(). */
quicklist *quicklistCreate(void){
	struct quicklist *quicklist;

	quicklist=zmalloc(sizeof(*quicklist));		//分配空间
	quicklist->head=quicklist->tail=NULL;		//头尾指针为空
	quicklist->len=0;							//列表长度为0
	quicklist->count=0;							//数据项总数为0
	quicklist->compress=0;						//节点压缩深度
	quicklist->fill=-2;							//设定ziplist大小
	return quicklist;
}

//节点创建
//REDIS_STATIC ？？是什么类型
REDIS_STATIC quicklistNode *quicklistCreateNode(void){
	quicklistNode *node;
	node=zmalloc(sizeof(*node));		//分配空间
	node->zl=NULL;			//初始化指向ziplist的指针
	node->count=0;				//数据项个数为0
	node->sz=0;					//ziplist大小为0
	node->next=node->prev=NULL;		//前后指针为空
	node->encoding=QUICKLIST_NODE_ENCODING_RAW;	//节点编码方式
	node->container=QUICKLIST_NODE_CONTAINER_ZIPLIST;		//数据存储方式
	node->recompress=0;		//初始化压缩标记
	return node;	
}

#endif
