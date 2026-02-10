/*
 * MiniOS Page Fault Handler Header
 */

#ifndef _PAGEFAULT_H
#define _PAGEFAULT_H

/*
 * Initialize page fault handler
 * Must be called after ISR initialization
 */
void pagefault_init(void);

#endif /* _PAGEFAULT_H */

