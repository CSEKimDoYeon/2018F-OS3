#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
// added includes
// from threads
#include "threads/init.h"
#include "threads/synch.h" // for read and write
#include "threads/vaddr.h" // for PHYS_BASE
// from userprog
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
// from filesys
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/stdio.h"

#include "lib/string.h"

#include <stdlib.h>
// #include <syscall.h>

static void syscall_handler (struct intr_frame *);
int get_val (struct intr_frame *f);
bool valid_address (void *address);
void invalid_terminate(void);

// defining variables
static struct lock flock; // file lock

// TOCHECK : syscall_init을 call하는 곳이 없는데 어떻게 실행되는거지.....?
void
syscall_init (void) 
{
  lock_init(&flock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// check valid_address of f->esp
// TIP: is_user_vaddr 이전에 pagedir_get_page먼저 call하니 
// ASSERT (is_user_vaddr (uaddr)) 때문에 kernel panic 발생
// 순서 바꿔서 해결!
bool
valid_address (void *address)
{
  if (address == NULL ||
    !is_user_vaddr (address) ||
    (pagedir_get_page (thread_current()->pagedir, address) == NULL)
    )
    return false;
  return true;
}

// All the invalide types of invalid pointers must be rejected without harm to the kernel or other running processes
// by terminating the offending process and freeing its resources
void
invalid_terminate(void)
{
  struct thread *t = thread_current();
  struct PCB *pcb = t->t2pcb;

  pcb->exit_status = -1;
  thread_exit();
}

// normal terminate
void
terminate(void)
{
  thread_exit();
}

// use for get current *esp value
int
get_val (struct intr_frame *f)
{
  if( !valid_address (f->esp) ){
    invalid_terminate();
    return -1;
  }
  int tmp = *(int *)f->esp;
  f->esp += 4;
  return tmp;
}

struct filePCB *
fd2file (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&t->t2pcb->file_list); e != list_end (&t->t2pcb->file_list);
       e = list_next (e)) {
         struct filePCB *fPCB = list_entry (e, struct filePCB, file_elem);
         if (fPCB->fd == fd)
          return fPCB;
       }
  return NULL;
}

int
file2fd (struct file *f)
{
  struct thread *t = thread_current();
  struct filePCB *fPCB = malloc(sizeof(struct filePCB));
  if (fPCB == NULL) {
    free(fPCB);
    invalid_terminate();
  }

  fPCB->file = f;
  fPCB->fd = t->t2pcb->fd;
  t->t2pcb->fd++;
  list_push_back(&t->t2pcb->file_list, &fPCB->file_elem);
  return fPCB->fd;
}

int open (const char *file)
{
  lock_acquire(&flock);

  if (!valid_address(file)) {
    lock_release(&flock);
    invalid_terminate();
    return -1;
  }

  struct file *openfile = filesys_open(file);

  if (openfile == NULL) {
    lock_release(&flock);
    return -1;              
  }
  int fd = file2fd(openfile);
  lock_release(&flock);
  return fd;
}


int write (int fd, const void* buffer, unsigned size)
{
  struct filePCB *fPCB;
  lock_acquire (&flock);

  if ( fd == -1 || !valid_address(buffer) ) {
    lock_release (&flock);
    invalid_terminate();
    return 0;
  }

  // STDOUT_FILENO==1 is from stdio.h
  if (fd==STDOUT_FILENO) {
    putbuf ((const char *)buffer, size); // putbuf in console.c
    lock_release (&flock);
    // TOCHECK: 고려안함 - as long as size is not bigger than a few hundred bytes
    return size;
  }

  fPCB = fd2file(fd);
  // fd 가 1이 아닌 경우 + fd를 통해 fPCB가 찾기지 않을 경우 (fd값이 제대로 주어지지 경우)
  if (fPCB == NULL) {
    lock_release (&flock);
    return 0;   //no bytes could be written at all
  }

  size = file_write (fPCB->file, buffer, size);
  lock_release (&flock);
  return size;
}

