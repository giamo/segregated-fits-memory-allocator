#include <stdio.h>

extern int mm_init (void);
extern void *mm_malloc (size_t size);
extern void mm_free (void *ptr);
extern void *mm_realloc(void *ptr, size_t size);
extern void split(void *ptr, size_t newsize);
extern void *coalesce(void *addr);
extern void *search_free_list(int class, size_t size);
extern void remove_from_free_list(int class, void *ptr);
extern void put_on_front_of_class_list(int class, void *ptr);
extern int simulate_right_coalesce(void *ptr, size_t diff);
extern void mm_check();
