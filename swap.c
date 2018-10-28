#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "vm/swap.h"
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"

static struct list swap_table;
static struct lock swap_lock;
static struct bitmap *swap_bitmap;

void
swap_init(void)
{
    list_init (&swap_table);
    lock_init (&swap_lock);

    // disk_init() : init.c에서 이미 한번 call해서 더 init안해줘도 될듯.. 이때 모든 disk다 생성됨
    // sec_no를 위해 bitmap이 필요한듯......
    struct disk *swap_disk = disk_get(1, 1);
    disk_sector_t swap_disk_size = disk_size (swap_disk);
    // first: bitmap all set to 'false'
    swap_bitmap = bitmap_create(swap_disk_size);
}

// page_fault()나, sup_page_table check 함수에서 call 'swap_in()'
// swap_in : swap_disk(table)에서 관련 slot을 뽑아 다시 frame_table에 넣어주는 일
void
swap_in(struct thread *t)
{
    lock_acquire(&swap_lock);
    // swap_table을 보기....
    struct list_elem *e;
    struct disk *swap_disk = disk_get(1, 1);
    int i;

    for (e = list_begin (&swap_table); e != list_end (&swap_table); e = list_next(e)) {
        struct swap_table_entry *ste = list_entry(e, struct swap_table_entry, elem);
        if (ste->owner == t) {
            // put into frame_table / erase from swap_table
            struct frame_table_entry *fte = frame_entry_allocate(ste->upage);
            // TOCHECK : if, fte == NULL, what to do...?

            // disk_read한 것을 다시 frame(disk section)에 적어야됌 ('memcpy' 사용하면 될 듯)
            for (i=0; i<8; i++) {
                void *buffer = malloc(512);
                disk_read(swap_disk, ste->sec_no, buffer);
                memcpy(fte->frame, buffer, strlen(buffer));
            }
            // swap_free : free swap_table_entry + change swap_bit_map(sec_no part)
            bitmap_set_multiple (swap_bitmap, ste->sec_no, 8, false);
            swap_free(ste);
        }
    }

}

// swap_out : frame_table_entry의 하나를 골라서 swap_disk에 넣어주는 일
// TO THINK : swap out되었다는 사실을 sup_page_table에서 아는 방법이 있나...? 정보를 어떻게 저장해야 할까..?
void
swap_out(struct frame_table_entry *fte)
{
    lock_acquire(&swap_lock);
    struct swap_table_entry *ste = malloc(sizeof(struct swap_table_entry));
    // TOCHECk: correct use...?
    struct disk *swap_disk = disk_get(1, 1);
    int i;

    if (swap_disk == NULL) {
        // TOCHECK : is_ATA가 false인 상황이 뭔지 잘 이해가 안되서... NULL이 의미하는게 뭐죠......??? (what to do..)
        lock_release(&swap_lock);
    }

    ste->owner = thread_current();
    ste->swap_disk = swap_disk;
    ste->upage = fte->upage;
    // need to find 8 consecutive empty disk space!
    ste->sec_no = bitmap_scan_and_flip (swap_bitmap, 0, 8, false);
    list_push_back(&swap_table, &ste->elem);

    uintptr_t *pa = fte->frame;
    disk_sector_t temp_sec = ste->sec_no;

    // (4096/512) = 8 slots need for one frame(page)
    for (i=0; i<8; i++) {
        pa = pa + (512 * i);
        // pa == buffer 개념!  
        disk_write(swap_disk, temp_sec, pa);
        temp_sec++;
    }
    lock_release(&swap_lock);
}

// 언제 swap_free를 부르죠....?음...
// swap_in만 제대로 잘되면 부를일이 없을 것 같아보이기도 한데...
void
swap_free(struct swap_table_entry *ste)
{
    lock_acquire(&swap_lock);
    list_remove(&ste->elem);
    free(ste);
    lock_release(&swap_lock);
}