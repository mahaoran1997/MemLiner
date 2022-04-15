#include "rswap_rdma.h"
#include "rswap_dram.h"
#include "rswap_scheduler.h"

//
// ############################ Start of RDMA operation for Fronswap ############################
//

/**
 * Wait for the finish of ALL the outstanding rdma_request
 *
 */
void drain_rdma_queue(struct rswap_rdma_queue *rdma_queue)
{
	unsigned long flags;

	while (atomic_read(&rdma_queue->rdma_post_counter) > 0) { // [?] not disable preempt, other threads may keep enqueuing request into the rdma_queue ?
		spin_lock_irqsave(&rdma_queue->cq_lock, flags);
		ib_process_cq_direct(rdma_queue->cq, 16);	     //  default, IB_POLL_BATCH is 16. return when cqe reaches min(16, IB_POLL_BATCH) or CQ is empty.
		spin_unlock_irqrestore(&rdma_queue->cq_lock, flags); // [?] Is the spin lock necessary ?
		cpu_relax();
	}

	return;
}

/**
 * Drain all the outstanding messages for a specific memory server.
 * [?] TO BE DONE. Multiple memory server
 *
 */
void drain_all_rdma_queues(int target_mem_server)
{
	int i;
	struct rdma_session_context *rdma_session = &rdma_session_global;

	for (i = 0; i < num_queues; i++) {
		drain_rdma_queue(&(rdma_session->rdma_queues[i]));
	}
}

/**
 * The function to process rdma write done.
 *
 */
void fs_rdma_write_done(struct ib_cq *cq, struct ib_wc *wc)
{
	// get the instance start address of fs_rdma_req, whose filed, fs_rdma_req->cqe is pointed by wc->wr_cqe
	struct fs_rdma_req *rdma_req = container_of(wc->wr_cqe, struct fs_rdma_req, cqe);
	struct rswap_rdma_queue *rdma_queue = cq->cq_context;
	struct ib_device *ibdev = rdma_queue->rdma_session->rdma_dev->dev;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		pr_err("%s status is not success, it is=%d\n", __func__, wc->status);
	}
	ib_dma_unmap_page(ibdev, rdma_req->dma_addr, PAGE_SIZE, DMA_TO_DEVICE);

	atomic_dec(&rdma_queue->rdma_post_counter); // decrease outstanding rdma request counter
	complete(&rdma_req->done);		    // inform caller, write is done. is this necessary for a write?
	kmem_cache_free(rdma_queue->fs_rdma_req_cache, rdma_req); // safe to free
}

void fs_rdma_read_done(struct ib_cq *cq, struct ib_wc *wc)
{
	// get the instance start address of fs_rdma_req, whose filed, fs_rdma_req->cqe is pointed by wc->wr_cqe
	struct fs_rdma_req *rdma_req = container_of(wc->wr_cqe, struct fs_rdma_req, cqe);
	struct rswap_rdma_queue *rdma_queue = cq->cq_context;
	struct ib_device *ibdev = rdma_queue->rdma_session->rdma_dev->dev;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		pr_err("%s status is not success, it is=%d\n", __func__, wc->status);
	}
	ib_dma_unmap_page(ibdev, rdma_req->dma_addr, PAGE_SIZE, DMA_FROM_DEVICE);

	SetPageUptodate(rdma_req->page);  // [?] will be invoked in swap_readpage(). no need to do it here ?
	unlock_page(rdma_req->page);
	atomic_dec(&rdma_queue->rdma_post_counter); // decrease outstanding rdma request counter
	complete(&rdma_req->done); // inform caller, write is done. is this necessary for a write?
	kmem_cache_free(rdma_queue->fs_rdma_req_cache, rdma_req); // safe to free
}

/**
 * When we enqueue a write/read wr,
 * the total number can't exceed the send/receive queue depth.
 * Or it will cause QP Out of Memory error.
 *
 * return :
 *  0 : success;
 *  -1 : error.
 *
 * More explanation:
 * There are 2 queues for QP, send/recv queue.
 * 1) Send queue limit the number of outstanding wr.
 *    This limits both the 1-sided/2-sided wr.
 * 2) For 2-sided RDMA, it also needs to post s recv wr to receive data.
 *    we reply on 1-sided RDMA wr to write/read data.
 *    The the depth of send queue should be much larger than recv queue.
 *
 * The depth of these 2 queues are limited by:
 * 	init_attr.cap.max_send_wr
 *	init_attr.cap.max_recv_wr
 *
 */
