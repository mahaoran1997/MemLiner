
#include "precompiled.hpp"

#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "code/codeCache.hpp"

#include "gc/shared/weakProcessor.inline.hpp"
#include "gc/shared/gcTimer.hpp"
#include "gc/shared/referenceProcessor.hpp"
#include "gc/shared/referenceProcessorPhaseTimes.hpp"

#include "gc/shenandoah/shenandoahBarrierSet.inline.hpp"
#include "gc/shenandoah/shenandoahClosures.inline.hpp"
#include "gc/shenandoah/shenandoahConcurrentMark.inline.hpp"
#include "gc/shenandoah/shenandoahMarkCompact.hpp"
#include "gc/shenandoah/shenandoahHeap.inline.hpp"
#include "gc/shenandoah/shenandoahRootProcessor.hpp"
#include "gc/shenandoah/shenandoahOopClosures.inline.hpp"
#include "gc/shenandoah/shenandoahTaskqueue.inline.hpp"
#include "gc/shenandoah/shenandoahTimingTracker.hpp"
#include "gc/shenandoah/shenandoahUtils.hpp"

#include "memory/iterator.inline.hpp"
#include "memory/metaspace.hpp"
#include "memory/resourceArea.hpp"
#include "oops/oop.inline.hpp"

#include "gc/shenandoah/shenandoahConcurrentPrefetch.inline.hpp"




class ShenandoahConcurrentPrefetchingTask : public AbstractGangTask {
private:
  ShenandoahConcurrentPrefetch* _pf;
  ShenandoahTaskTerminator* _terminator;

public:
  ShenandoahConcurrentPrefetchingTask(ShenandoahConcurrentPrefetch* pf, ShenandoahTaskTerminator* terminator) :
    AbstractGangTask("TP Prefetch"), _pf(pf), _terminator(terminator) {
  }

  void work(uint worker_id) {
    // Haoran: modify
    assert(Thread::current()->is_ConcurrentGC_thread(), "Not a concurrent GC thread");
    ShenandoahHeap* heap = ShenandoahHeap::heap();
    // ShenandoahConcurrentWorkerSession worker_session(worker_id);
    // ShenandoahSuspendibleThreadSetJoiner stsj(ShenandoahSuspendibleWorkers);
    ShenandoahObjToScanQueue* q = _pf->get_queue(worker_id);
    ReferenceProcessor* rp;
    if (heap->process_references()) { // Haoran: set ShenandoahRefProcFrequency=0 then false
      rp = heap->ref_processor();
      shenandoah_assert_rp_isalive_installed();
    } else {
      rp = NULL;
    }

    // _cm->concurrent_scan_code_roots(worker_id, rp);
    _pf->mark_loop(worker_id, _terminator, rp,
                   true, // cancellable
                   ShenandoahStringDedup::is_enabled()); // perform string dedup // Haoran: disabled by default, string dedup
  }
};


void ShenandoahConcurrentPrefetch::initialize(uint workers) {
  _in_pf = 0;
  _heap = ShenandoahHeap::heap();

  uint num_queues = MAX2(workers, 1U);

  _task_queues = new ShenandoahObjToScanQueueSet((int) num_queues*2);

  // Haoran: modify
  _current_queue_index = 0;

  for (uint i = 0; i < num_queues*2; ++i) {
    ShenandoahObjToScanQueue* task_queue = new ShenandoahObjToScanQueue();
    task_queue->initialize();
    _task_queues->register_queue(i, task_queue);
  }
}


void ShenandoahConcurrentPrefetch::mark_from_roots() {

  _in_pf = 1;

  WorkGang* workers = _heap->prefetch_workers();
  uint nworkers = workers->active_workers();

  task_queues()->reserve(nworkers*2);

  {
    // ShenandoahTerminationTracker term(ShenandoahPhaseTimings::conc_termination);
    // ShenandoahTaskTerminator terminator(nworkers, task_queues());
    ShenandoahConcurrentPrefetchingTask task(this, NULL);
    workers->run_task(&task);
  }

  _in_pf = 0;
  // task_queues()->clear_claimed();
  // assert(task_queues()->is_empty() || _heap->cancelled_gc(), "Should be empty when not cancelled");
}



void ShenandoahConcurrentPrefetch::cancel() {
  // Clean up marking stacks.
  ShenandoahObjToScanQueueSet* queues = task_queues();
  queues->clear();

  // Cancel SATB buffers.
  // ShenandoahBarrierSet::satb_mark_queue_set().abandon_partial_marking();
}

ShenandoahObjToScanQueue* ShenandoahConcurrentPrefetch::get_queue(uint worker_id) {
  assert(task_queues()->get_reserved() > worker_id, "No reserved queue for worker id: %d", worker_id);
  return _task_queues->queue(worker_id);
}

