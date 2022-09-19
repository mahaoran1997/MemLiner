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
#include "classfile/classLoaderDataGraph.hpp"
// #include "gc/g1/g1Analytics.hpp"
// #include "gc/g1/g1CollectedHeap.inline.hpp"
// #include "gc/g1/g1ConcurrentPrefetch.inline.hpp"
// #include "gc/g1/ShenandoahControlPrefetchThread.inline.hpp"
// #include "gc/g1/g1MMUTracker.hpp"
// #include "gc/g1/g1Policy.hpp"
// #include "gc/g1/g1RemSet.hpp"
// #include "gc/g1/g1VMOperations.hpp"
#include "gc/shared/concurrentGCPhaseManager.hpp"
#include "gc/shared/gcId.hpp"
#include "gc/shared/gcTrace.hpp"
#include "gc/shared/gcTraceTime.inline.hpp"
#include "gc/shared/suspendibleThreadSet.hpp"
#include "logging/log.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/vmThread.hpp"
#include "utilities/debug.hpp"

// #include "gc/shenandoah/shenandoahConcurrentPrefetch.inline.hpp"
#include "gc/shenandoah/shenandoahControlPrefetchThread.hpp"
#include "gc/shenandoah/shenandoahConcurrentMark.hpp"
#include "gc/shenandoah/shenandoahConcurrentPrefetch.inline.hpp"

// ======= Concurrent Prefetch Thread ========



ShenandoahControlPrefetchThread::ShenandoahControlPrefetchThread(ShenandoahConcurrentPrefetch* pf, ShenandoahControlThread* ct, ShenandoahConcurrentMark* cm) :
  ConcurrentGCThread(),
  _pf(pf),
  _ct(ct),
  _cm(cm),
  _state(Idle) {

  set_name("Shenandoah Main Prefetcher");
  create_and_start();
}

void ShenandoahControlPrefetchThread::run_service() {

  ShenandoahHeap* heap = ShenandoahHeap::heap();
    //   G1Policy* g1_policy = g1h->g1_policy();

  // G1ConcPhaseManager cpmanager(G1ConcurrentPhase::IDLE, this);

  while (!should_terminate()) {
    // wait until started is set.
    sleep_before_next_cycle();
    if (should_terminate()) {
      break;
    }
    {
    //   ResourceMark rm;
    //   HandleMark   hm;
      double cycle_start_time = os::elapsedTime();
      double cycle_start = os::elapsedVTime();
      // It would be nice to use the G1ConcPhase class here but
      // the "end" logging is inside the loop and not at the end of
      // a scope. Also, the timer doesn't support nesting.
      // Mimicking the same log output instead.
      {
        // G1ConcPhaseManager mark_manager(G1ConcurrentPhase::CONCURRENT_MARK, this);
        jlong mark_start = os::elapsed_counter();
        

        // Haoran: TODO while ct is in marking
        
        uint n_workers = heap->prefetch_workers()->update_active_workers(heap->max_prefetch_workers());
        log_info(gc, task)("Using %u of %u workers for %s",
          n_workers, ShenandoahHeap::heap()->max_prefetch_workers(), "Prefetch");
        while(_cm->in_cm()) {
        // Haoran: TODO
        // do prefetch
          _pf->mark_from_roots();
        }
      }

      double end_time = os::elapsedVTime();

      log_debug(prefetch)("PrefetchThread cycle %lf s", os::elapsedTime()-cycle_start_time);
    }
    // Update the number of full collections that have been
    // completed. This will also notify the FullGCCount_lock in case a
    // Java thread is waiting for a full GC to happen (e.g., it
    // called System.gc() with +ExplicitGCInvokesConcurrent).
    {
      SuspendibleThreadSetJoiner sts_join;
      set_idle();
    }
  }
}

void ShenandoahControlPrefetchThread::stop_service() {
  MutexLockerEx ml(CPF_lock, Mutex::_no_safepoint_check_flag);
  CPF_lock->notify_all();
}


// Need to set started first and then CPF_lock notify it.
// MutexLockerEx x(CGC_lock, Mutex::_no_safepoint_check_flag);
// _cm_thread->set_started();
// CGC_lock->notify();


void ShenandoahControlPrefetchThread::sleep_before_next_cycle() {
  // We join here because we don't want to do the "shouldConcurrentMark()"
  // below while the world is otherwise stopped.
  assert(!in_progress(), "should have been cleared");

  MutexLockerEx x(CPF_lock, Mutex::_no_safepoint_check_flag);
  while (!started() && !should_terminate()) {
    CPF_lock->wait(Mutex::_no_safepoint_check_flag);
  }

  if (started()) {
    set_in_progress();
  }
}