int fs_enqueue_send_wr(struct rdma_session_context *rdma_session, struct rswap_rdma_queue *rdma_queue, struct fs_rdma_req *rdma_req)
{
	int ret = 0;
	const struct ib_send_wr *bad_wr;
	int test;

	rdma_req->rdma_queue = rdma_queue; // points to the rdma_queue to be enqueued.

	// Post 1-sided RDMA read wr
	// wait and enqueue wr
	// Both 1-sided read/write queue depth are RDMA_SEND_QUEUE_DEPTH
	while (1) {
		test = atomic_inc_return(&rdma_queue->rdma_post_counter);
		if (test < RDMA_SEND_QUEUE_DEPTH - 16) {
			//post the 1-sided RDMA write
			// Use the global RDMA context, rdma_session_global
			ret = ib_post_send(rdma_queue->qp, (struct ib_send_wr *)&rdma_req->rdma_wr, &bad_wr);
			if (unlikely(ret)) {
				printk(KERN_ERR "%s, post 1-sided RDMA send wr failed, return value :%d. counter %d \n", __func__, ret, test);
				ret = -1;
				goto err;
			}

			// Enqueue successfully.
			// exit loop.
			return ret;
		} else {
			// RDMA send queue is full, wait for next turn.
			test = atomic_dec_return(&rdma_queue->rdma_post_counter);
			//schedule(); // release the core for a while.
			// cpu_relax(); // which one is better ?

			// IB_DIRECT_CQ, poll cqe directly
			drain_rdma_queue(rdma_queue);
		}
	}
err:
	printk(KERN_ERR " Error in %s \n", __func__);
	return -1;
}

/**
 * Build a rdma_wr for frontswap data path.
 *
 */
int fs_build_rdma_wr(struct rdma_session_context *rdma_session, struct rswap_rdma_queue *rdma_queue, struct fs_rdma_req *rdma_req,
			struct remote_mapping_chunk *remote_chunk_ptr, size_t offset_within_chunk, struct page *page, enum dma_data_direction dir)
{

	int ret = 0;
	struct ib_device *dev = rdma_session->rdma_dev->dev;

	// 1) Map a single page as RDMA buffer
	rdma_req->page = page;
	init_completion(&(rdma_req->done));

	rdma_req->dma_addr = ib_dma_map_page(dev, page, 0, PAGE_SIZE, dir);
	if (unlikely(ib_dma_mapping_error(dev, rdma_req->dma_addr))) {
		pr_err("%s, ib_dma_mapping_error\n", __func__);
		ret = -ENOMEM;
		kmem_cache_free(rdma_queue->fs_rdma_req_cache, rdma_req);
		goto out;
	}

	ib_dma_sync_single_for_device(dev, rdma_req->dma_addr, PAGE_SIZE, dir); // Map the dma address to IB deivce.

	if (dir == DMA_TO_DEVICE) {
		rdma_req->cqe.done = fs_rdma_write_done; // rdma cqe process function
	} else {
		rdma_req->cqe.done = fs_rdma_read_done;
	}

	// 2) Initialize the rdma_wr
	// 2.1 local addr
	rdma_req->sge.addr = rdma_req->dma_addr;
	rdma_req->sge.length = PAGE_SIZE;
	rdma_req->sge.lkey = rdma_session->rdma_dev->pd->local_dma_lkey;

	// 2.2 initia rdma_wr for remote addr
	rdma_req->rdma_wr.wr.next = NULL;
	rdma_req->rdma_wr.wr.wr_cqe = &rdma_req->cqe; // assing completion handler. prepare for container_of()
	rdma_req->rdma_wr.wr.sg_list = &(rdma_req->sge);
	rdma_req->rdma_wr.wr.num_sge = 1; // single page.  [?] how to support mutiple pages ?
	rdma_req->rdma_wr.wr.opcode = (dir == DMA_TO_DEVICE ? IB_WR_RDMA_WRITE : IB_WR_RDMA_READ);
	rdma_req->rdma_wr.wr.send_flags = IB_SEND_SIGNALED;
	rdma_req->rdma_wr.remote_addr = remote_chunk_ptr->remote_addr + offset_within_chunk;
	rdma_req->rdma_wr.rkey = remote_chunk_ptr->remote_rkey;

//debug
#ifdef DEBUG_MODE_BRIEF
	if (dir == DMA_FROM_DEVICE)
	{
		printk(KERN_INFO "%s, read data from remote 0x%lx, size 0x%lx \n", __func__, (size_t)rdma_req->rdma_wr.remote_addr, (size_t)PAGE_SIZE);
	}
#endif

out:
	return ret;
}