template <bool CANCELLABLE>
void ShenandoahConcurrentPrefetch::mark_loop_prework(uint w, ShenandoahTaskTerminator *t, ReferenceProcessor *rp,
                                                 bool strdedup) {
  ShenandoahObjToScanQueue* q = get_queue(w*2);

  jushort* ld = _heap->get_prefetch_liveness_cache(w);
  {
    if (_heap->has_forwarded_objects()) {
      if (strdedup) {
        ShenandoahMarkUpdateRefsDedupClosure cl(q, rp);
        mark_loop_work<ShenandoahMarkUpdateRefsDedupClosure, CANCELLABLE>(&cl, ld, w, t); // Haoran: would not call this
      } else {
        ShenandoahMarkUpdateRefsClosure cl(q, rp);
        mark_loop_work<ShenandoahMarkUpdateRefsClosure, CANCELLABLE>(&cl, ld, w, t);
      }
    } else {
      if (strdedup) {
        ShenandoahMarkRefsDedupClosure cl(q, rp);
        mark_loop_work<ShenandoahMarkRefsDedupClosure, CANCELLABLE>(&cl, ld, w, t); // Haoran: would not call this
      } else {
        ShenandoahMarkRefsClosure cl(q, rp);
        mark_loop_work<ShenandoahMarkRefsClosure, CANCELLABLE>(&cl, ld, w, t);
      }
    }
  }

  _heap->flush_prefetch_liveness_cache(w);
}




// Haoran: TODO
template <class T, bool CANCELLABLE>
void ShenandoahConcurrentPrefetch::mark_loop_work(T* cl, jushort* live_data, uint worker_id, ShenandoahTaskTerminator *terminator) {
  uintx stride = ShenandoahMarkLoopStride;

  ShenandoahHeap* heap = ShenandoahHeap::heap();
  ShenandoahObjToScanQueueSet* queues = task_queues();
  ShenandoahObjToScanQueue* q;
  ShenandoahObjToScanQueue* dupq;
  ShenandoahMarkTask task;

  /*
   * Process outstanding queues, if any.
   *
   * There can be more queues than workers. To deal with the imbalance, we claim
   * extra queues first. Since marking can push new tasks into the queue associated
   * with this worker id, we come back to process this queue in the normal loop.
   */
  assert(queues->get_reserved() == heap->prefetch_workers()->active_workers()*2,
         "Need to reserve proper number of queues: reserved: %u, active: %u", queues->get_reserved(), heap->workers()->active_workers());

  size_t prefetch_cnt = 0;
  size_t prefetch_size = 0;
  q = get_queue(worker_id * 2 );
  dupq = get_queue(worker_id * 2 + 1);
  q->clear();
  do {
    size_t index = prefetch_queue_index();
    JavaThreadIteratorWithHandle jtiwh;
    JavaThread *t;
    PrefetchQueue* prefetch_queue;
    while(index && _heap->concurrent_mark()->in_cm()) {
      t = jtiwh.next();
      if(t == NULL) {
          jtiwh.rewind();
          t = jtiwh.next();
      }
      index--;
    }
    bool get_queue = 0;
    while(_heap->concurrent_mark()->in_cm()) {
      if(t == NULL) {
        jtiwh.rewind();
        t = jtiwh.next();
      }
      if(t == NULL) {
        ShouldNotReachHere();
      }
      prefetch_queue = &ShenandoahThreadLocalData::prefetch_queue(t);
      if(jtiwh.list()->includes(t)&&prefetch_queue->set_in_processing()) {
          get_queue = 1;
          break;
      }
      t = jtiwh.next();
      if(t == NULL) {
          jtiwh.rewind();
          t = jtiwh.next();
      }
    }
    if(get_queue) {
      void* ptr;
      bool ret = prefetch_queue->dequeue(&ptr);
      while (ret && ptr != NULL && _heap->concurrent_mark()->in_cm()) {
        if(!ShenandoahHeap::heap()->is_in_reserved(ptr)) break;
      
        oop* p = (oop *) &ptr;
        if (_heap->has_forwarded_objects()) {
          ShenandoahConcurrentPrefetch::mark_through_ref<oop, RESOLVE, NO_DEDUP>(p, _heap, q, _heap->marking_context());
        } else {
          ShenandoahConcurrentPrefetch::mark_through_ref<oop, NONE, NO_DEDUP>(p, _heap, q, _heap->marking_context());
        }
        // bool success = task->make_reference_grey((oop)(HeapWord*)ptr);
        // if(success) {
        // log_debug(prefetch)("Succesfully mark one in PFTask!");
        // }
        ret = prefetch_queue->dequeue(&ptr);
      }
      prefetch_queue->release_processing();

      size_t max_num_objects = PrefetchNum;
      size_t max_size = PrefetchSize;
      size_t current_size = 0;

      for (uint i = 0; i < max_num_objects && current_size < max_size && i < stride && _heap->concurrent_mark()->in_cm(); i++) {
        if (q->pop(task)) {
          prefetch_cnt++;
          current_size += do_task<T>(q, cl, live_data, &task);
        } else {
          assert(q->is_empty(), "Must be empty");
          // q = queues->claim_next();
          break;
        }
      }
      prefetch_size += current_size;

      while(_heap->concurrent_mark()->in_cm() && q->pop(task)) {
        if(!heap->is_in_reserved(task.obj())) {
            ShouldNotReachHere();
        }
        shenandoah_assert_correct(NULL, task.obj());
        dupq->push(task);
      }
      // assert(q->is_empty(), "Must be empty");
      
      // Haoran: TODO
      // How to push other obj refs to global queues


      // task->do_marking_step();
      // _pf->do_yield_check();
    }
  } while (_heap->concurrent_mark()->in_cm());

  tty->print("worker_id: %u, PREFETCH_cnt: %lu, prefetch_size: %lu\n", worker_id, prefetch_cnt, prefetch_size);

}