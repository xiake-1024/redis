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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intset.h"
#include "zmalloc.h"
#include "endianconv.h"


/* Note that these encodings are ordered, so:
 * INTSET_ENC_INT16 < INTSET_ENC_INT32 < INTSET_ENC_INT64. */
#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))

/* Return the required encoding for the provided value. */
//从外网内判断
static uint8_t _intsetValueEncoding(int64_t v){
	if(v<INT32_MIN||v>INT32_MAX){
		return INTSET_ENC_INT64;
	}else if(v<INT16_MIN|||v>INT16_MAX){
		return INTSET_ENC_INT32;
	}else
		return INTSET_ENC_INT16
}


/* Resize the intset */
static intset *intsetResize(intset *is,unit32_t len){
	uint32_t size=len*intrev32ifbe(is->encoding);
	is=zrealloc(is,sizeof(intset)+size);
	return is;
}
/* Return the value at pos, given an encoding. */
//根据编码类型，获取pos位置的元素
static int64_t _intsetGetEncoded(intset *is,int pos,uint8_t enc){
	int64_t v64;
	int32_t v32;
	int16_t v16;
	if(enc==INTSET_ENC_INT64){
		memcpy(&v64,((int64_t*)is->contents+pos),sizeof(v64));
		memrev64ifbe(&v64);
		return v64;	
	}else if(enc==INTSET_ENC_INT32){
		memcpy(&v32,((int32_t*)is->contents+pos),sizeof(v32));
		memrev32ifbe(&v32);
		return v32;				
	}else{
		memcpy(&v16,((int16_t*)is->contents+pos),sizeof(v16));
		memrev16ifbe(&v16);
		return v16; 

	}
}
/* Set the value at pos, using the configured encoding. */
//根据编码类型设置指定位置数据
static void _intsetSet(intset *is,int pos,int64_t value){
	uint32_t encoding=intrev32ifbe(is->encoding);

	if(encoding==INSET_ENC_INT64){
		((int64_t*)is->contents)[pos]=value;
		memrev64ifbe(((int64_t*)is->contents)+pos);
	}else if(encoding==INTSET_ENC_INT32){
		((int32_t*)is->contents)[pos]=value;
		memrev64ifbe(((int32_t*)is->contents)+pos);
	}else{
		((int16_t*)is->contents)[pos]=value;
		memrev64ifbe(((int16_t*)is->contents)+pos);
	}
}

/* Return the value at pos, using the configured encoding. */
static int64_t _intsetGet(intset *is, int pos) {
    return _intsetGetEncoded(is,pos,intrev32ifbe(is->encoding));
}


/* Search for the position of "value". Return 1 when the value was found and
 * sets "pos" to the position of the value within the intset. Return 0 when
 * the value is not present in the intset and sets "pos" to the position
 * where "value" can be inserted. */
//查找value的位置  ，如果被找到返回1，并返回位置pos，否则返回0，并将pos设置为value可以被插入的位置
static uint8_t intsetSearch(intset *is,int64_t value,uint32_t *pos){
	int min=0,max=intrev32ifbe(is->length)-1,mid=-1;
	int64_t cur=-1;

	/* The value can never be found when the set is empty */
	if(intrev32ifbe(is->length)==0){
		if(pos) *pos=0;
		return 0;
	}else{
		 /* Check for the case where we know we cannot find the value,
         * but do know the insert position. */
         if(value>_intsetGet(is,max)){
			if(pos) *pos=intrev32ifbe(is->length);
			return 0;
		 }else if(value<_intsetGet(is,0)){
			if(pos)	 *pos=0;
			return 0;
		 }
	}

	//采用二分查找算法进行搜索
	while(max>=min){
		//右移1位  代表除以2
		mid=((unsigned int)min+(unsigned int)max)>>1;
		cur=_intsetGet(is, mid);
		if(value>cur){
				min=mid+1;
		}else if(value<cur){
				max=mid-1;
		}else{
			break;
		}
	}
	//判断找到元素没有
	if(value==cur){
		if(pos) *pos=mid;
		return 1;
	}else{
		if(pos) *pos=min;
		return 0;
	}

}

/* Upgrades the intset to a larger encoding and inserts the given integer. */
//对intset进行扩容 并插入元素
static intset *intsetUpgradeAndAdd(intset *is,int64_t value){
	uint8_t curenc=intrev32ifbe(is->encoding);
	uint8_t newenc=_intsetValueEncoding(value);
	int length=intrev32ifbe(is->length);
	//它要么比目前所有元素都大，要么比所有元素都小，即插入位置要么第一个，要么最后一个
	int prepend=value<-?1:0;

	/* First set new encoding and resize */
	//设置编码方式和重新分配空间
	is->encoding=intrev32ifbe(newenc);
	is=intsetResize(is, intrev32ifbe(is->length)+1);

	/* Upgrade back-to-front so we don't overwrite values.
     * Note that the "prepend" variable is used to make sure we have an empty
     * space at either the beginning or the end of the intset. */
     //从后向前覆盖元素  ，prepend代表通过开始或者结束获取位置
	while(length--)
		_intsetSet(is, length+prepend, _intsetGetEncoded(is, length, curenc));
	/* Set the value at the beginning or the end. */
	if(prepend)
		_intset(is,0,value);
	else
		_intsetSet(is, intrev32ifbe(is->length), value);

	is->length=intrev32ifbe(intrev32ifbe(is->length)+1);
	return is;
}

static void intsetMoveTail(intset *is,unit32_t form,uint32_t to){

}


/* Create an empty intset. */
intset *intsetNew(void){
	intset *is=zmalloc(sizeof(intset));
	//为什么使用INTSET_ENC_INT16?? 默认编码 INTSET_ENC_INT16
	is->encoding=intrev32ifbe(INTSET_ENC_INT16);
	is->length=0;
	return is;
}




/* Insert an integer in the intset */
//向intset里面插入整数
intset *intsetAdd(intset * is, int64_t value, int * success){
	//判断插入数值用哪种长度数据表示
	uint8_t valenc=_intValueEncoding(value);
	unit32_t pos;
	//如果上层关注 是否操作成功 可以传地址进来获取操作结果
	//如果失败的情况少  那么就默认成功
	if(success) *success=1;
	if(valenc>intrev32ifbe(is->encoding)){
		/* This always succeeds, so we don't need to curry *success. */
		return intsetUpgradeAndAdd(is, value);
	}else{
		  /* Abort if the value is already present in the set.
         * This call will populate "pos" with the right position to insert
         * the value when it cannot be found. */
		if(intsetSearch(is,value,&pos)){
			if(success) *success=0;
			return is;
		}

		is=intsetResize(is, intrev32ifbe(is->length)+1);

		  
	}		

}






