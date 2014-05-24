Segregated Fits Memory Allocator
=================================

Memory allocator supporting malloc(), free() and realloc() operations. 

This was my final project for the Algorithm Engineering course held in 2009/2010 for the Bachelor in Computer Engineering at Sapienza University of Rome.

Implementation details
----------------------

- Segregated fits strategy: maintains separate explicit lists of free memory blocks of varying size classes
- Lookups within a free list can either employ the best-fit or the first-fit policy
- Operations of splitting and coalescing (using boundary tags) reduce fragmentation and improve memory usage performances

Report (in italian): https://drive.google.com/file/d/0B-iwhKMG4iF1VVEyZXhlOVd5UFE/edit?usp=sharing