void seek (int fd, unsigned position)
{
  lock_acquire(&flock);

  struct filePCB *fPCB = fd2file(fd);
  if (fPCB == NULL) {
    lock_release (&flock);
    return ;
  }
  file_seek(fPCB->file, position);
  lock_release(&flock);
}

unsigned tell (int fd)
{
  lock_acquire(&flock);

  struct filePCB *fPCB = fd2file(fd);
  if (fPCB == NULL) {
    lock_release (&flock);
    return -1;
  }
  unsigned position = file_tell(fPCB->file);
  lock_release(&flock);
  return position;
}

void close (int fd)
{
  lock_acquire(&flock);
  struct thread *t = thread_current();

  if (list_size(&t->t2pcb->file_list)==0) {
    lock_release(&flock);
    return ;
  }
  struct filePCB *fPCB = fd2file(fd);

  if (fPCB == NULL) {
    lock_release(&flock);
    return ;
  }

  file_close(fPCB->file);
  list_remove(&fPCB->file_elem);
  free(fPCB);

  lock_release(&flock);
}

void dereference (int sys_code, struct intr_frame *f UNUSED)
{
  switch (sys_code)
  {
    case SYS_HALT: {
      // from "threads/init.h"
      power_off();
      break;
    }
    case SYS_EXIT: {
      int status = get_val(f);
      struct thread *t = thread_current();
      struct PCB *pcb = t->t2pcb;
      pcb->exit_status = status;
      pcb->called_wait = false;

      terminate();
      break;
    }
    case SYS_EXEC: {
      /* exec_sema 사용 */
      const char *cmd_line = (const char*)get_val(f);

      // TOCHECK : 근데 get_val에서 invalid_address를 체크하면서 invalid시 바로 thread_exit()을 하는데
      // 이 파트까지 올수가 있나 코드가...?
      // cmd_line의 경우 pointer -> *(f->esp)값도 정상적인 주소값을 포함하고 있는지 체크해주어야함!
      if (!valid_address(cmd_line)) {
        f->eax = -1;
        invalid_terminate();
        break;
      }
      // cmd_line <- file_name
      tid_t child_pid = process_execute(cmd_line);

      if (child_pid == TID_ERROR) {
        f->eax = -1;
        break;
      }

      struct PCB *child_pcb = pcb_from_tid(child_pid);
      if (child_pcb == NULL) {
        f->eax = -1;
        break;
      }
      sema_down(&child_pcb->exec_sema);
      // thread_current() 가 현재 SYS_EXEC실행중 -> process_execute()통해 child_thread(process) 만드는중
      // 즉, 현재 thread_current() = child_t의 parent_thread로 볼 수 있음
      struct thread *parent_t = thread_current();
      
      struct PCB *parent_pcb = parent_t->t2pcb;
      
      if (!child_pcb->executed){
        list_remove(&child_pcb->elem);
        free(child_pcb);
        f->eax = -1;
      }
      else {
      	child_pcb->parent_tid = parent_t->tid;
      	list_push_back (&parent_pcb->child_list, &child_pcb->child_elem);
      	f->eax = child_pid;
      }
      break;
    }
    case SYS_WAIT: {
      // pid = child's pid(tid)
      pid_t pid = (pid_t)get_val(f);

      struct PCB *child_pcb = pcb_from_tid(pid);
      struct thread *child_t = child_pcb->pcb2t;

      // thread_current가 child_thread를 기다리기 위해 wait()실행하려는 parent_thread
      struct thread *parent_t = thread_current();

      // (1) pid is not alive -> TOCHECK : child_pcb == NULL correct
      // (2) pid does not refer to direct child of the calling process
      // (3) process that calls 'wait' has already called 'wait' on pid -> called_wait가 true면 return -1!
      // TODO : (4) : pid did not call exit(), but was terminated by the kernel
      // return -1 immediately!
      if ( child_pcb == NULL || child_pcb->called_wait || child_pcb->parent_tid != parent_t->tid ) {
	      f->eax = -1;
        break;
      }
      
      child_pcb->called_wait = true;
      // process_wait : return pcb(pcb from given tid(pid))->exit_status
      f->eax = process_wait(pid);
      // return이 됬다는 건 -> sema_down을 잘 탈출했다는 것 ->wait가 끝났다는 뜻
      // child_thread실행이 exit로 끝났다는 것을 의미... -> exit에서 (called_wait=false)를 하자
      // parent_pcb->child_list에 있는 child_pcb->child_elem을 지우기
      // remove child_list
      list_remove(&child_pcb->child_elem);
      // remove pcb_list
      list_remove(&child_pcb->elem);

      free(child_pcb);
      break;
    }
    case SYS_CREATE: {
      const char *file = (char *)get_val(f);
      unsigned initial_size = (unsigned)get_val(f);

      if (!valid_address(file)) {
        f->eax = 0;  //meaning: false
        invalid_terminate();
        break;
      }
      
      lock_acquire(&flock); 
      f->eax = filesys_create(file, initial_size);
      lock_release(&flock);
      break;
    }
    case SYS_REMOVE: {
      const char *file = (const char *)get_val(f);

      if (!valid_address(file)) {
        f->eax = 0;   //meaning: false
        invalid_terminate();
        break;
      }
      lock_acquire(&flock);
      f->eax = filesys_remove(file);
      lock_release(&flock);
      break;
    }
    /*
    case SYS_OPEN: {
      const char *file = (const char *)get_val(f);
      f->eax = open(file);
      break;
    }*/
    case SYS_OPEN: {
      const char *file = (const char*)get_val(f);
      lock_acquire(&flock);
      
      if (!valid_address(file)) {
        f->eax = -1;
        lock_release(&flock);
        invalid_terminate();
        break;
      }

      struct file *openfile = filesys_open(file);

      if (openfile == NULL) {
        f->eax = -1;
        lock_release(&flock);
        break;
      }
      int fd = file2fd(openfile);
      f->eax = fd;
      lock_release(&flock);
      break;

    }
    case SYS_FILESIZE: {
      int fd = get_val(f);
      struct filePCB *fPCB = fd2file(fd);
      // fd->file을 못찾았을때, return '0'
      if (fPCB == NULL) {
        f->eax = 0;
        break;
      }
      f->eax = file_length(fPCB->file);
      break;
    }
    case SYS_READ: {
      int fd = get_val(f);
      void *buffer = (void *)get_val(f);
      unsigned size = (unsigned)get_val(f);

      int read_size = 0;
      uint8_t keyboardinput = 0;

      lock_acquire (&flock);

      if (!valid_address(buffer)) {
        f->eax = -1;
        lock_release (&flock);
        invalid_terminate();
        break;
      }

      struct filePCB *fPCB = fd2file(fd);

      if (fd == STDIN_FILENO) {
        // input_getc() : uint8_t == unsigned 'char' type return
        // buffer에 input_getc()로 받은 string을 넣어주기 (memcpy 사용하면 될듯)
        while (keyboardinput != "/0") {
          keyboardinput = input_getc();
          memcpy(buffer, &keyboardinput, 1);
          buffer++; // +1 byte
          read_size++;          
        }
      }
      else {
        if (fPCB == NULL) {
          f->eax = -1;
          lock_release(&flock);
          break;
        }
        read_size = file_read(fPCB->file, buffer, size);
      }
      f->eax = read_size;
      lock_release(&flock);

      break;
    }
    
    case SYS_WRITE: {
      // fd = file descriptor
      int fd = get_val(f);
      const void *buffer = (const void *)get_val(f);
      unsigned size = (unsigned)get_val(f);

      f->eax = write(fd, buffer, size);
      break;
    }
    case SYS_SEEK: {
      int fd = get_val(f);
      unsigned position = (unsigned)get_val(f);
      seek(fd, position);
      break;
    }
    case SYS_TELL: {
      int fd = get_val(f);
      f->eax = tell(fd);
      break;
    }
    case SYS_CLOSE: {
      int fd = get_val(f);
      close(fd);
      break;
    }
    default:
      terminate();
      break;
  }
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int sys_code = get_val(f);

  dereference (sys_code, f);
}