/**
 * Enqueue a page into RDMA queue.
 *
 */
int fs_rdma_send(struct rdma_session_context *rdma_session, struct rswap_rdma_queue *rdma_queue, struct fs_rdma_req *rdma_req,
			struct remote_mapping_chunk *remote_chunk_ptr, size_t offset_within_chunk, struct page *page, enum dma_data_direction dir)
{

	int ret = 0;

	// initialize the rdma_req
	ret = fs_build_rdma_wr(rdma_session, rdma_queue, rdma_req, remote_chunk_ptr, offset_within_chunk, page, dir);
	if (unlikely(ret)) {
		pr_err("%s, Build rdma_wr failed.\n", __func__);
		goto out;
	}

	// enqueue the rdma_req
	ret = fs_enqueue_send_wr(rdma_session, rdma_queue, rdma_req);
	if (unlikely(ret)) {
		pr_err("%s, enqueue rdma_wr failed.\n", __func__);
		goto out;
	}

out:
	return ret;
}

//
// ############################ Start of Fronswap operations definition ############################
//

/**
 * Synchronously write data to memory server.
 *
 *  1.swap out is single pages in default.
 *    [?]  Can we make it support multiple pages swapout ?
 *
 *  2. This is a synchronous swapping out. Return only when pages is written out.
 *     This is the assumption of frontswap store operation.
 *
 * Parameters
 *  type : used to select the swap device ?
 *  page_offset : swp_offset(entry). the offset for a page in the swap partition/device.
 *  page : the handler of page.
 *
 *
 * return
 *  0 : success
 *  non-zero : failed.
 *
 */
int rswap_frontswap_store(unsigned type, pgoff_t swap_entry_offset, struct page *page)
{
#ifdef ENABLE_VQUEUE
	int ret = 0;
	int cpu = -1;
	struct rswap_rdma_queue *rdma_queue;
	struct rswap_vqueue *vqueue;
	struct rswap_request vrequest = { swap_entry_offset, page, STORE, cpu };

	cpu = get_cpu(); // disable preemption

	vrequest.cpu = cpu;
	vqueue = rswap_vqueue_list_get(global_rswap_vqueue_list, cpu);
	if ((ret = rswap_vqueue_enqueue(vqueue, &vrequest)) != 0) {
		print_err(ret);
		put_cpu();
		goto out;
	}

	put_cpu(); // enable preeempt.

	// 2.3 wait for write is done.

	// busy wait on the rdma_queue[cpu].
	// This is not exclusive.
	rswap_vqueue_drain(vqueue);
	rdma_queue = get_rdma_queue(&rdma_session_global, cpu, QP_SYNC);
	drain_rdma_queue(rdma_queue); // poll the corresponding RDMA CQ

#else // !ENABLE_VQUEUE
	int ret = 0;
	int cpu;
	struct fs_rdma_req *rdma_req;
	struct rswap_rdma_queue *rdma_queue;
	struct rdma_session_context *rdma_session = &rdma_session_global; // support multiple Memory server later. !!

// page offset, compared start of Data Region
// The real virtual address is RDMA_DATA_SPACE_START_ADDR + start_addr.
#ifdef ENABLE_SWP_ENTRY_VIRT_REMAPPING
	size_t start_addr = retrieve_swap_remmaping_virt_addr_via_offset(swap_entry_offset) << PAGE_SHIFT; // calculate the remote addr
#else
	// For the default kernel, no need to do the swp_offset -> virt translation
	size_t start_addr = swap_entry_offset << PAGE_SHIFT;
#endif

	//size_t bytes_len            = PAGE_SIZE; // single page for now
	size_t start_chunk_index = start_addr >> CHUNK_SHIFT;
	size_t offset_within_chunk = start_addr & CHUNK_MASK;
	struct remote_mapping_chunk *remote_chunk_ptr;

#ifdef DEBUG_FRONTSWAP_ONLY
	// 1) Local dram path
	ret = rswap_dram_write(page, swap_entry_offset << PAGE_SHIFT); // only return after copying is done.
	if (unlikely(ret)) {
		pr_err("could not read page remotely\n");
		goto out;
	}

#else
	// 2) RDMA path
	cpu = get_cpu(); // disable preempt
	//cpu = smp_processor_id(); // if already disabled the preempt in caller, use this one

	// 2.1 get the rdma queue and remote chunk
	rdma_queue = get_rdma_queue(rdma_session, cpu, QP_SYNC);
	rdma_req = (struct fs_rdma_req *)kmem_cache_alloc(rdma_queue->fs_rdma_req_cache, GFP_ATOMIC);
	if (unlikely(rdma_req == NULL)) {
		pr_err("%s, get reserved fs_rdma_req failed. \n", __func__);
		ret = -1;
		goto out;
	}

	// get the rdma queue
	remote_chunk_ptr = &(rdma_session->remote_chunk_list.remote_chunk[start_chunk_index]);

	// 2.2 enqueue RDMA request
	ret = fs_rdma_send(rdma_session, rdma_queue, rdma_req, remote_chunk_ptr, offset_within_chunk, page, DMA_TO_DEVICE);
	if (unlikely(ret)) {
		pr_err("%s, enqueuing rdma frontswap write failed.\n", __func__);
		goto out;
	}

	put_cpu(); // enable preeempt.

	// 2.3 wait for write is done.

	// busy wait on the rdma_queue[cpu].
	// This is not exclusive.
	drain_rdma_queue(rdma_queue); // poll the corresponding RDMA CQ

	ret = 0;						  // reset to 0 for succss.

#endif

#endif

#ifdef DEBUG_MODE_DETAIL
	pr_info("%s, rdma_queue[%d] store page 0x%lx, virt addr 0x%lx DONE <<<<< \n", __func__, rdma_queue->q_index, (size_t)page, start_addr);
#endif

out:
	return ret;
}

