/* rtfirewire/rtpkbuff/rtpkbuff.c
 * Generic Pakcet buffer Structure and interfaces for RT-FireWire
 *
 *  Copyright (C) 2005 Zhang Yuchen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
 
#include <linux/config.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <net/checksum.h>

#include <rtpkbuff.h>


static unsigned int global_rtpkbs    = DEFAULT_GLOBAL_RTPKBS;
static unsigned int rtpkb_cache_size = DEFAULT_RTPKB_CACHE_SIZE;
MODULE_PARM(global_rtpkbs, "i");
MODULE_PARM(rtpkb_cache_size, "i");
MODULE_PARM_DESC(global_rtpkbs, "Number of realtime socket buffers in global pool");
MODULE_PARM_DESC(rtpkb_cache_size, "Number of cached rtpkbs for creating pools in real-time");


/* Linux slab pool for rtpkbs */
static kmem_cache_t *rtpkb_slab_pool;

/**
 * @ingroup
 * @anchor rtpkb_cache
 */
/* preallocated rtpkbs for real-time pool creation */
static struct rtpkb_pool rtpkb_cache={
	.name = "rtpkb cache",
};

/* pool of rtpkbs for global use */
struct rtpkb_pool global_pool = {
	.name = "global pool",
};

/* pool statistics */
unsigned int rtpkb_pools=0;
unsigned int rtpkb_pools_max=0;
unsigned int rtpkb_amount=0;
unsigned int rtpkb_amount_max=0;

/**
 * list of memory pools
 */
LIST_HEAD(pool_list);

/**
 * this is to prevent new pool being created during bottomhalf
 * when the list is being accessed, like thru read_proc
 */
static rwlock_t pool_list_lock = RW_LOCK_UNLOCKED;

/**
 *  skb_over_panic - private function
 *  @pkb: buffer
 *  @sz: size
 *  @here: address
 *
 *  Out of line support code for rtpkb_put(). Not user callable.
 */
void rtpkb_over_panic(struct rtpkb *pkb, int sz, void *here)
{
    char *name= "";
    if ( pkb->dev_name )
        name=pkb->dev_name;
    else
        name="<NULL>";
    rtos_print("rtpkb: rtpkb_put :over: %p:%d put:%d dev:%s\n", here, pkb->len,
               sz, name);
}



/**
 *  skb_under_panic - private function
 *  @pkb: buffer
 *  @sz: size
 *  @here: address
 *
 *  Out of line support code for rtpkb_push(). Not user callable.
 */
void rtpkb_under_panic(struct rtpkb *pkb, int sz, void *here)
{
    char *name = "";
    if ( pkb->dev_name )
        name=pkb->dev_name;
    else
        name="<NULL>";

    rtos_print("rtpkb: rtpkb_push :under: %p:%d put:%d dev:%s\n", here,
               pkb->len, sz, name);
}

/**
 * @ingroup rtpkb management
 * @anchor alloc_rtpkb
 * dequeue a pkb from memory pool
 * @param size - the length of data required, here it is
 * only used to check it doesnt exceed the limit,i.e.buf_len. 
 */
struct rtpkb *alloc_rtpkb(unsigned int size,struct rtpkb_pool *pool)
{
	struct rtpkb *pkb;
	
	if(size!=0)
		RTOS_ASSERT(size <= SKB_DATA_ALIGN(RTPKB_SIZE), return NULL;);
	
	pkb = rtpkb_dequeue(&pool->queue);
	
	if (!pkb)
		return NULL;

	rtpkb_clean(pkb);

	return pkb;
}

/***
 *  kfree_rtpkb
 *  @pkb    rtpkb
 */
void kfree_rtpkb(struct rtpkb *pkb)
{
    RTOS_ASSERT(pkb != NULL, return;);
    RTOS_ASSERT(pkb->pool != NULL, return;);
	
    rtpkb_queue_tail(&pkb->pool->queue, pkb);
}

/***
 *  rtpkb_pool_init
 *  @pool: pool to be initialized
 *  @initial_size: number of rtpkbs to allocate
 *  return: number of actually allocated rtpkbs
 */
unsigned int rtpkb_pool_init(struct rtpkb_pool *pool,
                             unsigned int initial_size)
{
    unsigned int i;

    rtpkb_queue_head_init(&pool->queue);

    i = rtpkb_pool_extend(pool, initial_size);
	
   list_add_tail(&pool->entry, &pool_list);

    rtpkb_pools++;
    if (rtpkb_pools > rtpkb_pools_max)
        rtpkb_pools_max = rtpkb_pools;

    return i;
}

/***
 *  rtpkb_pool_init_rt
 *  @pool: pool to be initialized
 *  @initial_size: number of rtpkbs to allocate
 *  return: number of actually allocated rtpkbs
 */
