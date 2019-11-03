/* The ziplist is a specially encoded dually linked list that is designed
 * to be very memory efficient. It stores both strings and integer values,
 * where integers are encoded as actual integers instead of a series of
 * characters. It allows push and pop operations on either side of the list
 * in O(1) time. However, because every operation requires a reallocation of
 * the memory used by the ziplist, the actual complexity is related to the
 * amount of memory used by the ziplist.
 *
 * ----------------------------------------------------------------------------
 *
 * ZIPLIST OVERALL LAYOUT
 * ======================
 *
 * The general layout of the ziplist is as follows:
 *
 * <zlbytes> <zltail> <zllen> <entry> <entry> ... <entry> <zlend>
 *
 * NOTE: all fields are stored in little endian, if not specified otherwise.
 *
 * <uint32_t zlbytes> is an unsigned integer to hold the number of bytes that
 * the ziplist occupies, including the four bytes of the zlbytes field itself.
 * This value needs to be stored to be able to resize the entire structure
 * without the need to traverse it first.
 *
 * <uint32_t zltail> is the offset to the last entry in the list. This allows
 * a pop operation on the far side of the list without the need for full
 * traversal.
 *
 * <uint16_t zllen> is the number of entries. When there are more than
 * 2^16-2 entries, this value is set to 2^16-1 and we need to traverse the
 * entire list to know how many items it holds.
 *
 * <uint8_t zlend> is a special entry representing the end of the ziplist.
 * Is encoded as a single byte equal to 255. No other normal entry starts
 * with a byte set to the value of 255.
 *
 * ZIPLIST ENTRIES
 * ===============
 *
 * Every entry in the ziplist is prefixed by metadata that contains two pieces
 * of information. First, the length of the previous entry is stored to be
 * able to traverse the list from back to front. Second, the entry encoding is
 * provided. It represents the entry type, integer or string, and in the case
 * of strings it also represents the length of the string payload.
 * So a complete entry is stored like this:
 *
 * <prevlen> <encoding> <entry-data>
 *
 * Sometimes the encoding represents the entry itself, like for small integers
 * as we'll see later. In such a case the <entry-data> part is missing, and we
 * could have just:
 *
 * <prevlen> <encoding>
 *
 * The length of the previous entry, <prevlen>, is encoded in the following way:
 * If this length is smaller than 254 bytes, it will only consume a single
 * byte representing the length as an unsinged 8 bit integer. When the length
 * is greater than or equal to 254, it will consume 5 bytes. The first byte is
 * set to 254 (FE) to indicate a larger value is following. The remaining 4
 * bytes take the length of the previous entry as value.
 *
 * So practically an entry is encoded in the following way:
 *
 * <prevlen from 0 to 253> <encoding> <entry>
 *
 * Or alternatively if the previous entry length is greater than 253 bytes
 * the following encoding is used:
 *
 * 0xFE <4 bytes unsigned little endian prevlen> <encoding> <entry>
 *
 * The encoding field of the entry depends on the content of the
 * entry. When the entry is a string, the first 2 bits of the encoding first
 * byte will hold the type of encoding used to store the length of the string,
 * followed by the actual length of the string. When the entry is an integer
 * the first 2 bits are both set to 1. The following 2 bits are used to specify
 * what kind of integer will be stored after this header. An overview of the
 * different types and encodings is as follows. The first byte is always enough
 * to determine the kind of entry.
 *
 * |00pppppp| - 1 byte
 *      String value with length less than or equal to 63 bytes (6 bits).
 *      "pppppp" represents the unsigned 6 bit length.
 * |01pppppp|qqqqqqqq| - 2 bytes
 *      String value with length less than or equal to 16383 bytes (14 bits).
 *      IMPORTANT: The 14 bit number is stored in big endian.
 * |10000000|qqqqqqqq|rrrrrrrr|ssssssss|tttttttt| - 5 bytes
 *      String value with length greater than or equal to 16384 bytes.
 *      Only the 4 bytes following the first byte represents the length
 *      up to 32^2-1. The 6 lower bits of the first byte are not used and
 *      are set to zero.
 *      IMPORTANT: The 32 bit number is stored in big endian.
 * |11000000| - 3 bytes
 *      Integer encoded as int16_t (2 bytes).
 * |11010000| - 5 bytes
 *      Integer encoded as int32_t (4 bytes).
 * |11100000| - 9 bytes
 *      Integer encoded as int64_t (8 bytes).
 * |11110000| - 4 bytes
 *      Integer encoded as 24 bit signed (3 bytes).
 * |11111110| - 2 bytes
 *      Integer encoded as 8 bit signed (1 byte).
 * |1111xxxx| - (with xxxx between 0000 and 1101) immediate 4 bit integer.
 *      Unsigned integer from 0 to 12. The encoded value is actually from
 *      1 to 13 because 0000 and 1111 can not be used, so 1 should be
 *      subtracted from the encoded 4 bit value to obtain the right value.
 * |11111111| - End of ziplist special entry.
 *
 * Like for the ziplist header, all the integers are represented in little
 * endian byte order, even when this code is compiled in big endian systems.
 *
 * EXAMPLES OF ACTUAL ZIPLISTS
 * ===========================
 *
 * The following is a ziplist containing the two elements representing
 * the strings "2" and "5". It is composed of 15 bytes, that we visually
 * split into sections:
 *
 *  [0f 00 00 00] [0c 00 00 00] [02 00] [00 f3] [02 f6] [ff]
 *        |             |          |       |       |     |
 *     zlbytes        zltail    entries   "2"     "5"   end
 *
 * The first 4 bytes represent the number 15, that is the number of bytes
 * the whole ziplist is composed of. The second 4 bytes are the offset
 * at which the last ziplist entry is found, that is 12, in fact the
 * last entry, that is "5", is at offset 12 inside the ziplist.
 * The next 16 bit integer represents the number of elements inside the
 * ziplist, its value is 2 since there are just two elements inside.
 * Finally "00 f3" is the first entry representing the number 2. It is
 * composed of the previous entry length, which is zero because this is
 * our first entry, and the byte F3 which corresponds to the encoding
 * |1111xxxx| with xxxx between 0001 and 1101. We need to remove the "F"
 * higher order bits 1111, and subtract 1 from the "3", so the entry value
 * is "2". The next entry has a prevlen of 02, since the first entry is
 * composed of exactly two bytes. The entry itself, F6, is encoded exactly
 * like the first entry, and 6-1 = 5, so the value of the entry is 5.
 * Finally the special entry FF signals the end of the ziplist.
 *
 * Adding another element to the above string with the value "Hello World"
 * allows us to show how the ziplist encodes small strings. We'll just show
 * the hex dump of the entry itself. Imagine the bytes as following the
 * entry that stores "5" in the ziplist above:
 *
 * [02] [0b] [48 65 6c 6c 6f 20 57 6f 72 6c 64]
 *
 * The first byte, 02, is the length of the previous entry. The next
 * byte represents the encoding in the pattern |00pppppp| that means
 * that the entry is a string of length <pppppp>, so 0B means that
 * an 11 bytes string follows. From the third byte (48) to the last (64)
 * there are just the ASCII characters for "Hello World".
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2009-2017, Salvatore Sanfilippo <antirez at gmail dot com>
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
#include <stdint.h>
#include <limits.h>
#include "zmalloc.h"
#include "util.h"
#include "ziplist.h"
#include "endianconv.h"
#include "redisassert.h"

#define ZIP_END 255         /* Special "end of ziplist" entry. */
//不太理解作用？？
#define ZIP_BIG_PREVLEN 254 /* Max number of bytes of the previous entry, for
                               the "prevlen" field prefixing each entry, to be
                               represented with just a single byte. Otherwise
                               it is represented as FF AA BB CC DD, where
                               AA BB CC DD are a 4 bytes unsigned integer
                               representing the previous entry len. */