/**
 * Synchronously read data from memory server.
 *
 *
 * return:
 *  0 : success
 *  non-zero : failed.
 */
int rswap_frontswap_load(unsigned type, pgoff_t swap_entry_offset, struct page *page)
{
#ifdef ENABLE_VQUEUE
	int ret = 0;
	int cpu = -1;
	struct rswap_vqueue *vqueue;
	struct rswap_rdma_queue *rdma_queue;
	struct rswap_request vrequest = { swap_entry_offset, page, LOAD_SYNC, cpu };

	cpu = smp_processor_id();

	vrequest.cpu = cpu;
	vqueue = rswap_vqueue_list_get(global_rswap_vqueue_list, cpu);
	if ((ret = rswap_vqueue_enqueue(vqueue, &vrequest)) != 0) {
		print_err(ret);
		goto out;
	}

	rswap_vqueue_drain(vqueue);
	rdma_queue = get_rdma_queue(&rdma_session_global, cpu, QP_SYNC);
	drain_rdma_queue(rdma_queue); // poll the corresponding RDMA CQ

#else // !ENABLE_VQUEUE
	int ret = 0;
	int cpu;
	struct fs_rdma_req *rdma_req;
	struct rswap_rdma_queue *rdma_queue;
	struct rdma_session_context *rdma_session = &rdma_session_global; // support multiple Memory server later. !!

	struct remote_mapping_chunk *remote_chunk_ptr;
// page offset, compared start of Data Region
// The real virtual address is RDMA_DATA_SPACE_START_ADDR + start_addr.
#ifdef ENABLE_SWP_ENTRY_VIRT_REMAPPING
	size_t start_addr = retrieve_swap_remmaping_virt_addr_via_offset(swap_entry_offset) << PAGE_SHIFT; // calculate the remote addr
#else
	// For the default kernel, no need to do the swp_offset -> virt translation
	size_t start_addr = swap_entry_offset << PAGE_SHIFT;
#endif

	//size_t bytes_len            = PAGE_SIZE; // single page for now

	size_t start_chunk_index = start_addr >> CHUNK_SHIFT;
	size_t offset_within_chunk = start_addr & CHUNK_MASK;

#ifdef DEBUG_FRONTSWAP_ONLY
	ret = rswap_dram_read(page, swap_entry_offset << PAGE_SHIFT);
	if (unlikely(ret)) {
		pr_err("could not read page remotely\n");
		goto out;
	}

#else
	// 2) RDMA path
	cpu = smp_processor_id();

	// 2.1 get the rdma queue and remote chunk
	rdma_queue = get_rdma_queue(rdma_session, cpu, QP_SYNC);
	rdma_req = (struct fs_rdma_req *)kmem_cache_alloc(rdma_queue->fs_rdma_req_cache, GFP_ATOMIC);
	if (unlikely(rdma_req == NULL)) {
		pr_err("%s, get reserved fs_rdma_req failed. \n", __func__);
		ret = -1;
		goto out;
	}

	// get the rdma queue
	remote_chunk_ptr = &(rdma_session->remote_chunk_list.remote_chunk[start_chunk_index]);

	// 2.2 enqueue RDMA request
	ret = fs_rdma_send(rdma_session, rdma_queue, rdma_req, remote_chunk_ptr, offset_within_chunk, page, DMA_FROM_DEVICE);
	if (unlikely(ret)) {
		pr_err("%s, enqueuing rdma frontswap write failed.\n", __func__);
		goto out;
	}

	drain_rdma_queue(rdma_queue);

#ifdef DEBUG_MODE_DETAIL
	pr_info("%s, rdma_queue[%d]  load page 0x%lx, virt addr 0x%lx, swp_offset 0x%lx  >>>>> \n",
		__func__, rdma_queue->q_index, (size_t)page, (szie_t)(RDMA_DATA_SPACE_START_ADDR + start_addr), (size_t)swap_entry_offset);
#endif

	ret = 0; // reset to 0 for succss.
#ifdef DEBUG_MODE_DETAIL
	pr_info("%s, rdma_queue[%d] load page 0x%lx, virt addr 0x%lx DONE <<<<< \n", __func__, rdma_queue->q_index, (size_t)page, start_addr);
#endif

#endif
#endif // ENABLE_VQUEUE

out:
	return ret;
}

