#include <list.h>
#include <stdio.h>
#include "vm/frame.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/malloc.h"

static struct list frame_table;
static struct lock frame_lock;

void
frame_init(void)
{
    list_init (&frame_table);
    lock_init (&frame_lock);
}

struct frame_table_entry *
frame_entry_allocate(uintptr_t* upage)
{
    lock_acquire(&frame_lock);
    uint8_t *kpage = palloc_get_page (PAL_USER);

    if (kpage == NULL) {
        lock_release(&frame_lock);
        // PANIC ("kernel panic made by us");        
        // what to do...? => need to call eviction()
        // evict();
        kpage = palloc_get_page (PAL_USER);
        // if palloc_get_page에서 또 null 이 안나오겠죠...? 그럼요 eviction 잘짜면 그런일은 없을거에요 ...
    }
    struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
    // TOCHECK : possible case...?
    if (fte == NULL) {
        // TOCHECK : what to do..?
        return NULL;
    }
    
    fte->frame = kpage;
    fte->upage = upage;
    fte->owner = thread_current();
    fte->eviction = 0; // make true(1), if entry need to be evicted
    list_push_back(&frame_table, &fte->elem);
    lock_release(&frame_lock);
    return fte;
}

void
frame_entry_free (uintptr_t *kpage)
{
    lock_acquire(&frame_lock);
    // ASSERT (is_kernel_vaddr(kpage));

    struct list_elem *e;
    for (e = list_begin (&frame_table); e != list_end (&frame_table);
        e = list_next (e)) {
            struct frame_table_entry *fte = list_entry (e, struct frame_table_entry, elem);
            if (fte->frame == kpage) {
                list_remove(&fte->elem);
                palloc_free_page(fte->frame);
                free(fte);
            }
    }
    // TOCHECK : how if there is no such fte..?
    
    lock_release(&frame_lock);
}

void
evict(void)
{
    lock_acquire(&frame_lock);
    
    //choose a frame(page) to evict when no frames are free (when disk is full)
    if (!list_empty(&frame_table)) {
        struct frame_table_entry *fte = list_entry (list_pop_front (&frame_table), struct frame_table_entry, elem);
        
        // uintptr_t *kpage = fte -> frame;
        // in swap_out(): write victim page in swap-disk => need to implement disk.c!
        swap_out(fte);
    }
    // TOCHECK : think frame_table will not be empty when evict() is called!(disk is full now)
    lock_release(&frame_lock);
}