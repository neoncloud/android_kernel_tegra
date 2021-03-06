#ifndef _LINUX_LOW_MEM_NOTIFY_H
#define _LINUX_LOW_MEM_NOTIFY_H

#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/swap.h>

/* We support up to this many different thresholds. */
#define LOW_MEM_THRESHOLD_MAX 5

extern unsigned long low_mem_thresholds[];
extern unsigned int low_mem_threshold_count;
extern unsigned int low_mem_threshold_last;
void low_mem_notify(void);
extern const struct file_operations low_mem_notify_fops;
extern bool low_mem_margin_enabled;
extern unsigned long low_mem_lowest_seen_anon_mem;
extern const unsigned long low_mem_anon_mem_delta;
extern unsigned int low_mem_ram_vs_swap_weight;

#ifdef CONFIG_SYSFS
extern void low_mem_threshold_notify(void);
#else
static inline void low_mem_threshold_notify(void) { }
#endif

/*
 * Compute available memory used by files that can be reclaimed quickly.
 */
static inline unsigned long get_available_file_mem(int lru_base)
{
	unsigned long file_mem =
			global_page_state(lru_base + LRU_ACTIVE_FILE) +
			global_page_state(lru_base + LRU_INACTIVE_FILE);
	unsigned long dirty_mem = global_page_state(NR_FILE_DIRTY);
	unsigned long min_file_mem = min_filelist_kbytes >> (PAGE_SHIFT - 10);
	unsigned long clean_file_mem = file_mem - dirty_mem;
	/* Conservatively estimate the amount of available_file_mem */
	unsigned long available_file_mem = (clean_file_mem > min_file_mem) ?
	    (clean_file_mem - min_file_mem) : 0;
	return available_file_mem;
}

/*
 * Compute "available" memory, that is either free memory or memory that can be
 * reclaimed quickly, adjusted for the presence of swap.
 */
static inline unsigned long get_available_mem_adj(int lru_base)
{
	/* min_free_kbytes is reserved for emergency allocation like when
	 * PF_MEMALLOC is set. In general it's not usable in normal page
	 * allocation process.
	 */
	unsigned long min_free_pages = min_free_kbytes >> (PAGE_SHIFT - 10);
	/* free_mem is completely unallocated; clean file-backed memory
	 * (file_mem - dirty_mem) is easy to reclaim, except for the last
	 * min_filelist_kbytes.
	 */
	unsigned long free_mem =
			global_page_state(NR_FREE_PAGES) - min_free_pages;
	unsigned long available_mem = free_mem +
	    get_available_file_mem(lru_base);
	long _nr_swap_pages = get_nr_swap_pages();
	/*
	 * The contribution of swap is reduced by a factor of
	 * low_mem_ram_vs_swap_weight.
	 */
	unsigned long swap_adj = _nr_swap_pages;

	do_div(swap_adj, low_mem_ram_vs_swap_weight);
	return available_mem + swap_adj;
}

#ifdef CONFIG_LOW_MEM_NOTIFY
static inline bool low_mem_check(void)
{
	const int lru_base = NR_LRU_BASE - LRU_BASE;
	static bool was_low_mem;	/* = false, as per style guide */
	/* We declare a low-memory condition when a combination of RAM and swap
	 * space is low.
	 */
	unsigned long available_mem = get_available_mem_adj(lru_base);
	/*
	 * For backwards compatibility with the older margin interface, we will trigger
	 * the /dev/chromeos-low_mem device when we are below the lowest threshold
	 */
	bool is_low_mem = available_mem < low_mem_thresholds[0];
	unsigned int threshold_lowest = UINT_MAX;
	int i;

	if (!low_mem_margin_enabled)
		return false;

	if (unlikely(is_low_mem && !was_low_mem)) {
		unsigned long anon_mem =
			global_page_state(lru_base + LRU_ACTIVE_ANON) +
			global_page_state(lru_base + LRU_INACTIVE_ANON);
		if (unlikely(anon_mem < low_mem_lowest_seen_anon_mem)) {
			printk(KERN_INFO "entering low_mem "
			       "(avail RAM = %lu kB, avail swap %lu kB, "
			       "avail file %lu kB) "
			       "with lowest seen anon mem: %lu kB\n",
			       available_mem * PAGE_SIZE / 1024,
			       get_nr_swap_pages() * PAGE_SIZE / 1024,
			       get_available_file_mem(lru_base) * PAGE_SIZE /
				  1024,
			       anon_mem * PAGE_SIZE / 1024);
			low_mem_lowest_seen_anon_mem = anon_mem -
				low_mem_anon_mem_delta;
		}
	}

	was_low_mem = is_low_mem;
	if (is_low_mem)
		low_mem_notify();

	for (i = 0; i < low_mem_threshold_count; i++)
		if (available_mem < low_mem_thresholds[i]) {
			threshold_lowest = i;
			break;
		}

	/* we crossed one or more thresholds */
	if (unlikely(threshold_lowest < low_mem_threshold_last))
		low_mem_threshold_notify();

	low_mem_threshold_last = threshold_lowest;

	return is_low_mem;
}
#else
static inline bool low_mem_check(void) { return false;}
#endif

#endif