int rswap_frontswap_load_async(unsigned type, pgoff_t swap_entry_offset, struct page *page)
{
#ifdef ENABLE_VQUEUE
	int ret = 0;
	int cpu = -1;
	struct rswap_vqueue *vqueue;
	struct rswap_request vrequest = { swap_entry_offset, page, LOAD_ASYNC, cpu };

	// 2) RDMA path
	cpu = smp_processor_id();

	vrequest.cpu = cpu;
	vqueue = rswap_vqueue_list_get(global_rswap_vqueue_list, cpu);
	if ((ret = rswap_vqueue_enqueue(vqueue, &vrequest)) != 0) {
		print_err(ret);
		goto out;
	}

#else //!ENABLE_VQUEUE
	int ret = 0;
	int cpu;
	struct fs_rdma_req *rdma_req;
	struct rswap_rdma_queue *rdma_queue;
	struct rdma_session_context *rdma_session = &rdma_session_global; // support multiple Memory server later. !!

	struct remote_mapping_chunk *remote_chunk_ptr;
// page offset, compared start of Data Region
// The real virtual address is RDMA_DATA_SPACE_START_ADDR + start_addr.
#ifdef ENABLE_SWP_ENTRY_VIRT_REMAPPING
	size_t start_addr = retrieve_swap_remmaping_virt_addr_via_offset(swap_entry_offset) << PAGE_SHIFT; // calculate the remote addr
#else
	// For the default kernel, no need to do the swp_offset -> virt translation
	size_t start_addr = swap_entry_offset << PAGE_SHIFT;
#endif

	//size_t bytes_len            = PAGE_SIZE; // single page for now

	size_t start_chunk_index = start_addr >> CHUNK_SHIFT;
	size_t offset_within_chunk = start_addr & CHUNK_MASK;

#ifdef DEBUG_FRONTSWAP_ONLY
	ret = rswap_dram_read(page, swap_entry_offset << PAGE_SHIFT);
	if (unlikely(ret)) {
		pr_err("could not read page remotely\n");
		goto out;
	}

#else
	// 2) RDMA path
	cpu = smp_processor_id();

	// 2.1 get the rdma queue and remote chunk
	rdma_queue = get_rdma_queue(rdma_session, cpu, QP_ASYNC);
	rdma_req = (struct fs_rdma_req *)kmem_cache_alloc(rdma_queue->fs_rdma_req_cache, GFP_ATOMIC);
	if (unlikely(rdma_req == NULL)) {
		pr_err("%s, get reserved fs_rdma_req failed. \n", __func__);
		ret = -1;
		goto out;
	}

	// get the rdma queue
	remote_chunk_ptr = &(rdma_session->remote_chunk_list.remote_chunk[start_chunk_index]);

	// 2.2 enqueue RDMA request
	ret = fs_rdma_send(rdma_session, rdma_queue, rdma_req, remote_chunk_ptr, offset_within_chunk, page, DMA_FROM_DEVICE);
	if (unlikely(ret)) {
		pr_err("%s, enqueuing rdma frontswap write failed.\n", __func__);
		goto out;
	}

#ifdef DEBUG_MODE_DETAIL
	pr_info("%s, rdma_queue[%d]  load page 0x%lx, virt addr 0x%lx, swp_offset 0x%lx  >>>>> \n",
		__func__, rdma_queue->q_index, (size_t)page, (szie_t)(RDMA_DATA_SPACE_START_ADDR + start_addr), (size_t)swap_entry_offset);
#endif

	ret = 0; // reset to 0 for succss.

#ifdef DEBUG_MODE_DETAIL
	pr_info("%s, rdma_queue[%d] load page 0x%lx, virt addr 0x%lx DONE <<<<< \n", __func__, rdma_queue->q_index, (size_t)page, start_addr);
#endif

#endif
#endif //ENABLE_VQUEUE

out:
	return ret;
}