/* Different encoding/length possibilities */
#define ZIP_STR_MASK 0xc0
#define ZIP_INT_MASK 0x30
//字节数组编码
#define ZIP_STR_06B (0 << 6)
#define ZIP_STR_14B (1 << 6)
#define ZIP_STR_32B (2 << 6)
//整数编码
#define ZIP_INT_16B (0xc0 | 0<<4)
#define ZIP_INT_32B (0xc0 | 1<<4)
#define ZIP_INT_64B (0xc0 | 2<<4)
#define ZIP_INT_24B (0xc0 | 3<<4)
#define ZIP_INT_8B 0xfe

/* 4 bit integer immediate encoding |1111xxxx| with xxxx between
 * 0001 and 1101. */
#define ZIP_INT_IMM_MASK 0x0f   /* Mask to extract the 4 bits value. To add
                                   one is needed to reconstruct the value. */
#define ZIP_INT_IMM_MIN 0xf1    /* 11110001 */
#define ZIP_INT_IMM_MAX 0xfd    /* 11111101 */

#define INT24_MAX 0x7fffff
#define INT24_MIN (-INT24_MAX - 1)

/* Macro to determine if the entry is a string. String entries never start
 * with "11" as most significant bits of the first byte. */
#define ZIP_IS_STR(enc) (((enc) & ZIP_STR_MASK) < ZIP_STR_MASK)

