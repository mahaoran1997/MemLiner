
#ifndef __LINUX_SWAP_SWAP_GLOBAL_STRUCT_MEM_LAYER_H
#define __LINUX_SWAP_SWAP_GLOBAL_STRUCT_MEM_LAYER_H

#include <linux/swap_global_struct.h>


//
// Global variables 
//



// page status 
enum page_stat{
  MAPPED   = 0,
  UNMAPPED = 1,
  SWAPPED  = 2,
};


// [TODO] Actually, we can store the present of not information
// into the page_stat and let the runtime query this information
// In this case, the epoch is useless?
// [TODO] Record the process ID. Right now only the data of
// single process can be swapped out.
//
//  Structure of the epoch_struct	
//  |--4 bytes for eppch --|-- 4 bytes for legnth --|-- unsigned char array --|
struct epoch_struct{
  unsigned int epoch;   // the first 32 bits for epoch recording
  unsigned int length;  // length of the page_stats
  //unsigned int page_stats[COVERED_MEM_LENGTH];  // the epoch value for each page
  unsigned char page_stats[];
};

// defined in extended_syscall.c
extern struct epoch_struct *user_kernel_shared_data;


void intialize_epoch_struct(struct epoch_struct* cur_epoch, unsigned long byte_size);
unsigned long virt_addr_to_page_stat_offset(unsigned long virt_addr);
void mark_page_stat(unsigned long user_virt_addr, enum page_stat state);


//
// Functions
//


enum check_mode{
  CHECK_FLUSH_MOD   = 1,
  CHECK_SWAP_SYSTEM = 2,
  DEFAULT,
};

void print_page_flags(struct page *page, enum check_mode print_mode, const char* message);
void print_virt_addr_of_page(struct page *page, const char* message);
void print_swap_info_struct(swp_entry_t entry, const char* message);

swp_entry_t walk_page_table_for_swap_entry(struct mm_struct * mm, unsigned long user_virt_addr );  // ONLY walk the page table of user process. Return the pte copied pte value or 0 for empty page table.

// declared as a funtion pointer??
//extern void (*mmap_user_kernel_shared_mem)(unsigned long, unsigned long);


#endif // __LINUX_SWAP_SWAP_GLOBAL_STRUCT_MEM_LAYER_H