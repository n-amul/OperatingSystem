#ifndef _MMAP_H_
#define _MMAP_H_

#define MAP_PRIVATE 0x0001
#define MAP_SHARED 0x0002
#define MAP_POPULATE 0x0004
#define MAP_ANONYMOUS 0x0005
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FIXED 0x0008
#define MAP_GROWSUP 0x0010

#define MAX_UPAGE_INFO 32

struct pgdirinfo {
  int n_upages;              // Number of user pages
  uint va[MAX_UPAGE_INFO];   // Array to store first 32 virtual addresses
  uint pa[MAX_UPAGE_INFO];   // Array to store corresponding physical addresses
};
#define MAX_WMAP_INFO 32

struct wmapinfo {
  int total_mmaps;              // Total number of memory maps
  uint addr[MAX_WMAP_INFO];     // Array to store addresses of memory maps
  uint length[MAX_WMAP_INFO];   // Array to store lengths of memory maps
  int n_loaded_pages[MAX_WMAP_INFO];  // Array to store the number of loaded pages for each memory map
};

int wmap(uint addr, int length, int flags);
int wunmap(uint addr);
int getpgdirinfo(struct pgdirinfo *pdinfo);
int getwmapinfo(struct wmapinfo *wminfo);



#endif