/* Utility macros.*/

/* Return total bytes a ziplist is composed of. */
//zlbytes的指针    压缩列表的字节长度(4个字节)   2^32-1       
#define ZIPLIST_BYTES(zl)       (*((uint32_t*)(zl)))

/* Return the offset of the last item inside the ziplist. */
//zltail的偏移值(zlbytes的地址加上4字节)
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t*)((zl)+sizeof(uint32_t))))

/* Return the length of a ziplist, or UINT16_MAX if the length cannot be
 * determined without scanning the whole ziplist. */
 //指向zllen的地址(占2个字节)
#define ZIPLIST_LENGTH(zl)      (*((uint16_t*)((zl)+sizeof(uint32_t)*2)))

/* The size of a ziplist header: two 32 bit integers for the total
 * bytes count and last item offset. One 16 bit integer for the number
 * of items field. */
 //压缩列表头部长度
#define ZIPLIST_HEADER_SIZE     (sizeof(uint32_t)*2+sizeof(uint16_t))

/* Size of the "end of ziplist" entry. Just one byte. */
//压缩列表的尾部长度
#define ZIPLIST_END_SIZE        (sizeof(uint8_t))

/* Return the pointer to the first entry of a ziplist. */
//压缩列表第一个数据段的地址(entry的地址)
#define ZIPLIST_ENTRY_HEAD(zl)  ((zl)+ZIPLIST_HEADER_SIZE)

/* Return the pointer to the last entry of a ziplist, using the
 * last entry offset inside the ziplist header. */
 //
 //按照小端存法(不是太理解？？)
 //
#define ZIPLIST_ENTRY_TAIL(zl)  ((zl)+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)))

/* Return the pointer to the last byte of a ziplist, which is, the
 * end of ziplist FF entry. */
 //返回最后一个entry的地址
#define ZIPLIST_ENTRY_END(zl)   ((zl)+intrev32ifbe(ZIPLIST_BYTES(zl))-1)

/* Increment the number of items field in the ziplist header. Note that this
 * macro should never overflow the unsigned 16 bit integer, since entries are
 * always pushed one at a time. When UINT16_MAX is reached we want the count
 * to stay there to signal that a full scan is needed to get the number of
 * items inside the ziplist. */
 //不是太理解？？
#define ZIPLIST_INCR_LENGTH(zl,incr) { \
    if (ZIPLIST_LENGTH(zl) < UINT16_MAX) \
        ZIPLIST_LENGTH(zl) = intrev16ifbe(intrev16ifbe(ZIPLIST_LENGTH(zl))+incr); \
}
//这个是逻辑上ziplist的结构,实际存储的时候,对ziplist进行了编码
typedef struct zlentry{
	unsigned int prevrawlensize;
			
	unsigned int prevrawlen;

	unsigned int lensize;

	unsigned int len;

	unsigned int headersize;

	unsigned char encoding;

	unsigned char *p;
}
//解码previous_entry_length   初始化zlentry中的prelensize和prevrawlen
#define ZIP_DECODE_PREVLENSIZE(ptr,prevlensize) do {			\
	if((ptr)[0]<ZIP_BIG_PREVLEN){								\
		(prevlensize)	=1;										\
	}else{														\
		(prevlensize)=5;										\
	}															\
}while(0);
	//不理解??
#define ZIP_DECODE_PREVLEN(ptr,prevlensize,prevlen) do { \
	ZIP_DECODE_PREVLENSIZE(ptr, prevlensize);	\
	if((prevlensize)==1){					\
		(prevlen)=(prev)[0];						\
	}else if((prevlensize)==5){							\
		memcpy(&(prevlen),((char*)(ptr))+1,4);				\
		memrev32(&prevlen);					\
	}			\

}while(0);
//
#define ZIP_ENTRY_ENCODING(ptr,encoding) do {		\
	(encoding)=(ptr[0]);			\
	if((encoding)<ZIP_STR_MASK) (encoding) &=ZIP_STR_MASK;\
}while(0);


