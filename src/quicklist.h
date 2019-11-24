/* quicklist.h - A generic doubly linked quicklist implementation
 *
 * Copyright (c) 2014, Matt Stancliff <matt@genges.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
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

#ifndef __QUICKLIST_H__
#define __QUICKLIST_H__
/* Node, quicklist, and Iterator are the only data structures used currently. */

/* quicklistNode is a 32 byte struct describing a ziplist for a quicklist.
 * We use bit fields keep the quicklistNode at 32 bytes.
 * count: 16 bits, max 65536 (max zl bytes is 65k, so max count actually < 32k).
 * encoding: 2 bits, RAW=1, LZF=2.
 * container: 2 bits, NONE=1, ZIPLIST=2.
 * recompress: 1 bit, bool, true if node is temporarry decompressed for usage.
 * attempted_compress: 1 bit, boolean, used for verifying during testing.
 * extra: 10 bits, free for future use; pads out the remainder of 32 bits */

typedef struct quicklistNode{
	struct quicklistNode *prev; //指向上一个ziplist节点  
	struct quicklistNode *next;//指向下一个ziplist节点 
	unsigned char *zl;  		//数据指针，如果没有压缩，就指向ziplist结构，反之指向quicklistLZF结构  
	unsigned int sz;  			//表示指向ziplist结构的总长度(内存占用长度)  /* ziplist size in bytes */
	unsigned count:16;			//表示ziplist中的数据项个数  /* count of items in ziplist */
	unsigned int encoding:2;		//编码方式  1-ziplist  ,2quicklistLZF /* RAW==1 or LZF==2 */
	unsigned int container:2;			//预留字段，1-	NONE,2-ziplist  /* NONE==1 or ZIPLIST==2 */
	unsigned int recompress:1;			 // 解压标记，当查看一个被压缩的数据时，需要暂时解压，标记此参数为1，之后再重新进行压缩 /* was this node previous compressed? */
	
	unsigned int attempted_compress : 1; // 测试相关  /* node can't compress; too small */
	 unsigned int extra : 10; // 扩展字段，暂时没用 /* more bits to steal for future usage */
}quicklistNode;
/* quicklistLZF is a 4+N byte struct holding 'sz' followed by 'compressed'.
 * 'sz' is byte length of 'compressed' field.
 * 'compressed' is LZF data with total (compressed) length 'sz'
 * NOTE: uncompressed length is stored in quicklistNode->sz.
 * When quicklistNode->zl is compressed, node->zl points to a quicklistLZF */
 //一种采用LZF压缩算法压缩的数据结构
typedef struct quicklistLZF{
	unsigned int sz;	//LZF压缩后占用的字节数
	char compressed[];		//柔性数组，指向数据部分
} quicklistLZF;
/* quicklist is a 40 byte struct (on 64-bit systems) describing a quicklist.
 * 'count' is the number of total entries.
 * 'len' is the number of quicklist nodes.
 * 'compress' is: -1 if compression disabled, otherwise it's the number
 *                of quicklistNodes to leave uncompressed at ends of quicklist.
 * 'fill' is the user-requested (or default) fill factor. */
 //8+8+8+8+8
 //quicklist的这个结构体在源码中说是占用了40byte的空间，怎么计算的呢？这边涉及到了位域的概念，
 //所谓”位域“是把一个字节中的二进位划分为几 个不同的区域， 并说明每个区域的位数。每个域有一个域名，
 //允许在程序中按域名进行操作。比如这个“int fill : 16” 表示不用整个int存储fill，而是只用了其中的16位来存储。
 typedef struct quicklist{
	quicklistNode *head;
	quicklistNode *tail;
	unsigned long count;  //  total count of all entries in all ziplists   ziplist中的entry节点计数器
	unsigned long len;			//quicklist的quicklistNode节点计数器 /* number of quicklistNodes */ 
	int fill:16;					 // ziplist大小限定，由list-max-ziplist-size给定
	unsigned int compress:16;			//节点压缩深度设置，由list-compress-depth给定 /* depth of end nodes not to compress;0=off */
 }quicklist;
 // quicklist的迭代器结构
 typedef struct quicklistIter{
	const quicklist *quicklist;// 指向所在quicklist的指针
	quicklistNode *current;     // 指向当前节点的指针
	unsigned char *zi;			// 指向当前节点的ziplist
	long offset;				 // 当前ziplist中的偏移地址
	int direction;					 // 迭代器的方向
 }quicklistIter;

typedef struct quicklistEntry{
	const quicklist *quicklist;		//指向所属的quicklist的指针
	quicklistNode *node;			//指向所属的quicklistNode节点的指针
	unsigned char *zi;				 //指向当前ziplist结构的指针
	unsigned char *value;				//指向当前ziplist结构的字符串vlaue成员  
	long long longval;					//指向当前ziplist结构的整数value成员
	unsigned int sz;					//保存当前ziplist结构的字节数大小
	int offset;							//保存相对ziplist的偏移量
}quicklistEntry;


#define QUICKLIST_HEAD 0
#define QUICKLIST_TAIL -1

/* quicklist node encodings */
//编码方式
#define QUICKLIST_NODE_ENCODING_RAW 1
#define QUICKLIST_NODE_ENCODING_LZF 2

/* quicklist compression disable */
#define QUICKLIST_NOCOMRESS 0

/* quicklist container formats */
#define QUICKLIST_NODE_CONTAINER_node 1
#define QUICKLIST_NODE_CONTAINER_ZIPLIST 2

/* Directions for iterators */
//迭代方向
#define AL_START_HEAD 0
#define AL_START_TAIL 1;

#define quicklistNodeIsCompressed(node) ((node)->encoding == QUICKLIST_NODE_ENCODING_LZF)

/* Prototypes */
quicklist *quicklistCreate(void);
quicklist *quicklistNew(int fill,int compress);
void quicklistSetCompressDepth(quicklist *quicklist,int depth);
int quicklistPushHead(quicklist *quicklist,void *value,const size_t sz);
int quicklistPushTail(quicklist *quicklist,void *value,const size_t sz);
void quicklistPush(quicklist *quicklist,void *value,const size_t sz,int where);







#endif /* __QUICKLIST_H__ */