unsigned int rtpkb_pool_init_rt(struct rtpkb_pool *pool,
                                unsigned int initial_size)
{
    unsigned int i;

    rtpkb_queue_head_init(&pool->queue);

    i = rtpkb_pool_extend_rt(pool, initial_size);
	
   list_add_tail(&pool->entry, &pool_list);

    rtpkb_pools++;
    if (rtpkb_pools > rtpkb_pools_max)
        rtpkb_pools_max = rtpkb_pools;

    return i;
}

/***
 *  __rtpkb_pool_release
 *  @pool: pool to release
 */
void __rtpkb_pool_release(struct rtpkb_pool *pool)
{
    struct rtpkb *pkb;


    while ((pkb = rtpkb_dequeue(&pool->queue)) != NULL) {
        kmem_cache_free(rtpkb_slab_pool, pkb);
        rtpkb_amount--;
    }
    
    list_del(&pool->entry);

    rtpkb_pools--;
}



/***
 *  __rtpkb_pool_release_rt
 *  @pool: pool to release
 */
void __rtpkb_pool_release_rt(struct rtpkb_pool *pool)
{
    struct rtpkb *pkb;


    while ((pkb = rtpkb_dequeue(&pool->queue)) != NULL) {
        rtpkb_queue_tail(&rtpkb_cache.queue, pkb);
        rtpkb_amount--;
    }
    
    list_del(&pool->entry);

    rtpkb_pools--;
}



unsigned int rtpkb_pool_extend(struct rtpkb_pool *pool,
                               unsigned int add_rtpkbs)
{
    unsigned int i;
    struct rtpkb *pkb;

    RTOS_ASSERT(pool != NULL, return -EINVAL;);

    for (i = 0; i < add_rtpkbs; i++) {
        /* get rtpkb from slab pool */
        if (!(pkb = kmem_cache_alloc(rtpkb_slab_pool, GFP_KERNEL))) {
            rtos_print(KERN_ERR "RTnet: rtpkb allocation from slab pool failed\n");
            break;
        }

        /* fill the header with zero */
        memset(pkb, 0, sizeof(struct rtpkb));

        pkb->pool = pool;
        pkb->buf_start = ((char *)pkb) + ALIGN_RTPKB_STRUCT_LEN;

        rtpkb_queue_tail(&pool->queue, pkb);

        rtpkb_amount++;
        if (rtpkb_amount > rtpkb_amount_max)
            rtpkb_amount_max = rtpkb_amount;
    }

    pool->capc += i;

    return i;
}



unsigned int rtpkb_pool_extend_rt(struct rtpkb_pool *pool,
                                  unsigned int add_rtpkbs)
{
    unsigned int i;
    struct rtpkb *pkb;

    RTOS_ASSERT(pool != NULL, return -EINVAL;);

    for (i = 0; i < add_rtpkbs; i++) {
        /* get rtpkb from rtpkb cache */
        if (!(pkb = rtpkb_dequeue(&rtpkb_cache.queue))) {
            rtos_print("rtpkb: rtpkb allocation from real-time cache "
                       "failed\n");
            break;
        }

        pkb->pool = pool;

        rtpkb_queue_tail(&pool->queue, pkb);

        rtpkb_amount++;
        if (rtpkb_amount > rtpkb_amount_max)
            rtpkb_amount_max = rtpkb_amount;
    }
    
    pool->capc += i;

    return i;
}



unsigned int rtpkb_pool_shrink(struct rtpkb_pool *pool,
                               unsigned int rem_rtpkbs)
{
    unsigned int i;
    struct rtpkb *pkb;

    for (i = 0; i < rem_rtpkbs; i++) {
        if ((pkb = rtpkb_dequeue(&pool->queue)) == NULL)
            break;

        kmem_cache_free(rtpkb_slab_pool, pkb);
        rtpkb_amount--;
    }
    
    pool->capc -= i;

    return i;
}



unsigned int rtpkb_pool_shrink_rt(struct rtpkb_pool *pool,
                                  unsigned int rem_rtpkbs)
{
    unsigned int i;
    struct rtpkb *pkb;

    for (i = 0; i < rem_rtpkbs; i++) {
        if ((pkb = rtpkb_dequeue(&pool->queue)) == NULL)
            break;
	
        rtpkb_queue_tail(&rtpkb_cache.queue, pkb);
        rtpkb_amount--;
    }

    pool->capc -= i;
    
    return i;
}


