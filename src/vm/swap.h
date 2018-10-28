#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"
#include "threads/thread.h"
#include "devices/disk.h"
#include "vm/frame.h"

struct swap_table_entry
{
    struct list_elem elem;  // for swap_table (hash table)
    // uintptr_t* va;  // to connect easily with supplementary page table....
    struct thread* owner;
    uintptr_t* upage;
    // we use 2 disk in project3
    struct disk* swap_disk;  // need to create (allocate) new swap_disk <-> disk in project2
    disk_sector_t sec_no;
};

void swap_init(void);
void swap_in(struct thread *t);
void swap_out(struct frame_table_entry *fte);
void swap_free(struct swap_table_entry *ste);