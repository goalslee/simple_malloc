/*
实现一个简单的内存管理模块，即malloc 与 free
真正的malloc 内存不够用的时候会sbrk向系统申请，上限是虚拟内存的最大值。
而这个实现事先申请好最大的内存，作为虚拟内存的上限，sbrk的时候仅增大指针值即可。
*/
#include <stddef.h>//size_t
#include <stdlib.h>//malloc
#include <errno.h>//errno
#include <stdio.h>
#include <stdbool.h>

int mm_init(void);
void* mm_malloc(size_t size);
void mm_free(void* ptr);

static char* mem_heap;//指向堆底
static char* mem_brk;//指向当前堆顶+1
static char* mem_max_addr;//最大的虚拟内存地址+1
static char* heap_listtp;//指向序言块

#define MAX_HEAP 100*1024*1024 //100M
#define WSIZE 4//BYTES 字
#define DSIZE 8//BYTES 双字
#define CHUNKSIZE (1<<12)//4*1024=4KB
#define MAX(x,y) ((x)>(y)?(x):(y))

/*将头块/脚块的size和状态整合成一个4字节*/
#define PACK(size,alloc) ((size)|(alloc))

/*获取或写入地址 4个字节*/
#define GET(p) (*(unsigned *)(p))
#define PUT(p,val) (*(unsigned *)(p)=(val))

/*从头块或脚块获取块大小和是否分配*/
#define GET_SIZE(p) (GET(p)&~0x7)
#define GET_ALLOC(p) (GET(p)&~0x1)

/*通过块指针计算头块和脚块*/
#define HDRP(bp) ((char*)(bp)-WSIZE)
#define FTRP(bp) ((char*)(bp)+GET_SIZE(HDRP(bp))-DSIZE)

/*通过块指针计算下一个块指针和上一个块指针*/
#define NEXT_BLKP(bp) ((char*)(bp)+GET_SIZE(((char*)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((char*)(bp)-GET_SIZE(((char*)(bp)-DSIZE)))

static void* coalesce(void *bp);
static void * extend_heap(size_t words);
/*
模拟虚拟内存的最大值，100M
*/
void mem_init(void)
{
    mem_heap=(char*)malloc(MAX_HEAP);
    mem_brk=mem_heap;
    mem_max_addr=(char*)(mem_heap+MAX_HEAP);
}

void *mem_sbrk(int incr)
{
    char* old_brk=mem_brk;
    if((incr<0)||((mem_brk+incr)>mem_max_addr))
    {
        errno=ENOMEM;
        fprintf(stderr,"error:mem_sbrk failed.Run out of memory\n");
        return (void*)-1;
    }
    mem_brk+=incr;
    return (void*)old_brk;
}


/*创建空闲链表*/
int mm_init(void)
{
    if((heap_listtp=mem_sbrk(4*WSIZE))==(void*)-1)
    {
        return -1;
    }
    PUT(heap_listtp,0);
    PUT(heap_listtp+(1*WSIZE),PACK(DSIZE,1));
    PUT(heap_listtp+(2*WSIZE),PACK(DSIZE,1));
    PUT(heap_listtp+(3*WSIZE),PACK(0,1));
    heap_listtp+=(2*WSIZE);
    //扩展空的堆
    if(extend_heap(CHUNKSIZE/WSIZE)==NULL)
    {
        return -1;
    }
    return 0;
}

//双字的倍数对齐--与块的最小值16字节没关系？
//堆初始化和mm_malloc找不到合适的匹配块时，调用xtend_heap向系统申请内存
static void * extend_heap(size_t words)
{
    char* bp;
    size_t size;

    size=(words%2)?(words+1)*WSIZE:words*WSIZE;
    if((long)(bp=mem_sbrk(size))==-1)
    {
        return NULL;
    }

    PUT(HDRP(bp),PACK(size,0));
    PUT(FTRP(bp),PACK(size,0));
    PUT(HDRP(NEXT_BLKP(bp)),PACK(0,1));

    return coalesce(bp);
}

/*释放和合并块
将头和脚块置为可用，再看能否合并
*/
void mm_free(void* bp)
{
    size_t size=GET_SIZE(HDRP(bp));

    PUT(HDRP(bp),PACK(size,0));
    PUT(FTRP(bp),PACK(size,0));
    coalesce(bp);
    printf("free\n");
}

//合并
static void* coalesce(void *bp)
{
    size_t prev_alloc=GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc=GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size=GET_SIZE(HDRP(bp));

    if(prev_alloc&&next_alloc)//前后都已经分配
    {
        return bp;
    }
    else if(prev_alloc&&!next_alloc)
    {
        size+=GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp),PACK(size,0));
        PUT(FTRP(bp),PACK(size,0));
    }
    else if(!prev_alloc&&next_alloc)
    {
        size+=GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(bp),PACK(size,0));
        PUT(FTRP(bp),PACK(size,0));
    }
    else
    {
        size+=GET_SIZE(HDRP(PREV_BLKP(bp)))+
            GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)),PACK(size,0));
        PUT(FTRP(NEXT_BLKP(bp)),PACK(size,0));
        bp=PREV_BLKP(bp);
    }
    return bp;
}

/*放置并分割*/
bool place(char* bp,size_t asize)
{
    /*
    //方案一，仅放置，不分割
    char* headptr=HDRP(bp);
    PUT(headptr,PACK(GET_SIZE(headptr),1));
    */

    //方案二，放置并分割
    char* headptr=HDRP(bp);
    size_t totalsize = GET_SIZE(headptr);
    size_t newsize = totalsize- (DSIZE+asize);
    PUT(headptr,PACK(DSIZE+asize,1));
    char* footptr=FTRP(bp);
    PUT(footptr,PACK(DSIZE+asize,1));

    bp=footptr+WSIZE;
    char* newheadptr=HDRP(bp);
    PUT(newheadptr,PACK(newsize,0));
    char* newfootptr=FTRP(bp);
    PUT(newfootptr,PACK(newsize,0));
    return true;
}

/*首次适配搜索*/
char* find_fit(size_t asize)
{
    char* p=heap_listtp+WSIZE;
    while(GET_SIZE(p))
    {
        if((GET_ALLOC(p)-DSIZE)>=asize)
        {
            return p+WSIZE;
        }
        p+=GET_SIZE(p);
    }
    return NULL;
}

/*分配块，16字节对齐 即2倍双字*/
void *mm_malloc(size_t size)
{
    size_t asize;//ajust block size
    size_t extendsize;
    char* bp;

    if(size==0)
    {
        return NULL;
    }
    if(size<=DSIZE)
    {
        asize=2*DSIZE;
    }
    else
    {
        asize=DSIZE*((size+(DSIZE)+(DSIZE-1))/DSIZE);
    }

    if((bp=find_fit(asize))!=NULL)
    {
        printf("find!\n");
        place(bp,asize);
        return bp;
    }
    extendsize=MAX(asize,CHUNKSIZE);
    if((bp=extend_heap(extendsize/WSIZE))==NULL)
    {
        return NULL;
    }
    place(bp,asize);
    printf("allocate new mem\n");
    return bp;
}


int main()
{
    mem_init();
    mm_init();
    char* p=mm_malloc(1024*1);
    if(p)
        mm_free(p);
    return 0;
}

