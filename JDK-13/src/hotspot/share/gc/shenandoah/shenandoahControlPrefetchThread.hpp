
#ifndef SHARE_VM_GC_SHENANDOAH_SHENANDOAHCONTROLPREFETCHTHREAD_HPP
#define SHARE_VM_GC_SHENANDOAH_SHENANDOAHCONTROLPREFETCHTHREAD_HPP

#include "gc/shared/concurrentGCPhaseManager.hpp"
#include "gc/shared/concurrentGCThread.hpp"
#include "gc/shenandoah/shenandoahControlThread.hpp"

class ShenandoahConcurrentPrefetch;
// class G1Policy;

// The concurrent mark thread triggers the various steps of the concurrent marking
// cycle, including various marking cleanup.
class ShenandoahControlPrefetchThread: public ConcurrentGCThread {
  friend class VMStructs;



  ShenandoahConcurrentPrefetch* _pf;
  ShenandoahControlThread* _ct;
  ShenandoahConcurrentMark* _cm;


  enum State {
    Idle,
    Started,
    InProgress
  };

  volatile State _state;


  void sleep_before_next_cycle();

  void run_service();
  void stop_service();

 public:
  // Constructor
  ShenandoahControlPrefetchThread(ShenandoahConcurrentPrefetch* pf, ShenandoahControlThread* ct, ShenandoahConcurrentMark* cm);
  
  ShenandoahConcurrentPrefetch* pf()   { return _pf; }
  ShenandoahControlThread* ct()   { return _ct; }


  void set_idle()          { assert(_state != Started, "must not be starting a new cycle"); _state = Idle; }
  bool idle()              { return _state == Idle; }
  void set_started()       { assert(_state == Idle, "cycle in progress"); _state = Started; }
  bool started()           { return _state == Started; }
  void set_in_progress()   { assert(_state == Started, "must be starting a cycle"); _state = InProgress; }
  bool in_progress()       { return _state == InProgress; }

  // Returns true from the moment a marking cycle is
  // initiated (during the initial-mark pause when started() is set)
  // to the moment when the cycle completes (just after the next
  // marking bitmap has been cleared and in_progress() is
  // cleared). While during_cycle() is true we will not start another cycle
  // so that cycles do not overlap. We cannot use just in_progress()
  // as the CM thread might take some time to wake up before noticing
  // that started() is set and set in_progress().
  bool during_cycle()      { return !idle(); }


  // ConcurrentGCPhaseManager::Stack* phase_manager_stack() {
  //   return &_phase_manager_stack;
  // }
};

#endif // SHARE_VM_GC_SHENANDOAH_SHENANDOAHCONTROLPREFETCHTHREAD_HPP
