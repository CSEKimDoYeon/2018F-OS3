#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "vm/page.h"
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"

unsigned
sup_page_hash_func(const struct hash_elem *e, void *aux)
{
    struct sup_page_table_entry *spte = hash_entry (e, struct sup_page_table_entry, elem);
    return hash_int ((int) spte->va);
}

bool
sup_page_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
    struct sup_page_table_entry *a_entry = hash_entry(a, struct sup_page_table_entry, elem);
    struct sup_page_table_entry *b_entry = hash_entry(b, struct sup_page_table_entry, elem);
    return a_entry->va < b_entry->va;
}

struct hash*
sup_page_init()
{
    struct hash *sup_page_table = malloc(sizeof(struct hash));
    // 굳이 alloc해야 하나..?
    // thread마다 하나씩 박을거면 -> thread처음 생길때 hash_init하면...
    // 전역으로 하려면 전역변수 선언 방식으로 해야할듯....
    // sup_page_table = palloc_get_page (PAL_USER);
    // sup_page_table = malloc(sizeof(struct hash));
    // if (sup_page_table == NULL) {
    //     // TOCHECK : what to do....?
    //     PANIC ("palloc_get: out of pages");
    // }
    // hash_init (struct hash *h, hash_hash_func *hash, hash_less_func *less, void *aux)
    hash_init (sup_page_table, sup_page_hash_func, sup_page_less_func, NULL);
    return sup_page_table;
}

struct sup_page_table_entry*
sup_page_entry_allocate(uintptr_t* va, uintptr_t *pa) // parameter 아직 작성안함
{
    // thread마다 하나씩 있는 sup_page라 frame_lock처럼 lock을 만들지는 못할듯..
    struct sup_page_table_entry *spte = malloc(sizeof(struct sup_page_table_entry));
    if (spte == NULL) {
        // TOCHECK : what to do...?
    }
    spte->va = va;
    spte->pa = pa;
    spte->owner = thread_current();
    spte->in_frame = true;
    // struct hash_elem *hash_insert (struct hash *, struct hash_elem *);
    // Inserts NEW into hash table H and returns a null pointer, if
    // no equal element is already in the table.
    if (hash_insert(thread_current()->sup_page_table, &spte->elem) != NULL) {
        // TOCHECK : right.....?
        PANIC("sup_page_table_entry is already inserted in hash table...");
    }
    return spte;
}

void
sup_page_entry_free(uintptr_t *pa)
{
    // hash_delete (struct hash *h, struct hash_elem *e)
    struct hash_elem *e;
    struct hash *sup_page_table = thread_current()->sup_page_table;
    struct sup_page_table_entry *temp = malloc(sizeof(struct sup_page_table_entry));
    if (temp == NULL) {
        // TOCHECK : what to do...?
    }
    temp->pa = pa;
    e = hash_find(sup_page_table, &temp->elem);
    if (e != NULL) {
        hash_delete(sup_page_table, &e);
        free(temp);
    }
    // TOCHECK : if there is no such spte....??
    // frame도 그렇고 뭔가 NULL이나 false를 return하게 만들어야 할듯...
}


void
sup_page_action_func (struct hash_elem *e, void *aux)
{
    struct sup_page_table_entry *spte = hash_entry(e, struct sup_page_table_entry, elem);

    // free the frame
    if (spte->in_frame) {
        // frame free를 시킵니다.
        frame_entry_free(spte->pa);
    }
    else {
        // swap free를 시킵니다.
        // swap_free(spte->st);
    }
    free(spte);
}

void
sup_page_table_free (struct hash *sup_page_table) {
    ASSERT (sup_page_table != NULL);
    hash_destroy (&sup_page_table, sup_page_action_func);
    free(&sup_page_table);
}