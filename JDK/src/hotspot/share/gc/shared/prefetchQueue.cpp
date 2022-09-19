/*
 * Copyright (c) 2001, 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */
#include "precompiled.hpp"
#include "gc/shared/prefetchQueue.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "logging/log.hpp"
#include "memory/allocation.inline.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/thread.hpp"
#include "runtime/threadSMR.hpp"
#include "runtime/vmThread.hpp"
PrefetchQueue::PrefetchQueue(PrefetchQueueSet* qset, bool permanent) :
  // SATB queues are only active during marking cycles. We create
  // them with their active field set to false. If a thread is
  // created during a cycle and its SATB queue needs to be activated
  // before the thread starts running, we'll need to set its active
  // field to true. This must be done in the collector-specific
  // BarrierSet::on_thread_attach() implementation.
  //PtrQueue(qset, permanent, false /* active is set to false */),
  
  PtrQueue(qset, permanent, false /* active is set to true*/),
  _m(Mutex::leaf, FormatBuffer<128>("PrefetchQueue"), true, Monitor::_safepoint_check_never),
  _in_processing(false)
{ }
void PrefetchQueue::flush() {
  // Filter now to possibly save work later.  If filtering empties the
  // buffer then flush_impl can deallocate the buffer.
  filter();
  flush_impl();
}
// This method will first apply filtering to the buffer. If filtering
// retains a small enough collection in the buffer, we can continue to
// use the buffer as-is, instead of enqueueing and replacing it.
bool PrefetchQueue::should_enqueue_buffer() {
  assert(_lock == NULL || _lock->owned_by_self(),
         "we should have taken the lock before calling this");
  // This method should only be called if there is a non-NULL buffer
  // that is full.
  assert(index() == 0, "pre-condition");
  assert(_buf != NULL, "pre-condition");
  filter();
  PrefetchQueueSet* prefetch_qset = static_cast<PrefetchQueueSet*>(qset());
  size_t threshold = prefetch_qset->buffer_enqueue_threshold();
  // Ensure we'll enqueue completely full buffers.
  assert(threshold > 0, "enqueue threshold = 0");
  // Ensure we won't enqueue empty buffers.
  assert(threshold <= capacity(),
         "enqueue threshold " SIZE_FORMAT " exceeds capacity " SIZE_FORMAT,
         threshold, capacity());
  return index() < threshold;
}
void PrefetchQueue::apply_closure_and_empty(PrefetchBufferClosure* cl) {
  assert(SafepointSynchronize::is_at_safepoint(),
         "SATB queues must only be processed at safepoints");
  if (_buf != NULL) {
    cl->do_buffer(&_buf[index()], size());
    reset();
  }
}
void PrefetchQueue::handle_zero_index() {
  // MutexLockerEx z(&_m, Mutex::_no_safepoint_check_flag);
  // assert(index() == 0, "precondition");
  // This thread records the full buffer and allocates a new one (while
  // holding the lock if there is one).
  if (_buf != NULL) {
    // // avoid reading by TP threads
    // // Make is_empty() return true.
    // set_index_atomic(capacity());
    // Atomic::store(capacity(), &_tail);
    // return;
    // size_t current_index = index();
    // set_index_atomic(capacity());
    // // Two-fingered compaction toward the end.
    // size_t remaining_objs = MIN2(prefetch_queue_threshold(), _tail-current_index);
    // if(remaining_objs > 0) {
    // 	void** src = &_buf[0];
    // 	void** dst = &_buf[0 + remaining_objs - 1];
    // 	void** end = &_buf[capacity() - 1];
    // 	// assert(src <= dst, "invariant");
    // 	for ( ; src <= dst; dst --, end --) {
    // 		//*end = *dst;
    // 		Atomic::store(*dst, end);
    // 	}
    // }
    // // dst points to the lowest retained entry, or the end of the buffer
    // // if all the entries were filtered out.
    // set_index_atomic(capacity() - remaining_objs);
    // _tail = capacity();
    // Simpler version.
    // Discard all the contents
		// set_index_atomic(capacity());
    // set_tail_atomic(capacity());  // capacity() returns element index
    size_t current_index = index_atomic();
		size_t current_tail = tail_atomic(); 
    set_index_atomic(capacity());
    set_tail_atomic(capacity());  // capacity() returns element index
    //
    // Old version
    // avoid reading by TP threads
    // Make is_empty() return true.
    // set_index_atomic(capacity());
    // Two-fingered compaction toward the end.
    if(current_tail < current_index) current_tail = current_index;
    size_t remaining_objs = MIN2(prefetch_queue_threshold(), current_tail-current_index);
    if(remaining_objs > 0) {
      void** src = &_buf[0];
      void** dst = &_buf[0 + remaining_objs - 1];
      void** end = &_buf[capacity() - 1];
      // assert(src <= dst, "invariant");
      for ( ; src <= dst; dst --, end --) {
        //*end = *dst;
        Atomic::store(*dst, end);
      }
    }
    // dst points to the lowest retained entry, or the end of the buffer
    // if all the entries were filtered out.
    set_index_atomic(capacity() - remaining_objs);
    set_tail_atomic(capacity());
  }
  else {
    // Set capacity in case this is the first allocation.
    set_capacity(qset()->buffer_size());
    // Allocate a new buffer.
    // _buf = NEW_C_HEAP_ARRAY(void*, qset()->buffer_size(), mtGC)
    _buf = qset()->allocate_buffer();
    reset();
  }
}
bool PrefetchQueue::dequeue(void** ptrptr){


		// // *ptrptr = NULL;
		// // return false;
		// // MutexLockerEx z(&_m, Mutex::_no_safepoint_check_flag);
		// size_t current_index = index();
		// size_t current_tail = _tail; 
		// if(current_tail <= current_index + 1 ||current_tail <= 0 || current_tail > capacity()) {
		// 	*ptrptr = NULL;
		// 	return false;
		// }
		// _tail -= 1;
		// // size_t current_index = index();
		// // if(_tail < index()) _tail = index();
		// //*ptrptr = _buf[current_tail - 1];
		// *ptrptr = Atomic::load((void**)&_buf[current_tail - 1]);
		// return true;
		// *ptrptr = NULL;
		// return false;
		// MutexLockerEx z(&_m, Mutex::_no_safepoint_check_flag);
		size_t current_index = index_atomic_for_dequeue();
		size_t current_tail = tail_atomic(); 
		if(_buf == NULL || current_tail <= current_index || current_tail <= 0 || current_tail > capacity()) {
			*ptrptr = NULL;
			return false;
		}
		//_tail -= 1;
		//*ptrptr = _buf[current_tail - 1];
		set_tail_atomic(current_tail-1);  // for consumer, update first, then take the content.
		*ptrptr = Atomic::load((void**)&_buf[current_tail - 1]);
		return true;
} 
PrefetchQueueSet::PrefetchQueueSet() :
  PtrQueueSet(),
  _shared_prefetch_queue(this, true /* permanent */),
  _buffer_enqueue_threshold(0)
{}
void PrefetchQueueSet::initialize(Monitor* cbl_mon,
                                  BufferNode::Allocator* allocator/*,
                                  size_t process_completed_buffers_threshold,
                                  uint buffer_enqueue_threshold_percentage,
                                  Mutex* lock*/) {
  PtrQueueSet::initialize(cbl_mon, allocator);
  // set_process_completed_buffers_threshold(process_completed_buffers_threshold);
  // _shared_prefetch_queue.set_lock(lock);
  assert(buffer_size() != 0, "buffer size not initialized");
  // Minimum threshold of 1 ensures enqueuing of completely full buffers.
  // size_t size = buffer_size();
  // size_t enqueue_qty = (size * buffer_enqueue_threshold_percentage) / 100;
  // _buffer_enqueue_threshold = MAX2(size - enqueue_qty, (size_t)1);
}
void PrefetchQueueSet::set_active_all_threads(bool active, bool expected_active) {
  assert(SafepointSynchronize::is_at_safepoint(), "Must be at safepoint.");
  _all_active = active;
  for (JavaThreadIteratorWithHandle jtiwh; JavaThread *t = jtiwh.next(); ) {
    prefetch_queue_for_thread(t).set_active(active);
  }
  shared_prefetch_queue()->set_active(active);
}
void PrefetchQueueSet::filter_thread_buffers() {
  for (JavaThreadIteratorWithHandle jtiwh; JavaThread *t = jtiwh.next(); ) {
    prefetch_queue_for_thread(t).filter();
  }
  shared_prefetch_queue()->filter();
}
bool PrefetchQueueSet::apply_closure_to_completed_buffer(PrefetchBufferClosure* cl) {
  BufferNode* nd = NULL;
  {
    MutexLockerEx x(_cbl_mon, Mutex::_no_safepoint_check_flag);
    if (_completed_buffers_head != NULL) {
      nd = _completed_buffers_head;
      _completed_buffers_head = nd->next();
      if (_completed_buffers_head == NULL) _completed_buffers_tail = NULL;
      _n_completed_buffers--;
      if (_n_completed_buffers == 0) _process_completed = false;
    }
  }
  if (nd != NULL) {
    void **buf = BufferNode::make_buffer_from_node(nd);
    size_t index = nd->index();
    size_t size = buffer_size();
    assert(index <= size, "invariant");
    cl->do_buffer(buf + index, size - index);
    deallocate_buffer(nd);
    return true;
  } else {
    return false;
  }
}
void PrefetchQueueSet::abandon_partial_marking() {
  BufferNode* buffers_to_delete = NULL;
  {
    MutexLockerEx x(_cbl_mon, Mutex::_no_safepoint_check_flag);
    while (_completed_buffers_head != NULL) {
      BufferNode* nd = _completed_buffers_head;
      _completed_buffers_head = nd->next();
      nd->set_next(buffers_to_delete);
      buffers_to_delete = nd;
    }
    _completed_buffers_tail = NULL;
    _n_completed_buffers = 0;
    DEBUG_ONLY(assert_completed_buffer_list_len_correct_locked());
  }
  while (buffers_to_delete != NULL) {
    BufferNode* nd = buffers_to_delete;
    buffers_to_delete = nd->next();
    deallocate_buffer(nd);
  }
  assert(SafepointSynchronize::is_at_safepoint(), "Must be at safepoint.");
  // So we can safely manipulate these queues.
  for (JavaThreadIteratorWithHandle jtiwh; JavaThread *t = jtiwh.next(); ) {
    prefetch_queue_for_thread(t).reset();
  }
  shared_prefetch_queue()->reset();
}
