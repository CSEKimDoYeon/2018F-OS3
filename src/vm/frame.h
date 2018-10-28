#include <debug.h>
#include <list.h>
#include <stdint.h>
#include <lib/kernel/list.h>

struct frame_table_entry
{
    uint32_t* frame;
    struct list_elem elem;  // for frame_table list
    // process.c/load_segment() -> upage 변수로 받아오기 가능
    uintptr_t* upage;  // upage == page_number..
    // struct sup_page_table_entry* spte
    struct thread* owner;  // let's just not think about SHARE now....
    int eviction;
};

void frame_init(void);
struct frame_table_entry *frame_entry_allocate(uintptr_t* upage);
// TOCHECK : input parameter..?
void frame_entry_free(uintptr_t *kpage);
void evict(void);