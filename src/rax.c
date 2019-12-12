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
/* Initialize the stack. *///stack初始化
static inline void raxStackInit(raxStack *ts){
	ts->stack=ts=>static_items;	//上面调用者设置static_items
	ts->items=0;
	ts->maxitems=RAX_STACK_STATIC_ITEMS;
	ts->oom=0;
}
/* Push an item into the stack, returns 1 on success, 0 on out of memory. */
static inline int raxStackPush(raxStack *ts,void *ptr){
	if(ts->items==ts->maxitems){
		if(ts->stack==ts->static_items){
			ts->stack=rax_malloc(sizeof(void*)*ts->maxitems*2);	//比如32位系统为 32*maxitems*2
			if(ts->stack==NULL){
				ts->stack=ts->static_items;  //还原
				ts->oom=1;
				errno=ENOMEM; //内存不足
				return 0;
			}
			memcpy(ts->stack,ts->static_items,Sizeof(void*)*ts->maxitems);
		}else{
			void **newalloc=rax_realloc(ts->stack,Sizeof(void*)*ts->maxitems*2);
			if(newalloc==NULL){
				ts->oom=1;
				errno=ENOMEM;
				return 0;
			}
			ts->stack=newalloc;
		}
		ts->maxitems*2;
	}
	ts->stack[ts->items]=ptr;
	ts->items++;
	return 1;
}
/* Pop an item from the stack, the function returns NULL if there are no
 * items to pop. */
static inline void *raxStackPop(raxStack *ts){
	if(ts->items==0) return NULL;
	ts->items--;
	return ts->stack-ts->items;
}
/* Return the stack item at the top of the stack without actually consuming
 * it. */
 static inline void *raxStackPeek(raxStack *ts){
	if(ts->items==0) return NULL;
	return ts->stack[ts->items-1];
 }
 /* Free the stack in case we used heap allocation. */
 static inline void raxStackFree(raxStack *ts){
	if(ts->stack != ts->static_items)	rax_free(ts->stack);
 }
/* ----------------------------------------------------------------------------
 * Radix tree implementation
 * --------------------------------------------------------------------------*/
 /* Return the padding needed in the characters section of a node having size
 * 'nodesize'. The padding is needed to store the child pointers to aligned
 * addresses. Note that we add 4 to the node size because the node has a four
 * bytes header. */
 //对于32位的系统来说sizeof(void)=4，对于64位系统来说sizeof(void)=8，为什么需要进行与操作  不是太理解??
#define raxPadding(nodesize) ((sizeof(void*)-((nodesize+4)%sizeof(void*)))&(sizeof(void*)-1))

