
#include "server.h"
#include <math.h>

int zslLexValueGteMin(sds value, zlexrangespec *spec);
int zslLexValueLteMax(sds value, zlexrangespec *spec);

/*创建跳跃表节点*/
zskiplistNode *zslCreateNode(int level, double score, sds ele) {
  //分配内存
  zskiplistNode *zn=zmalloc(sizeof(*zn)+level*sizeof(struct zskiplistLevel));
  zn->score=score;//分值
  zn->ele=ele;		//sds字符串
  return zn;
}

/* Create a new skiplist. */
zskiplist *zslCreate(void) {
   //分配内存
   zskiplist *zsl=zmalloc(sizeof(zskiplist));
   zsl->length=0;
   zsl->level=1;
   zsl->header=zslCreateNode(ZSKIPLIST_MAXLEVEL, 0, NULL);
   for(int j =0;j<ZSKIPLIST_MAXLEVEL;j++){
		zsl->header->level[j]->forward=NULL;
		zsl->header->level[j]->span=0;
   }
   zsl->header->backward=NULL;
   zsl->tail=NULL;
}


void zslFreeNode(zskiplistNode *node) {
	sdsfree(node->ele);//释放字符串空间
	zfree(node);  //释放节点
}

/* Free a whole skiplist. */
void zslFree(zskiplist *zsl) {
   zskiplistNode *node=zsl->header->level[0].forward,*next;
   zfree(zsl->header);
   while (node)
	   {
	   		next = node->level[0].forward;
			zslFreeNode(node);
			node =next;
	   }
   zfree(zsl);
}

int zslRandomLevel(void) {
   
}

zskiplistNode *zslInsert(zskiplist *zsl, double score, sds ele) {
 	zskiplistNode *update[ZSKIPLIST_MAXLEVEL],*x;
	unsigned int rank[ZSKIPLIST_MAXLEVEL];
	int i,level;

	x=zsl->header;
	for(i=zsl->level-1;i>0;i--){
		rank[i]=i==zsl->level?0:rank[i+1];
		while (x->level[i].forward&&
				(x->level[i].forward.score<score||
				sdscmp(x->level[i].forward.ele,ele)<0))
			{
				rank[i]+=x->level[i].span;
				x=x->level[i].forward;
			}
			update[i]=x;
	}
	level=zslRandomLevel();
	if(level>zsl->level){
		for(i=level;i>zsl->level;i--){
			rank[i]=0;
			update[i]=zsl->header;
			update[i]->level[i].span=zsl->length;
		}
		zsl->level=level;
	}
	
	for(i=0;i<level0;i++){
		//更新forward
		x->level[i].forward=update[i]->level[i].forward;
		update[i]->level[i].forward=x;
		//更新span
		x->level[i].span=update[i]->level[i].span-(rank[0]-rank[x]);
		update[i]->level[i].span=x->level[i].span-(rank[0]-rank[x]);
	}

	for(i=level;i<zsl->level;i++){
		update[i]->level[i].span++;
	}

	//修改x的前项指针和后向指针
	x->backward=(update[0]==zsl->header)?NULL:update[0];
	if(x->level[0].forward){
		x->level[0].forward->backward=x;
	}else{
		zsl->tail=x;
	}
	zsl->length++;
	return x;
}



/* Internal function used by zslDelete, zslDeleteByScore and zslDeleteByRank */
//只是将节点从跳跃表中解绑 具体释放内存上层处理
void zslDeleteNode(zskiplist *zsl, zskiplistNode *x, zskiplistNode **update) {
  //修改每层update的指针和span
  for(int i=0;i<zsl->level;i++){
	if (update[i]->level[i].forward==x)
		{
			update[i]->level[i].span+=x->level[i].span-1;
			update[i]->level[i].forward=x->leve[i].forward;
		}else{
			update[i]->level[i].span-=1;
		}
  }
  //修改x下一个指针的backward指针
	if(x->level[0].forward){
		x->level[0].forward.backward=x->level[0].backward;
	}else{
		zsl->tail=x->backward;
	}
	//缩减zsl层数 level
	while(zsl->level>1&&zsl->header->level[i].forward==NULL)
		zsl->level--;
	
	zsl->length-1;
}

/* Delete an element with matching score/element from the skiplist.
 * The function returns 1 if the node was found and deleted, otherwise
 * 0 is returned.
 *
 * If 'node' is NULL the deleted node is freed by zslFreeNode(), otherwise
 * it is not freed (but just unlinked) and *node is set to the node pointer,
 * so that it is possible for the caller to reuse the node (including the
 * referenced SDS string at node->ele). */
