#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "lib/kernel/hash.h"
#include "threads/thread.h"

struct sup_page_table_entry
{
    struct hash_elem elem;  // for sup_page_table (hash table)
    uintptr_t* va;  // virtual address
    uintptr_t* pa;  // physical address
    struct thread* owner;
    struct swap_table* st;
    bool in_frame;
};

struct hash* sup_page_init();
struct sup_page_table_entry *sup_entry_allocate(void);
void sup_entry_free(void);