#define ZIP_DECODE_LENGTH(ptr,encoding,lensize,len) do {		\
	if((encoding)<ZIP_STR_MASK)	{								\
		if((encoding)==ZIP_STR_06B){							\
			(lensize)=1;										\
			(len)=(ptr)[0]&0x3f;								\
		}else if((encoding)==ZIP_STR_14B){						\
			(lensize)=2;										\
			(len)=(((ptr)[0]&0x3f)<<8)|(ptr)[1];				\
		}else if((encoding)==ZIP_STR_32B){						\
			(lensize)=5;										\
			(len)=((ptr)[1]<<24) |								\
					((ptr)[2]<<16) |							\
					((ptr)[3]<<8)|								\
					((ptr)[4]);									\
			)
		}else{													\
			panic("Invalid string encoding 0x%02X", (encoding));\
		}														\
	}else{														\
		(lensize)=1;											\
		(len)=zipIntSize(encoding);								\
	}															\
}while(0);





/* Return bytes needed to store integer encoded by 'encoding'. */
//整数编码类型
unsigned int zipIntSize(unsigned char encoding){
	switch (encoding)
		{
			case ZIP_INT_8B: return 1;
			case ZIP_INT_16B:return 2;
			case ZIP_INT_24B:return 3;
			case ZIP_INT_32B:return 4;
			case ZIP_INT_64B:return 8;					
		}
	if(encoding>=ZIP_INT_IMM_MIN &&encoding <=ZIP_INT_IMM_MAX)
		return 0;   //介于1-11之间的整数
	anic("Invalid integer encoding 0x%02X", encoding);
	return 0；
}

//根据内存返回zlentry结构体
void zipEntry(unsigned char *p,zlentry *e){
	//还需要研究不太明白？？
	ZIP_DECODE_PREVLEN(P,e->prevlensize, e->prevlen);
	ZIP_DECODE_LENGTH(p+e->prevrawlensize, e->encoding, e->lensize, e->len);
	e->headersize=e->prevrawlensize+e->lensize;
	e->p=p;
}

/* Create a new empty ziplist. */
//创建压缩列表
unsigned char *ziplistNew(void){
	unsigned int bytes=ZIPLIST_HEADER_SIZE+ZIPLIST_END_SIZE;
	//分配空间
	unsigned char *zl=zmalloc(bytes);
	//初始化
	ZIPLIST_BYTES(zl)=intrev32ifbe(bytes);//统一小端存储   ，还需要深入研究
	ZIPLIST_TAIL_OFFSET(zl)=0; //元素个数为0
	zl[bytes-1]=ZIP_END;
	return zl;
}
/* Return the total number of bytes used by the entry pointed to by 'p'. */
//获取p指向的entry元素的长度
unsigned int zipRawEntryLength(unsigned char *p){
	unsigned int prevlensize,encoding,lensize,len;
	ZIP_DECODE_PREVLEN(p, prevlensize);
	ZIP_DECODE_LENGTH(p+prevlensize, encoding, lensize, len);
	return prevlensize+lensize+len;
}

//尝试数据是否可以转换为整数
int zipTryEncoding(unsigned char *entry,unsigned int entrylen,long long *v,unsigned char *encoding){
	long long value;
	//不理解？？
	if(entrylen>=32||entrylen==0) return 0;
	if(string2ll((char *)entry, entrylen, &value)){
		/* Great, the string can be encoded. Check what's the smallest
         * of our encoding types that can hold this value. */
         //从最小值开始检查
         if(value>=0&&&value<=12){
				*encoding=ZIP_INT_IMM_MIN+value;
		 }else if(value>=INT8_MIN&&value<=INT8_MAX){
				*encoding=ZIP_INT_8B;
		 }else if(value>=INT16_MIN&&value<=INT16_MAX){
				*encoding=ZIP_INT_16B;
		 }else if(value>=INT24_MIN&&value<=INT24_MAX){
				*encoding=ZIP_INT_24B;		 	     
		 }else if(value>=INT32_MIN&&value<=INT32_MAX){
	    		 *encoding=ZIP_INT_32B; 		 
		 }else{
				*encoding=ZIP_INT_64B;
		 }
		 *v=value;
		 return 1;
	}
	return 0;
}

/* Encode the length of the previous entry and write it to "p". This only
 * uses the larger encoding (required in __ziplistCascadeUpdate). */
 



/* Encode the length of the previous entry and write it to "p". Return the
 * number of bytes needed to encode this length if "p" is NULL. */
 //previous_entry_length所需空间大小
unsigned int zipStorePrevEntryLength(unsigned char *p,unsigned int len){
	if(p==NULL){
		return (len<ZIP_BIG_PREVLEN)
	}else{
	
	}
}