int rswap_frontswap_poll_load(int cpu)
{
#ifdef ENABLE_VQUEUE
	struct rswap_vqueue *vqueue;
	struct rswap_rdma_queue *rdma_queue;

	vqueue = rswap_vqueue_list_get(global_rswap_vqueue_list, cpu);
	rswap_vqueue_drain(vqueue);
	rdma_queue = get_rdma_queue(&rdma_session_global, cpu, QP_SYNC);
	drain_rdma_queue(rdma_queue); // poll the corresponding RDMA CQ
#else // !ENABLE_VQUEUE
#ifndef DEBUG_FRONTSWAP_ONLY
	struct rswap_rdma_queue *rdma_queue;

	rdma_queue = get_rdma_queue(&rdma_session_global, cpu, QP_SYNC);
	drain_rdma_queue(rdma_queue); // poll the corresponding RDMA CQ
#endif
#endif // ENABLE_VQUEUE
	return 0;
}

static void rswap_invalidate_page(unsigned type, pgoff_t offset)
{
#ifdef DEBUG_MODE_DETAIL
	pr_info("%s, remove page_virt addr 0x%lx\n", __func__, offset << PAGE_OFFSET);
#endif
	return;
}

/**
 * What's the purpose ?
 *
 */
static void rswap_invalidate_area(unsigned type)
{
#ifdef DEBUG_MODE_DETAIL
	pr_warn("%s, remove the pages of area 0x%x ?\n", __func__, type);
#endif
	return;
}

static void rswap_frontswap_init(unsigned type)
{
#ifdef ENABLE_VQUEUE
	int ret;
	ret = rswap_scheduler_init(&rdma_session_global);
	if (ret) {
		print_err(ret);
	}
#endif // ENABLE_VQUEUE
}

static struct frontswap_ops rswap_frontswap_ops = {
	.init = rswap_frontswap_init,
	.store = rswap_frontswap_store,
	.load = rswap_frontswap_load,
	.load_async = rswap_frontswap_load_async,
	.poll_load = rswap_frontswap_poll_load,
	.invalidate_page = rswap_invalidate_page, // Invoked to clear a page of memory pool?
	.invalidate_area = rswap_invalidate_area,
};

int rswap_register_frontswap(void)
{
	frontswap_register_ops(&rswap_frontswap_ops); // will enable the frontswap path

#ifdef DEBUG_FRONTSWAP_ONLY
	rswap_init_local_dram();
#endif

	pr_info("frontswap module loaded\n");
	return 0;
}

void rswap_deregister_frontswap(void)
{
#ifdef DEBUG_FRONTSWAP_ONLY
	rswap_remove_local_dram();
#endif

	// * TO BE DONE * - Have to reboot after rmmod
	// We need remove the frontswap path from kernel. How to do this ??
	// 1) dec frontswap_enabled_key
	// 2) Also need to unplug the swap device from kernel.

	pr_info("unloading frontswap module - * TO BE DONE *\n");
	pr_info("1) dec frontswap_enabled_key \n");
	pr_info("2) Also need to unplug the swap device from kernel.\n");
#ifdef ENABLE_VQUEUE
	rswap_scheduler_stop();
#endif // ENABLE_VQUEUE
}
