
//#include <system_error>
#include <cstdio>
#include <stdexcept>

extern "C" {
#include "irq.h"
#include "sched.h"
#include "vtimer.h"
#include "priority_queue.h"
}

#include "caf/condition_variable.hpp"

using namespace std::chrono;

namespace caf {

condition_variable::~condition_variable() {
  m_queue.first = NULL;
}

void condition_variable::notify_one() noexcept {
  unsigned old_state = disableIRQ();
  priority_queue_node_t *head = priority_queue_remove_head(&m_queue);
  int other_prio = -1;
  if (head != NULL) {
    tcb_t *other_thread = (tcb_t *) sched_threads[head->data];
    if (other_thread) {
      other_prio = other_thread->priority;
      sched_set_status(other_thread, STATUS_PENDING);
    }
    head->data = -1u;
  }
  restoreIRQ(old_state);
  if (other_prio >= 0) {
    sched_switch(other_prio);
  }
}

void condition_variable::notify_all() noexcept {
  unsigned old_state = disableIRQ();
  int other_prio = -1;
  while (true) {
    priority_queue_node_t *head = priority_queue_remove_head(&m_queue);
    if (head == NULL) {
      break;
    }
    tcb_t *other_thread = (tcb_t *) sched_threads[head->data];
    if (other_thread) {
      auto max_prio = [](int a, int b) {
        return (a < 0) ? b : ((a < b) ? a : b);
      };
      other_prio = max_prio(other_prio, other_thread->priority);
      sched_set_status(other_thread, STATUS_PENDING);
    }
    head->data = -1u;
  }
  restoreIRQ(old_state);
  if (other_prio >= 0) {
    sched_switch(other_prio);
  }
}

void condition_variable::wait(unique_lock<mutex>& lock) noexcept {
  if (!lock.owns_lock()) {
    throw std::runtime_error("condition_variable::wait: mutex not locked");
  }
  priority_queue_node_t n;
  n.priority = sched_active_thread->priority;
  n.data = sched_active_pid;
  n.next = NULL;
  // the signaling thread may not hold the mutex, the queue is not thread safe
  unsigned old_state = disableIRQ();
  priority_queue_add(&m_queue, &n);
  restoreIRQ(old_state);
  mutex_unlock_and_sleep(lock.mutex()->native_handle());
  if (n.data != -1u) {
    // on signaling n.data is set to -1u
    // if it isn't set, then the wakeup is either spurious or a timer wakeup
    old_state = disableIRQ();
    priority_queue_remove(&m_queue, &n);
    restoreIRQ(old_state);
  }
  mutex_lock(lock.mutex()->native_handle());
}

} // namespace caf