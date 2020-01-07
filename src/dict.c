/* Hash Tables Implementation.
 *
 * This file implements in memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto resize if needed
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

#include "fmacros.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/time.h>

#include "dict.h"
#include "zmalloc.h"
#ifndef DICT_BENCHMARK_MAIN
#include "redisassert.h"
#else
#include <assert.h>
#endif

/* Using dictEnableResize() / dictDisableResize() we make possible to
 * enable/disable resizing of the hash table as needed. This is very important
 * for Redis, as we use copy-on-write and don't want to move too much memory
 * around when there is a child performing saving operations.
 *
 * Note that even when dict_can_resize is set to 0, not all resizes are
 * prevented: a hash table is still allowed to grow if the ratio between
 * the number of elements and the buckets > dict_force_resize_ratio. */
static int dict_can_resize = 1;
static unsigned int dict_force_resize_ratio = 5;

/* -------------------------- private prototypes ---------------------------- */



/* -------------------------- hash functions -------------------------------- */


}

/* ----------------------------- API implementation ------------------------- */
static void _dictReset(dictht *ht){
	ht->table=NULL;
	ht->size=0;
	ht->sizemask=0;
	ht->used=0;
}
/* Performs N steps of incremental rehashing. Returns 1 if there are still
 * keys to move from the old to the new hash table, otherwise 0 is returned.
 *
 * Note that a rehashing step consists in moving a bucket (that may have more
 * than one key as we use chaining) from the old to the new hash table, however
 * since part of the hash table may be composed of empty spaces, it is not
 * guaranteed that this function will rehash even a single bucket, since it
 * will visit at max N*10 empty buckets in total, otherwise the amount of
 * work it does would be unbound and the function may block for a long time. */
/* 实现渐进式的重新哈希，如果还有需要重新哈希的key，返回1，否则返回0
 *
 * 需要注意的是，rehash持续将bucket从老的哈希表移到新的哈希表，但是，因为有的哈希表是空的，
 * 因此函数不能保证即使一个bucket也会被rehash，因为函数最多一共会访问N*10个空bucket，不然的话，函数将会耗费过多性能，而且函数会被阻塞一段时间
 */

int dictRehash(dict d,int n){
	//一次rehash操作执行n*10个buckets
	int empty_visits=n*10;/* Max number of empty buckets to visit. */
	if(!dictIsRehashing(d))	return 0;
	while(n--&&d->ht[0].used !=0){
		dictEntry *de,*nextde;
	
		while (d->ht[0].table[d->rehashidx]==NULL)
			{
				d->rehashidx++;
				if(--empty_visits==0) return 1;//经历empty_visit次bucket空的时 下次rehash
			}
		de=d->ht[0].table[d->rehashidx];
		while (de)
			{
				unit64_t h;

				nextde=de->next;
				/* Get the index in the new hash table */
				//获取新的hash表
				h=dictHashKey(d, de->key)&d->ht[1].sizemask;
				//插入链表的最前面  据说可以省时间??
				de->next =d->ht[1].table[h] ;
				d->ht[1].table[h]=de;

				d->ht[0].used--;//老dictht(节点数)-1
				d->ht[1].used++;//新dictht(节点数)+1
							
			}
		d->ht[0].table[d->rehashidx]=NULL;//当前bucket已经全部移走
		d->rehashidx++;//移动到下一个bucket
	}
	
	
	/* Check if we already rehashed the whole table... */
	//检查是否已经rehash完成
	if(d->ht[0].used==0){
		zfree(d->ht[0].table); //释放ht[0]的buckets空间
		d->ht[0]=d->ht[1];//浅复制 只是操作地址
		_dictReset(&d->ht[1]);
		d->rehashidx=-1;
		return 0;
	}
	
	//继续下次rehash
	return 1;
}


/* A fingerprint is a 64 bit number that represents the state of the dictionary
 * at a given time, it's just a few dict properties xored together.
 * When an unsafe iterator is initialized, we get the dict fingerprint, and check
 * the fingerprint again when the iterator is released.
 * If the two fingerprints are different it means that the user of the iterator
 * performed forbidden operations against the dictionary while iterating. */
 //unsafe迭代器在第一次dictNext时用dict的两个dictht的table、size、used进行hash算出一个结果
