static inline struct page *cma_alloc_or_rsv(struct cma_heap *cma_heap,
					    unsigned long nr_pages,
					    unsigned int align,
					    unsigned long len)
{
	struct config_cma_rsv *cma_rsv = cma_heap->cma_rsv;
	struct page *pages;

	if (len > SZ_1M * 10)
		mm_logi("warn: len:%lu", len);

	if (!cma_rsv || len < cma_rsv->min || len > cma_rsv->max)
		goto fallback;

	if (test_and_set_bit(0, &cma_rsv->pages))
		goto fallback;

	pages = (struct page *)(cma_rsv->pages & ~1UL);
	if (pages) {
		mm_logi("HIT rsv_pages len:%lu p:%lx\n", len, (unsigned long)pages);
		return pages;
	}
fallback:
	return cma_alloc(cma_heap->cma, nr_pages, align, false);
}

static void cma_rsv_release(struct timer_list *t)
{
	struct config_cma_rsv *cma_rsv = from_timer(cma_rsv, t, timer);
	struct page *pages;
	size_t size;
	unsigned long nr_pages;

	if (test_and_set_bit(0, &cma_rsv->pages)) {
		mm_logi("rsv_pages already used\n");
		return;
	}

	size = PAGE_ALIGN(cma_rsv->max);
	nr_pages = size >> PAGE_SHIFT;
	pages = (struct page *)(cma_rsv->pages & ~1UL);
	cma_release(cma_rsv->cma, pages, nr_pages);
	mm_logi("timer: rsv_pages released\n");
}

static void probe_ta_cma_rsv(struct cma_heap *cma_heap)
{
	struct config_cma_rsv *cma_rsv;
	size_t size;
	unsigned long nr_pages, align;
	struct page *cma_pages;

	cma_rsv = oplus_read_mm_config(module_name_ta_cma_rsv);
	if (!cma_rsv ||
	    strcmp(cma_get_name(cma_heap->cma), cma_rsv->bind_cma) != 0)
		return;

	size = PAGE_ALIGN(cma_rsv->max);
	nr_pages = size >> PAGE_SHIFT;
	align = get_order(size);
	if (align > cma_heap->max_align)
		align = cma_heap->max_align;

	cma_pages = cma_alloc(cma_heap->cma, nr_pages, align, false);
	if (!cma_pages) {
		mm_logi("failed to rsv pages\n");
		return;
	}

	cma_rsv->pages = (unsigned long)cma_pages;
	cma_rsv->cma = cma_heap->cma;
	cma_heap->cma_rsv = cma_rsv;
	mm_logi("rsv_pages: %lu range:[%u-%u] p:%lx\n",
		nr_pages, cma_rsv->min, cma_rsv->max, cma_rsv->pages);
	timer_setup(&cma_rsv->timer, cma_rsv_release, 0);
	/* 10 min timer iff rsv pages not used by hal */
	mod_timer(&cma_rsv->timer, jiffies + 10 * 60 * HZ);
}