int rtpkb_acquire(struct rtpkb *rtpkb, struct rtpkb_pool *comp_pool)
{
    struct rtpkb *comp_rtpkb;

    comp_rtpkb = alloc_rtpkb(0, comp_pool);

    if (!comp_rtpkb)
        return -ENOMEM;

    comp_rtpkb->pool = rtpkb->pool;//do the hack here
    kfree_rtpkb(comp_rtpkb);

    rtpkb->pool = comp_pool;

    return 0;
}

#define PUTF(fmt, args...)				\
do {							\
	len += sprintf(page + len, fmt, ## args);	\
	pos = begin + len;				\
	if (pos < off) {				\
		len = 0;				\
		begin = pos;				\
	}						\
	if (pos > off + count)				\
		goto done_proc;				\
} while (0)

static int rtpkb_read_proc(char *page, char **start, off_t off, int count,
					int *eof, void *data)
{
	struct rtpkb_pool *pool;
	
	struct list_head *lh;
	off_t begin=0, pos=0;
	int len=0;
	
	unsigned int rtpkb_len = ALIGN_RTPKB_STRUCT_LEN + SKB_DATA_ALIGN(RTPKB_SIZE);
	
	read_lock_bh(&pool_list_lock);
	
	PUTF("Statistics\t\tCurrent\tMaximum\n"
					"rtpkb pools\t\t%d\t%d\n"
					"rtpkbs\t\t%d\t%d\n"
					"rtpkb memory needed\t\t\%d\t%d\n",
					rtpkb_pools, rtpkb_pools_max,
					rtpkb_amount, rtpkb_amount_max,
					rtpkb_amount * rtpkb_len, rtpkb_amount_max * rtpkb_len);
	
	PUTF("Pools\t\tCurrent Balance\tMaximum Balance\n");

	list_for_each(lh, &pool_list) {
		pool = list_entry(lh, struct rtpkb_pool, entry);
		PUTF("%s\t\t%d\t%d\n", pool->name, pool->queue.qlen, pool->capc);
	}
done_proc:
	read_unlock_bh(&pool_list_lock);

	*start = page + (off - begin);
	len -= (off - begin);
	if (len > count)
		len = count;
	else {
		*eof = 1;
		if (len <= 0)
			return 0;
	}

	return len;
}
#undef PUTF
 
int  rtpkb_pools_init(void)
{
	struct proc_dir_entry *proc_entry;
	
	rtpkb_slab_pool = kmem_cache_create("rtpkb_slab_pool",
		ALIGN_RTPKB_STRUCT_LEN + SKB_DATA_ALIGN(RTPKB_SIZE),
		0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (rtpkb_slab_pool == NULL)
		return -ENOMEM;
	
	proc_entry = create_proc_entry("rtpkb", S_IFREG | S_IRUGO | S_IWUSR, 0);
	if(!proc_entry) {
		printk("rtpkb:failed to create proc entry!\n");
		goto err_out1;
	}
	proc_entry->read_proc = rtpkb_read_proc;
	
	/* reset the statistics (cache is accounted separately) */
	rtpkb_pools      = 0;
	rtpkb_pools_max  = 0;
	rtpkb_amount     = 0;
	rtpkb_amount_max = 0;
	
	/* create the rtpkb cache like a normal pool */
	if (rtpkb_pool_init(&rtpkb_cache, rtpkb_cache_size) < rtpkb_cache_size)
		goto err_out2;

	/* create the global rtpkb pool */
	if (rtpkb_pool_init(&global_pool, global_rtpkbs) < global_rtpkbs)
		goto err_out3;
	
	printk("rtpkb:Real-Time Socket Buffer Module Initialized!\n");

	return 0;

err_out3:
	rtpkb_pool_release(&global_pool);
	rtpkb_pool_release(&rtpkb_cache);
err_out2:
	remove_proc_entry("rtpkb", 0);

err_out1:
	kmem_cache_destroy(rtpkb_slab_pool);

	return -ENOMEM;
}

void rtpkb_pools_release(void)
{
    struct list_head *lh;
    struct rtpkb_pool *pool;

    rtpkb_pool_release(&global_pool);
    rtpkb_pool_release(&rtpkb_cache);
    
    list_for_each(lh, &pool_list){
	    pool = list_entry(lh, struct rtpkb_pool, entry);
	    printk("rtpkb: Memory Pool %s is still in use!!!\n", pool->name);
    }

    remove_proc_entry("rtpkb",0);

    if (kmem_cache_destroy(rtpkb_slab_pool) != 0)
        printk(KERN_CRIT "rtpkb: memory leakage detected "
               "- reboot required!\n");
    else
	    printk("rtpkb: Real-Time Socket Buffer Module unloaded\n");
}

module_init(rtpkb_pools_init);
module_exit(rtpkb_pools_release);

MODULE_LICENSE("GPL");