int zslDelete(zskiplist *zsl, double score, sds ele, zskiplistNode **node) {
	zskiplistNode *update[ZSKIPLIST_MAXLEVEL],*x;

	x=zsl->header;
	//竖折结构寻找相应节点
	for(int i=zsl->level-1;i>=0;i--){
		while (x->level[i].forward&&
			(x->level[i].forward.score<score||
			(x->level[i].forward.score==score&&sdscmp(x->level[i].forward.ele,ele)<0)				
			))
			{
				x=x->level[i].forward;
			}
		update[i]=x;
	}
	x=x->level[0].forward;
	if(x&&x->score==score&&sdscmp(x->ele, ele)==0){
		zslDeleteNode(zsl, x, update);
		if(!node){
			zslFreeNode(x);
		}else{
			*node=x;
		}
		return 1;
	}
	return 0;
	
}

/* Update the score of an elmenent inside the sorted set skiplist.
 * Note that the element must exist and must match 'score'.
 * This function does not update the score in the hash table side, the
 * caller should take care of it.
 *
 * Note that this function attempts to just update the node, in case after
 * the score update, the node would be exactly at the same position.
 * Otherwise the skiplist is modified by removing and re-adding a new
 * element, which is more costly.
 *
 * The function returns the updated element skiplist node pointer. */
zskiplistNode *zslUpdateScore(zskiplist *zsl, double curscore, sds ele, double newscore) {
   
}

int zslValueGteMin(double value, zrangespec *spec) {
    return spec->minex ? (value > spec->min) : (value >= spec->min);
}

int zslValueLteMax(double value, zrangespec *spec) {
    return spec->maxex ? (value < spec->max) : (value <= spec->max);
}

/* Returns if there is a part of the zset is in range. */
int zslIsInRange(zskiplist *zsl, zrangespec *range) {
   
}

/* Find the first node that is contained in the specified range.
 * Returns NULL when no element is contained in the range. */
zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range) {
  
}

/* Find the last node that is contained in the specified range.
 * Returns NULL when no element is contained in the range. */
zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range) {
   
}

/* Delete all the elements with score between min and max from the skiplist.
 * Min and max are inclusive, so a score >= min || score <= max is deleted.
 * Note that this function takes the reference to the hash table view of the
 * sorted set, in order to remove the elements from the hash table too. */
unsigned long zslDeleteRangeByScore(zskiplist *zsl, zrangespec *range, dict *dict) {
   
}

unsigned long zslDeleteRangeByLex(zskiplist *zsl, zlexrangespec *range, dict *dict) {
   
}

/* Delete all the elements with rank between start and end from the skiplist.
 * Start and end are inclusive. Note that start and end need to be 1-based */
unsigned long zslDeleteRangeByRank(zskiplist *zsl, unsigned int start, unsigned int end, dict *dict) {
   
}

/* Find the rank for an element by both score and key.
 * Returns 0 when the element cannot be found, rank otherwise.
 * Note that the rank is 1-based due to the span of zsl->header to the
 * first element. */
unsigned long zslGetRank(zskiplist *zsl, double score, sds ele) {
   zskiplistNode *x;
   int i;
   unsigned long rank=0;
   x=zsl->header;
   for(i=zsl->level-1;i>=0;i--){
		while(x->level[i].forward&&
			(x->level[i].forward.score>score||
			sdscmp(x->level[i].forward.ele, ele)<=0)){
				rank+=x->level[i].span;
				x=x->level[i].forward;
			}
		if(x->ele&&sdscmp(x->ele,ele)==0){
			return rank;
		}
   }
   return 0;
}

/* Finds an element by its rank. The rank argument needs to be 1-based. */
zskiplistNode* zslGetElementByRank(zskiplist *zsl, unsigned long rank) {
  	zskiplistNode *x;
	unsigned long traversed=0;
	for(int i=zsl->level-1;i>=0;i--){
		while(x->level[i].forward&&(traversed+x->level[i].span<=rank)){
			traversed+=x->level[i].span;
			x=x->level[i].forward;
		}
		if(traversed==rank){
			return x;
		}
	}
	return NULL;
}

/* Populate the rangespec according to the objects min and max. */
static int zslParseRange(robj *min, robj *max, zrangespec *spec) {
  
}





