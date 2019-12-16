/*
 * Copyright (c) 2017, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include "server.h"
#include "endianconv.h"
#include "stream.h"

#define STREAM_BYTES_PER_LISTPACK 2048

/* Every stream item inside the listpack, has a flags field that is used to
 * mark the entry as deleted, or having the same field as the "master"
 * entry at the start of the listpack> */
#define STREAM_ITEM_FLAG_NONE 0             /* No special flags. */
#define STREAM_ITEM_FLAG_DELETED (1<<0)     /* Entry is delted. Skip it. */
#define STREAM_ITEM_FLAG_SAMEFIELDS (1<<1)  /* Same fields as master entry. */

void streamFreeCG(streamCG *cg);
void streamFreeNACK(streamNACK *na);
size_t streamReplyWithRangeFromConsumerPEL(client *c, stream *s, streamID *start, streamID *end, size_t count, streamConsumer *consumer);

/* -----------------------------------------------------------------------
 * Low level stream encoding: a radix tree of listpacks.
 * ----------------------------------------------------------------------- */

/* Create a new stream data structure. */
//创建消息队列
steam *streamNew(void){
	stream *s zmalloc(sizeof(*s));
	s->rax=raxNew();
	s->length=0;
	s->last_id.ms=0;
	s->last_id.seq=0;
	s->cgroups=NULL;  /* Created on demand to save memory when not used. *///需要的时候再创建消费组
	return s;	
}

/* Free a stream, including the listpacks stored inside the radix tree. */
//释放stream,包括基数树里面的listpack
void freeStream(stream *s){
	  raxFreeWithCallback(s->rax,(void(*)(void*))lpFree);
    if (s->cgroups)
        raxFreeWithCallback(s->cgroups,(void(*)(void*))streamFreeCG);
    zfree(s);
}
/* Generate the next stream item ID given the previous one. If the current
 * milliseconds Unix time is greater than the previous one, just use this
 * as time part and start with sequence part of zero. Otherwise we use the
 * previous time (and never go backward) and increment the sequence. */
 //根据给出的消息队列item的id，获取下个队列元素的id。如果当前unix时间的毫秒数
 //大于给出的那个，则按照0开始计算。否则用之前的时间并且递增序列号
 void streamNextID(streamID *last_id,streamID *new_id){
	unit64_t ms=mstime();
	if(ms>last_id->ms){  //当前时间大于消息队列中最大的ms
		new_+id->ms=ms;
		new_id=sequence=0;
	}else{
		new_id->ms=last_id->ms;
		new_id->seq	=last_id->seq+1;
	}
 }
 /* This is just a wrapper for lpAppend() to directly use a 64 bit integer
 * instead of a string. */ 
 //其实是对lpinsert的封装 不太理解？？
unsigned char *lpAppendInteger(unsigned char *lp,int64_t value){
	char buf[LONG_STR_SIZE];
	int slen=ll2string(buf, sizeof(buf), value);
	return lpAppend(lp,(unsigned char*)buf,slen);
}

 


/* -----------------------------------------------------------------------
 * Low level implementation of consumer groups 消费组的底层应用
 * ----------------------------------------------------------------------- */
 
 /* Create a NACK entry setting the delivery count to 1 and the delivery
  * time to the current time. The NACK consumer will be set to the one
  * specified as argument of the function. */
 streamNACK *streamCreateNACK(streamConsumer *consumer) {
	 streamNACK *nack = zmalloc(sizeof(*nack));
	 nack->delivery_time = mstime();
	 nack->delivery_count = 1;
	 nack->consumer = consumer;
	 return nack;
 }




