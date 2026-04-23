#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);

// helper to safely read a 32 bit value from user memory
// returns the value or kills the process if the pointer is bad
static uint32_t
copy_in_u32 (const void *uaddr)
{
  //check if null
  if (uaddr == NULL)
    {
      thread_current()->exit_status = -1;
      thread_exit();
    }

  // check all 4 bytes are in user space
  if (!is_user_vaddr(uaddr) || !is_user_vaddr((const char *)uaddr + 3))
    {
      thread_current()->exit_status = -1;
      thread_exit();
    }

  // check that the page containing the start address is mapped
  if (pagedir_get_page(thread_current()->pagedir, uaddr) == NULL)
    {
      thread_current()->exit_status = -1;
      thread_exit();
    }

  // check that the page containing the last byte is also mapped
  // this handles the case where the value straddles a page boundary
  if (pagedir_get_page(thread_current()->pagedir, (const char *)uaddr + 3) == NULL)
    {
      thread_current()->exit_status = -1;
      thread_exit();
    }

  return *(uint32_t *)uaddr; // safe to read now
}

// helper to validate that an entire buffer range is in user space
// kills the process if any part of the buffer is invalid
static void
validate_user_range (const void *start, size_t size)
{
  const char *ptr = start;
  size_t i;

  if (start == NULL) // null pointer is always bad
    {
      thread_current()->exit_status = -1;
      thread_exit();
    }

  // check every byte of the buffer is in user space
  for (i = 0; i < size; i++)
    {
      if (!is_user_vaddr(ptr + i)) // if any byte is in kernel space, kill it
        {
          thread_current()->exit_status = -1;
          thread_exit();
        }
    }
}

// write system call implementation
// only handles writing to stdout (fd = 1) for now
int
syscall_write (int fd, const void *buffer, unsigned size)
{
  if (fd != 1) // only support stdout for now
    return -1;

  putbuf(buffer, size); // write the buffer to the console
  return size;
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  // read the syscall number from the top of the user stack
  uint32_t syscall_num = copy_in_u32(f->esp);

  // dispatch to the correct syscall based on the number
  switch (syscall_num)
    {
      case SYS_HALT:
        shutdown_power_off(); // shut down the machine immediately
        break;

      case SYS_EXIT:
        {
          // read the exit status argument from the stack
          int status = (int) copy_in_u32(f->esp + 4);
          thread_current()->exit_status = status; // save the exit status
          thread_exit(); // exit the process
          break;
        }

      case SYS_WRITE:
        {
          // read all three arguments from the stack
          int fd = (int) copy_in_u32(f->esp + 4);
          const void *buffer = (const void *) copy_in_u32(f->esp + 8);
          unsigned size = (unsigned) copy_in_u32(f->esp + 12);

          validate_user_range(buffer, size); // make sure buffer is valid
          f->eax = syscall_write(fd, buffer, size); // store return value in eax
          break;
        }

      default:
        // unknown syscall, kill the process
        thread_current()->exit_status = -1;
        thread_exit();
        break;
    }
}