//最后释放iterator时再调用这个函数生成指纹，看看结果是否一致，不一致就报错.
//safe迭代器不会用到这个
 long long dictFingerprint(dict *d) {
	 long long integers[6], hash = 0;
	 int j;
 
	 integers[0] = (long) d->ht[0].table;//把指针类型转换成long
	 integers[1] = d->ht[0].size;
	 integers[2] = d->ht[0].used;
	 integers[3] = (long) d->ht[1].table;
	 integers[4] = d->ht[1].size;
	 integers[5] = d->ht[1].used;
 
	 /* We hash N integers by summing every successive integer with the integer
	  * hashing of the previous sum. Basically:
	  *
	  * Result = hash(hash(hash(int1)+int2)+int3) ...
	  *
	  * This way the same set of integers in a different order will (likely) hash
	  * to a different number. */
	 for (j = 0; j < 6; j++) {
		 hash += integers[j];
		 /* For the hashing step we use Tomas Wang's 64 bit integer hash. */
		 hash = (~hash) + (hash << 21); // hash = (hash << 21) - hash - 1;
		 hash = hash ^ (hash >> 24);
		 hash = (hash + (hash << 3)) + (hash << 8); // hash * 265
		 hash = hash ^ (hash >> 14);
		 hash = (hash + (hash << 2)) + (hash << 4); // hash * 21
		 hash = hash ^ (hash >> 28);
		 hash = hash + (hash << 31);
	 }
	 return hash;
 }

 dictIterator *dictGetIterator(dict *d){
	dictIterator *iter =zmalloc(sizeof(*iter));

	iter->d=d;
	iter->table=0;
	iter->index=-1;
	iter->safe=0;
	 iter->entry = NULL;
    iter->nextEntry = NULL;
    return iter;	
 }

 dictIterator *dictGetSafeIterator(dict *d) {
    dictIterator *i = dictGetIterator(d);

    i->safe = 1;
    return i;
}

dictEntry *dictNext(dictIterator *iter){
	while (1)
		{
			if(iter->entey==NULL){	//新new的dictIterator的entry是null，或者到达一个bucket的链表尾部
				dictht *ht=&iter->d->ht[iter->table];//dictht的地址
				if(iter->index==-1&&iter->table==0){
					//刚new的dictIterator
					if(iter->safe)
						iter->d->iterator++;
					else
						iter->fingerprint=dictFingerprint(iter->d);//初始化unsafe迭代器的指纹，只进行一次
				}
				 iter->index++;//buckets的下一个索引
				 if(iter->index>=(long)ht->size){
					//如果buckets的索引大于等于这个buckets的大小，则这个buckets迭代完毕
					if(dictIsRehashing(iter->d)&&iter->table==0){//当前使用的是dictht[0]
						//考虑rehash，换第二个dictht 
						iter->table++;
						iter->index=0;//index复原成0，从第二个dictht的第0个bucket迭代
						ht = &iter->d->ht[1];//设置这个是为了rehash时更新下面的iter->entry
					}else{
						break;
					}
				 }
				 iter->entry=ht->table[iter->index];
				
			}else{
				iter->entry=ht->table[iter->index];
			}
			
			
		}
	return NULL;
}
/* This function performs just a step of rehashing, and only if there are
 * no safe iterators bound to our hash table. When we have iterators in the
 * middle of a rehashing we can't mess with the two hash tables otherwise
 * some element can be missed or duplicated.
 *
 * This function is called by common lookup or update operations in the
 * dictionary so that the hash table automatically migrates from H1 to H2
 * while it is actively used. */
 static void _dictRehashStep(dict *d){
 	//有迭代器在执行不适用rehash，防止数据混乱
	if(d->iterators==0) dictRehash(d,1);
 }


//字典中查找数据
dictEntry *dictFind(dict *d,const void *key){
	dictEntry *he;
	unit64_t h,idx,table;

	if(d->ht[0].used+d->ht[1].used==0) return NULL;  //字典为空
	if(dictIsRehashing(d)) _dictRehashStep(d);
	h=dictHashKey(d, key);
	for(table=0;table<=1;table++){
		idx=h & d->ht[table].sizemask;
		he=d->ht[table].table.idx;
		while (he)
		{
			if(key==he->key||dictCompareKeys(d,key,he->key))
				return he;
			he=he->next;
		}
		if(!dictIsRehashing(d)) return NULL;
	}
	return NULL;
}



void *dictFetchValue(dict * d, const void * key){
	dictEntry *he;

	he=dictFind(d,key);  //返回值赋值给节点
	return he?dictGetVal(he):NULL;
}
/* ------------------------------- Debugging ---------------------------------*/
/* ------------------------------- Benchmark ---------------------------------*/
#endif
