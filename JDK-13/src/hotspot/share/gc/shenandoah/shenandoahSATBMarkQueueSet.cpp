/*
 * Copyright (c) 2018, 2019, Red Hat, Inc. All rights reserved.
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

#include "gc/shenandoah/shenandoahHeap.inline.hpp"
#include "gc/shenandoah/shenandoahSATBMarkQueueSet.hpp"
#include "gc/shenandoah/shenandoahThreadLocalData.hpp"

ShenandoahSATBMarkQueueSet::ShenandoahSATBMarkQueueSet() :
  _heap(NULL),
  _satb_mark_queue_buffer_allocator("SATB Buffer Allocator", ShenandoahSATBBufferSize)
{}

void ShenandoahSATBMarkQueueSet::initialize(ShenandoahHeap* const heap,
                                            Monitor* cbl_mon,
                                            int process_completed_threshold,
                                            uint buffer_enqueue_threshold_percentage) {
  SATBMarkQueueSet::initialize(cbl_mon,
                               &_satb_mark_queue_buffer_allocator,
                               process_completed_threshold,
                               buffer_enqueue_threshold_percentage);
  _heap = heap;
}

SATBMarkQueue& ShenandoahSATBMarkQueueSet::satb_queue_for_thread(Thread* const t) const {
  return ShenandoahThreadLocalData::satb_mark_queue(t);
}

template <bool RESOLVE>
class ShenandoahSATBMarkQueueFilterFn {
  ShenandoahHeap* const _heap;

public:
  ShenandoahSATBMarkQueueFilterFn(ShenandoahHeap* heap) : _heap(heap) {}

  // Return true if entry should be filtered out (removed), false if
  // it should be retained.
  bool operator()(const void* entry) const {
    return !_heap->requires_marking<RESOLVE>(entry);
  }
};

void ShenandoahSATBMarkQueueSet::filter(SATBMarkQueue* queue) {
  assert(_heap != NULL, "SATB queue set not initialized");
  if (_heap->has_forwarded_objects()) {
    apply_filter(ShenandoahSATBMarkQueueFilterFn<true>(_heap), queue);
  } else {
    apply_filter(ShenandoahSATBMarkQueueFilterFn<false>(_heap), queue);
  }
}

void ShenandoahSATBMarkQueue::handle_completed_buffer() {
  SATBMarkQueue::handle_completed_buffer();
  if (!is_empty()) {
    Thread* t = Thread::current();
    if (ShenandoahThreadLocalData::is_force_satb_flush(t)) {
      // Non-empty buffer is compacted, and we decided not to enqueue it.
      // We still want to know about leftover work in that buffer eventually.
      // This avoid dealing with these leftovers during the final-mark, after
      // the buffers are drained completely. See JDK-8205353 for more discussion.
      ShenandoahThreadLocalData::set_force_satb_flush(t, false);
      enqueue_completed_buffer();
    }
  }
}






ShenandoahPrefetchQueueSet::ShenandoahPrefetchQueueSet() : _heap(NULL) {}

void ShenandoahPrefetchQueueSet::initialize(ShenandoahHeap* heap,
                                    Monitor* cbl_mon,
                                    BufferNode::Allocator* allocator
                                    /*size_t process_completed_buffers_threshold,
                                    uint buffer_enqueue_threshold_percentage,
                                    Mutex* lock*/) {
  PrefetchQueueSet::initialize(cbl_mon,
                               allocator/*,
                               process_completed_buffers_threshold,
                               buffer_enqueue_threshold_percentage,
                               lock*/);
  _heap = heap;
}

void ShenandoahPrefetchQueueSet::handle_zero_index_for_thread(JavaThread* t) {
  ShenandoahThreadLocalData::prefetch_queue(t).handle_zero_index();
}

PrefetchQueue& ShenandoahPrefetchQueueSet::prefetch_queue_for_thread(JavaThread* const t) const{
  return ShenandoahThreadLocalData::prefetch_queue(t);
}


static inline bool requires_marking_prefetch(const void* entry, ShenandoahHeap* heap) {

  if(heap->is_in_reserved(entry)) {
    return true;
  }
  return false;
}

static inline bool discard_entry_prefetch(const void* entry, ShenandoahHeap* heap) {
  return !requires_marking_prefetch(entry, heap) || heap->marking_context()->is_marked((oop)entry);
}

// Workaround for not yet having std::bind.
class ShenandoahPrefetchQueueFilterFn {
  ShenandoahHeap* _heap;

public:
  ShenandoahPrefetchQueueFilterFn(ShenandoahHeap* heap) : _heap(heap) {}

  // Return true if entry should be filtered out (removed), false if
  // it should be retained.
  bool operator()(const void* entry) const {
    return discard_entry_prefetch(entry, _heap);
  }
};

void ShenandoahPrefetchQueueSet::filter(PrefetchQueue* queue) {
  assert(_heap != NULL, "SATB queue set not initialized");
  apply_filter(ShenandoahPrefetchQueueFilterFn(_heap), queue);
}