/* Return the pointer to the first child pointer. */
//获取第一个字节点的指针
#define raxNodeFirstChildPtr(n) ((raxNode*)((n)->data+(n)->size+raxPadding((n)->size)))

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

 raxNode *raxNewNode(size_t children,int datafield){
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
 /* realloc the node to make room for auxiliary data in order
  * to store an item in that node. On out of memory NULL is returned. */
//不是太理解如何使用？
raxNode *raxReallocForData(raxNode *n,void *data){
	if(data==NULL) return n;	/* No reallocation needed, setting isnull=1 */
	size_t curlen=raxNodeCurrentLength(n);
	return rax_realloc(n,curlen+sizeof(void*));
}

/* Set the node auxiliary data to the specified pointer. *///设置指向辅助数据的指针的指针
void raxSetData(raxNode *n,void *data){
	n->iskey=1;
	if(data!=NULL){
		n->isnull=0;
		void **ndata=(void**)((char*)n+raxNodeCurrentLength(n)-sizeof(void*));//指向void*的指针，属于指针的指针
		memcpy(ndata,&data,sizeof(data));
	}else{
		//不是太理解这个如何使用??
		n->isnull=1;
	}
}

/* Get the node auxiliary data. *///返回的是指向数据的指针
void *raxGetData(raxNode *n){
	if(n->isnull) return NULL;//没有数据 返回null
	void **ndata=(void**)((char*)n+raxNodeCurrentLength(n)-sizeof(void*));
	void *data;
	memcpy(&data,ndata,sizeof(data));
	return data;
}

raxNode *raxAddChild(raxNode *n,unsigned char c,raxNode **childptr,raxNode ***parentlink){		
	assert(n->iscompr==0);  //false的时候  答应到stderr,并abort程序

	//这个怎么理解的？
	size_t curlen=raxNodeCurrentLength(n);
	n->size++;
	size_t newlen=raxNodeCurrentLength(n);//新空间 size加1
	n->size--; /* For now restore the orignal size. We'll update it only on
                  success at the end. */
				  	
	 /* Alloc the new child we will link to 'n'. */			  	
	raxNode *child=raxNewNode(0,0);
	if(child==NULL)	return NULL:
	/* Make space in the original node. */
	//为什么这样使用?? 分配空间
	//返回值与newn的地址与n的地址相同代表扩大空间。如果两个地址不相同代表重新分配了一块空间。
	//返回不能用n 因为可能才造成之前的地址被冲掉。
	raxNode *newn=rax_realloc(n,newlen);
	if(newn==NULL){
		rax_free(child);
		return NULL;
	}
	n=newn;
	    /* After the reallocation, we have up to 8/16 (depending on the system
     * pointer size, and the required node padding) bytes at the end, that is,
     * the additional char in the 'data' section, plus one pointer to the new
     * child, plus the padding needed in order to store addresses into aligned
     * locations.
     *
     *
     * So if we start with the following node, having "abde" edges.
     *
     * Note:
     * - We assume 4 bytes pointer for simplicity.
     * - Each space below corresponds to one byte
     *
     * [HDR*][abde][Aptr][Bptr][Dptr][Eptr]|AUXP|
     *
     * After the reallocation we need: 1 byte for the new edge character
     * plus 4 bytes for a new child pointer (assuming 32 bit machine).
     * However after adding 1 byte to the edge char, the header + the edge
     * characters are no longer aligned, so we also need 3 bytes of padding.
     * In total the reallocation will add 1+4+3 bytes = 8 bytes:
     *
     * (Blank bytes are represented by ".")
     *
     * [HDR*][abde][Aptr][Bptr][Dptr][Eptr]|AUXP|[....][....]
     *
     * Let's find where to insert the new child in order to make sure
     * it is inserted in-place lexicographically. Assuming we are adding
     * a child "c" in our case pos will be = 2 after the end of the following
     * loop. */
	 int pos;
	 for(pos=0;pos<n->size;pos++){
		if(n->data[pos]>c) break;
	 }
	  /* Now, if present, move auxiliary data pointer at the end
     * so that we can mess with the other data without overwriting it.
     * We will obtain something like that:
     *
     * [HDR*][abde][Aptr][Bptr][Dptr][Eptr][....][....]|AUXP|
     */
     unsigned char *src,*dst;
	 if(n->iskey&&!n->isnull){
		src=((unsigned char*)n+curlen-sizeof(void*));
		dst=((unsigned char*)n+newlen-sizeof(void*));
	 	memmove(dst,src,sizeof(void*));
	 }
	 
	 /* Compute the "shift", that is, how many bytes we need to move the
		  * pointers section forward because of the addition of the new child
		  * byte in the string section. Note that if we had no padding, that
		  * would be always "1", since we are adding a single byte in the string
		  * section of the node (where now there is "abde" basically).
		  *
		  * However we have padding, so it could be zero, or up to 8.
		  *
		  * Another way to think at the shift is, how many bytes we need to
		  * move child pointers forward *other than* the obvious sizeof(void*)
		  * needed for the additional pointer itself. */
		  //不太理解为何如此使用??
    	size_t shift =newlen-curlen-sizeof(void*);
	 /* We said we are adding a node with edge 'c'. The insertion
     * point is between 'b' and 'd', so the 'pos' variable value is
     * the index of the first child pointer that we need to move forward
     * to make space for our new pointer.
     *
     * To start, move all the child pointers after the insertion point
     * of shift+sizeof(pointer) bytes on the right, to obtain:
     *
     * [HDR*][abde][Aptr][Bptr][....][....][Dptr][Eptr]|AUXP|
     */
	 src=n->data+n->size+raxPadding(n->size)+sizeof(raxNode*)*pos;
	 memmove(src+shift+sizeof(raxNode*),src,sizeof(raxNode*)*(n->size-pos));
	  /* Move the pointers to the left of the insertion position as well. Often
     * we don't need to do anything if there was already some padding to use. In
     * that case the final destination of the pointers will be the same, however
     * in our example there was no pre-existing padding, so we added one byte
     * plus thre bytes of padding. After the next memmove() things will look
     * like thata:
     *
     * [HDR*][abde][....][Aptr][Bptr][....][Dptr][Eptr]|AUXP|
     */
	 if(shift){
		src=(unsigned char *)raxNodeFirstChildPtr(n);
		memmove(src+shift,src,sizeof(raxNode*)*pos);
	 }
	   /* Now make the space for the additional char in the data section,
     * but also move the pointers before the insertion point to the right
     * by shift bytes, in order to obtain the following:
     *
     * [HDR*][ab.d][e...][Aptr][Bptr][....][Dptr][Eptr]|AUXP|
     */
	  
	
}

/* This is the core of raxFree(): performs a depth-first scan of the
 * tree and releases all the nodes found. */
 //这个函数是raxFree的核心，是rax树的第一层遍历，释放所有节点
void raxRecursiveFree(rax *rax,raxNode *n,void (*free_callback)(void*)){
	debugnode("free traversing", n);
	int numchildren=n->iscompr?1:n->size;
	raxNode **cp=raxNodeLastChildPtr(n);
	while(numchildren--){
		raxNode *child;
		memcpy(&child,cp,sizeof(child));
		raxRecursiveFree(rax, child, free_callback);
		cp--;
	}
	debugnode("free depth-first", n);
	if(free_callback&&n->iskey&&!n->isnull)
		free_callback(raxGetData(n));
	rax_free(n);
	rax->numnodes--;
}
/* Free a whole radix tree, calling the specified callback in order to
 * free the auxiliary data. */
 //释放整个基数树 ，调用制定的callback函数 释放数据
 void raxFreeWithCallback(rax * rax, void(* free_callback)(void *)){
 	raxRecursiveFree(rax,rax->head,free_callback);
	assert(rax->numnodes);
	rax_free(rax);
 }

 /* Free a whole radix tree. */
 //释放基数树，辅助数据字段为空
 void raxFree(rax *rax){
	raxFreeWithCallback(rax,NULL);
 }
 









 




