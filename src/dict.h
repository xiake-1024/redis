/* Hash Tables Implementation.
 *
 * This file implements in-memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto-resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include <stdint.h>

#ifndef _DICT_H
#define _DICT_H

#define DICT_OK 0
#define DICT_ERR 1

/* Unused arguments generate annoying warnings... */
//不太理解如何使用
#define DICT_NOTUSED(V) ((VOID)V)

typedef struct ditEntry{
	void *key;
	//这个占用多少个字节？
	union {
		void *val;
		uint64_t u64;
		int64_t s64;
		double d;
	} v;
	struct dictEntry *next;  //指向下一个节点的地址 使用链地址法解决冲突
} dictEntry;

typedef struct dictType{
	uint64_t (*hashFunction)(const void *key);
	void *(*keyDup)(void *privdata,const void *key);
	void *(*valDup)(void *privdata,const void *obj);
	int (*keyCompare)(void *privdata,const void *key1,const void *key2);
	void(*keyDestructor)(void *privdata,void *key);
	void (*valDestructor)(void *privdata,void *obj);
} dictType;
/* This is our hash table structure. Every dictionary has two of this as we
 * implement incremental rehashing, for the old to the new table. */

typedef struct dictht{
	dictEntry **table;
	unsigned long size;
	unsigned long sizemask;
	unsigned long used;
} dictht;

typedef struct dict{
	dictType *type;
	void *privdata; /* 保存类型特定函数需要使用的参数 */
	dictht ht[2];
	long rehashidx;/* rehashing not in progress if rehashidx == -1 */
	unsigned long iterators;/* number of iterators currently running */	
}
/* If safe is set to 1 this is a safe iterator, that means, you can call
 * dictAdd, dictFind, and other functions against the dictionary even while
 * iterating. Otherwise it is a non safe iterator, and only dictNext()
 * should be called while iterating. */

//迭代器
typedef struct dictIterator{
	dict *d;
	long index;//当前buckets索引，buckets索引类型是unsinged long，而这个初始化会是-1,所以long
	int table,safe;//safe 区分是否是安全迭代器(1为安全迭代器)
	dictEntry *entry,*nextEntry;//当前hash节点以及下一个hash节点
	long long fingerprint;//dict.c里的dictFingerprint(),不安全迭代器相关
} dictIterator;


/* This is the initial size of every hash table */
//hash表的初始大小
#define DICT_HT_INITIAL_SIZE     4
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)

/* ------------------------------- Macros ------------------------------------*/
#define dictFreeVal(d,entry) if((d)->type->valDestructor) (d)->type->valDestructor((d)->privdata,(entry)->v.val
#define dictFreeKey(d, entry) if ((d)->type->keyDestructor)  (d)->type->keyDestructor((d)->privdata, (entry)->key)
#define dictHashKey(d,key) (d)->type->hasFunction(key)
#define dictIsRehashing(d) ((d)->rehashidx !=-1)
#define dictGetVal(he) ((he)->v.val)
#define dictCompareKeys(d,key1,key2) (((d)->type->keyCompare)?(d)->type->keyCompare((d)->privdata,key1,key2):(key1)==(key2))
//如果支持valDup，则使用将privdata和val作为参数生成val，否则直接使用val
#define dictSetVal(d,entry,_val_) do { if((d)->type->valDup) (entry)->v.val=(d)->type->valDup((d)->privdata,_val_); else (entry)->v.val=(_val_); }while(0)
#define dictSetKey(d,entry,_key_) do { if((d)->type->valDup) (entry)->v.key=(d)->type->valDup((d)->privdata,_key_); else (entry)->v.key=(_key_); }while(0)


//API

int dictRehash(dict *d,int n);
void *dictFetchValue(dict *d,const void *key);
int dictAdd(dict *d,void *key,void *val);
dictEntry *dictUnlink(dict *ht,const void *key);
int dictDelete(dict *d,const void *key);
void dictFreeUnlinkedEntry(dict *d,dictEntry *he);


#endif

