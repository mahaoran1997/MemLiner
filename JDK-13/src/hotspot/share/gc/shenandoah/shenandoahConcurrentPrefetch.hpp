
#ifndef SHARE_VM_GC_SHENANDOAH_SHENANDOAHCONCURRENTPREFETCH_HPP
#define SHARE_VM_GC_SHENANDOAH_SHENANDOAHCONCURRENTPREFETCH_HPP

#include "gc/shared/taskqueue.hpp"
#include "gc/shenandoah/shenandoahOopClosures.hpp"
#include "gc/shenandoah/shenandoahPhaseTimings.hpp"
#include "gc/shenandoah/shenandoahTaskqueue.hpp"

// #include ""

class ShenandoahStrDedupQueue;

class ShenandoahConcurrentPrefetch: public CHeapObj<mtGC> {
private:
  ShenandoahHeap* _heap;
  ShenandoahObjToScanQueueSet* _task_queues;
  

public:
  bool _in_pf;
  void initialize(uint workers);
  void cancel();

// ---------- Marking loop and tasks
//
private:
  // Haoran: modify
  template <class T>
  inline size_t do_task(ShenandoahObjToScanQueue* q, T* cl, jushort* live_data, ShenandoahMarkTask* task);

  // Haoran: modify
  template <class T>
  inline size_t do_chunked_array_start(ShenandoahObjToScanQueue* q, T* cl, oop array);

  // Haoran: modify
  template <class T>
  inline size_t do_chunked_array(ShenandoahObjToScanQueue* q, T* cl, oop array, int chunk, int pow);

  inline void count_liveness(jushort* live_data, oop obj);

  template <class T, bool CANCELLABLE>
  void mark_loop_work(T* cl, jushort* live_data, uint worker_id, ShenandoahTaskTerminator *t);

  template <bool CANCELLABLE>
  void mark_loop_prework(uint worker_id, ShenandoahTaskTerminator *terminator, ReferenceProcessor *rp, bool strdedup);

public:
  void mark_loop(uint worker_id, ShenandoahTaskTerminator* terminator, ReferenceProcessor *rp,
                 bool cancellable, bool strdedup) {
    if (cancellable) {
      mark_loop_prework<true>(worker_id, terminator, rp, strdedup);
    } else {
      mark_loop_prework<false>(worker_id, terminator, rp, strdedup);
    }
  }

  template<class T, UpdateRefsMode UPDATE_REFS, StringDedupMode STRING_DEDUP>
  static inline void mark_through_ref(T* p, ShenandoahHeap* heap, ShenandoahObjToScanQueue* q, ShenandoahMarkingContext* const mark_context);

  void mark_from_roots();
public:
  ShenandoahObjToScanQueue* get_queue(uint worker_id);
  ShenandoahObjToScanQueueSet* task_queues() { return _task_queues; }

  // Haoran: modify
  size_t _current_queue_index;

  size_t prefetch_queue_index() { 
    _current_queue_index ++;
    JavaThreadIteratorWithHandle jtiwh;
    size_t length = (size_t) jtiwh.length();
    _current_queue_index %= length;
    return _current_queue_index;
  }

};

#endif // SHARE_VM_GC_SHENANDOAH_SHENANDOAHCONCURRENTPREFETCH_HPP
