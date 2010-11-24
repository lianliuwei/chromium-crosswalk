// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// ExecutorsManager implementation.

#include "ceee/ie/broker/executors_manager.h"

#include "base/logging.h"
#include "ceee/ie/broker/broker_module_util.h"
#include "ceee/common/com_utils.h"

namespace {

// The timeout we set before accepting a failure when we wait for events.
const DWORD kTimeOut = 20000;

// Utility class to ensure the setting of an event before we exit a method.
class AutoSetEvent {
 public:
  explicit AutoSetEvent(HANDLE event_handle) : event_handle_(event_handle) {}
  ~AutoSetEvent() { ::SetEvent(event_handle_); }
 private:
  HANDLE event_handle_;
};

// A cached handle to our current process which we use when we call
// DuplicateHandle. GetCurrentProcess returns a pseudo HANDLE that doesn't
// need to be closed.
const HANDLE kProcessHandle = ::GetCurrentProcess();

}  // namespace


const size_t ExecutorsManager::kTerminationHandleIndexOffset = 0;
const size_t ExecutorsManager::kUpdateHandleIndexOffset = 1;
const size_t ExecutorsManager::kLastHandleIndexOffset =
    kUpdateHandleIndexOffset;
const size_t ExecutorsManager::kExtraHandles = kLastHandleIndexOffset + 1;

ExecutorsManager::ExecutorsManager(bool no_thread)
    : update_threads_list_gate_(::CreateEvent(NULL, FALSE, FALSE, NULL)),
      // Termination is manual reset. When we're terminated... We're terminated!
      termination_gate_(::CreateEvent(NULL, TRUE, FALSE, NULL)) {
  DCHECK(update_threads_list_gate_ != NULL);
  DCHECK(termination_gate_ != NULL);

  if (!no_thread) {
    ThreadStartData thread_start_data;
    thread_start_data.me = this;
    // Again, manual reset, because when we are started... WE ARE STARTED!!! :-)
    thread_start_data.thread_started_gate.Attach(::CreateEvent(NULL, TRUE,
                                                               FALSE, NULL));
    DCHECK(thread_start_data.thread_started_gate != NULL);

    // Since we provide the this pointer to the thread, we must make sure to
    // keep ALL INITIALIZATION CODE ABOVE THIS LINE!!!
    thread_.Attach(::CreateThread(NULL, 0, ThreadProc, &thread_start_data,
                                  0, 0));
    DCHECK(thread_ != NULL);

    // Make sure the thread is ready before continuing
    DWORD result = WaitForSingleObject(thread_start_data.thread_started_gate,
                                       kTimeOut);
    DCHECK(result == WAIT_OBJECT_0);
  }
}

HRESULT ExecutorsManager::RegisterTabExecutor(ThreadId thread_id,
                                              IUnknown* executor) {
  // We will need to know outside of the lock if the map was empty or not.
  // This way we can add a ref to the module for the existence of the map.
  bool map_was_empty = false;
  {
    AutoLock lock(lock_);
    map_was_empty = executors_.empty();
    if (!map_was_empty && executors_.find(thread_id) != executors_.end()) {
      return S_OK;
    }
    CHandle thread_handle(::OpenThread(SYNCHRONIZE, FALSE, thread_id));
    if (thread_handle == NULL) {
      DCHECK(false) << "Can't Open thread: " << thread_id;
      return E_UNEXPECTED;
    }
    ExecutorInfo& new_executor_info = executors_[thread_id];
    new_executor_info.executor = executor;
    new_executor_info.thread_handle = thread_handle;
  }  // End of lock.

  if (map_was_empty) {
    // We go from empty to not empty,
    // so lock the module to make sure we stay alive.
    ceee_module_util::LockModule();
  }
  return S_OK;
}

HRESULT ExecutorsManager::RegisterWindowExecutor(ThreadId thread_id,
                                                 IUnknown* executor) {
  // We need to fetch the event handle associated to this thread ID from
  // our map in a thread safe way...
  CHandle executor_registration_gate;
  {
    AutoLock lock(lock_);
    if (executors_.find(thread_id) != executors_.end()) {
      DCHECK(false) << "Unexpected registered thread_id: " << thread_id;
      return E_UNEXPECTED;
    }

    Tid2Event::iterator iter = pending_registrations_.find(thread_id);
    if (iter == pending_registrations_.end() || executor == NULL) {
      DCHECK(false) << "Invalid thread_id: " << thread_id <<
          ", or NULL executor.";
      return E_INVALIDARG;
    }
    // Make sure we use a duplicate handle so that we don't get caught setting
    // a dead handle when we exit, in case the other thread wakes up because
    // of a (unlikely) double registration.
    BOOL success = ::DuplicateHandle(
        kProcessHandle, iter->second.m_h, kProcessHandle,
        &executor_registration_gate.m_h, 0, FALSE, DUPLICATE_SAME_ACCESS);
    DCHECK(success) << com::LogWe();
  }  // End of lock.

  // We must make sure to wake up the thread(s) that might be waiting on us.
  // But only when we are done.
  AutoSetEvent auto_set_event(executor_registration_gate);

  // Try to get a handle to this thread right away so that we can do the rest
  // atomically. We need it to wake us up when it dies.
  CHandle thread_handle(::OpenThread(SYNCHRONIZE, FALSE, thread_id));
  if (thread_handle == NULL) {
    DCHECK(false) << "Can't Open thread: " << thread_id;
    return S_FALSE;
  }

  // We will need to know outside of the lock if the map was empty or not.
  // This way we can add a ref to the module for the existence of the map.
  bool map_was_empty = false;
  {
    AutoLock lock(lock_);
    map_was_empty = executors_.empty();
    // We should not get here if we already have an executor for that thread.
    DCHECK(executors_.find(thread_id) == executors_.end());
    ExecutorInfo& new_executor_info = executors_[thread_id];
    new_executor_info.executor = executor;
    new_executor_info.thread_handle = thread_handle;
  }  // End of lock.

  if (map_was_empty) {
    // We go from empty to not empty,
    // so lock the module to make sure we stay alive.
    ceee_module_util::LockModule();
  }

  // Update the list of handles that our thread is waiting on.
  BOOL success = ::SetEvent(update_threads_list_gate_);
  DCHECK(success);
  return S_OK;
}

HRESULT ExecutorsManager::GetExecutor(ThreadId thread_id, HWND window,
                                      REFIID riid, void** executor) {
  DCHECK(executor != NULL);
  // We may need to wait for either a currently pending
  // or own newly created registration of a new executor.
  CHandle executor_registration_gate;

  // We need to remember if we must create a new one or not.
  // But we must create the executor creator outside of the lock.
  bool create_executor = false;
  {
    AutoLock lock(lock_);
    ExecutorsMap::iterator exec_iter = executors_.find(thread_id);
    if (exec_iter != executors_.end()) {
      // Found it... We're done... That was quick!!! :-)
      DCHECK(exec_iter->second.executor != NULL);
      return exec_iter->second.executor->QueryInterface(riid, executor);
    }

    // Check if we need to wait for a pending registration.
    Tid2Event::iterator event_iter = pending_registrations_.find(thread_id);
    if (event_iter == pending_registrations_.end()) {
      // No pending registration, so we will need to create a new executor.
      create_executor = true;

      // Use the thread id as a cookie to only allow known threads to register.
      // Also use it to map to a new event we will use to signal the end of this
      // registration. We use a manual reset event so that more than one thread
      // can wait for it, and once we're done... we're done... period! :-)
      executor_registration_gate.Attach(::CreateEvent(NULL, TRUE, FALSE, NULL));
      DCHECK(executor_registration_gate != NULL);
      CHandle& new_registration_handle = pending_registrations_[thread_id];
      // Make sure we use a duplicate handle so that we don't get caught waiting
      // on a dead handle later, in case other threads wake up before we do and
      // close the handle before we wake up.
      BOOL success = ::DuplicateHandle(
          kProcessHandle, executor_registration_gate, kProcessHandle,
          &new_registration_handle.m_h, 0, FALSE, DUPLICATE_SAME_ACCESS);
      DCHECK(success) << com::LogWe();
    } else {
      // Same comment as above...
      BOOL success = ::DuplicateHandle(
          kProcessHandle, event_iter->second.m_h, kProcessHandle,
          &executor_registration_gate.m_h, 0, FALSE, DUPLICATE_SAME_ACCESS);
      DCHECK(success) << com::LogWe();
    }
  }  // End of lock.

  CComPtr<ICeeeExecutorCreator> executor_creator;
  if (create_executor) {
    // We need to create an executor creator so that the code setting up
    // a Windows Hook in the other process, runs from a DLL that can be
    // injected in that other process... WE are running in an executable.
    HRESULT hr = GetExecutorCreator(&executor_creator);
    DCHECK(SUCCEEDED(hr) && executor_creator != NULL) <<
          "CoCreating Executor Creator. " << com::LogHr(hr);
    hr = executor_creator->CreateWindowExecutor(thread_id,
        reinterpret_cast<CeeeWindowHandle>(window));
    if (FAILED(hr)) {
      // This could happen if the thread we want to hook to died prematurely.
      AutoLock lock(lock_);
      pending_registrations_.erase(thread_id);
      return hr;
    }
  }

  // Wait for the registration to complete.
  DWORD result = WaitForSingleObject(executor_registration_gate, kTimeOut);
  LOG_IF(INFO, result != WAIT_OBJECT_0) << "Registration problem? " <<
      "Wait Result: " << com::LogWe(result);

  // Let the executor creator know that we got the registration
  // and it can tear down what was needed to trigger it.
  if (executor_creator != NULL) {
    HRESULT hr = executor_creator->Teardown(thread_id);
    DCHECK(SUCCEEDED(hr)) << "Tearing down executor creator" << com::LogHr(hr);
  }

  // Do our own cleanup and return a reference thread safely...
  AutoLock lock(lock_);
  pending_registrations_.erase(thread_id);
  ExecutorsMap::iterator iter = executors_.find(thread_id);
  if (iter == executors_.end()) {
    DCHECK(false) << "New executor registration failed.";
    return E_UNEXPECTED;
  }

  DCHECK(iter->second.executor != NULL);
  return iter->second.executor->QueryInterface(riid, executor);
}

HRESULT ExecutorsManager::RemoveExecutor(ThreadId thread_id) {
  // Make sure to Release the executor outside the lock.
  CComPtr<IUnknown> dead_executor;
  bool map_is_empty = false;
  {
    AutoLock lock(lock_);
    ExecutorsMap::iterator iter = executors_.find(thread_id);
    if (iter == executors_.end()) {
      return S_FALSE;
    }

    dead_executor.Attach(iter->second.executor.Detach());
    executors_.erase(iter);
    map_is_empty = executors_.empty();
  }  // End of lock.

  if (map_is_empty) {
    // We go from not empty to empty,
    // so unlock the module it can leave in peace.
    ceee_module_util::UnlockModule();
  }
  return S_OK;
}

HRESULT ExecutorsManager::Terminate() {
  if (thread_ != NULL) {
    // Ask our thread to quit and wait for it to be done.
    DWORD result = ::SignalObjectAndWait(termination_gate_, thread_, kTimeOut,
                                         FALSE);
    DCHECK(result == WAIT_OBJECT_0);
    thread_.Close();
  }
  if (!executors_.empty()) {
    // TODO(mad@chromium.org): Can this happen???
    NOTREACHED();
    ceee_module_util::UnlockModule();
  }

  executors_.clear();
  update_threads_list_gate_.Close();
  termination_gate_.Close();

  return S_OK;
}

void ExecutorsManager::SetTabIdForHandle(int tab_id, HWND handle) {
  AutoLock lock(lock_);
  if (tab_id_map_.end() != tab_id_map_.find(tab_id) ||
      handle_map_.end() != handle_map_.find(handle)) {
    // Avoid double-setting of tab id -> handle mappings, which could otherwise
    // lead to inconsistencies. In practice, this should never happen.
    NOTREACHED();
    return;
  }
  if (handle == reinterpret_cast<HWND>(INVALID_HANDLE_VALUE) ||
      tab_id == kInvalidChromeSessionId) {
    NOTREACHED();
    return;
  }
  // A tool band tab ID should not be registered with this function.
  DCHECK(tool_band_id_map_.end() == tool_band_id_map_.find(tab_id));

  tab_id_map_[tab_id] = handle;
  handle_map_[handle] = tab_id;
}

void ExecutorsManager::SetTabToolBandIdForHandle(int tool_band_id,
                                                 HWND handle) {
  AutoLock lock(lock_);
  if (tool_band_id_map_.end() != tool_band_id_map_.find(tool_band_id) ||
      tool_band_handle_map_.end() != tool_band_handle_map_.find(handle)) {
    // Avoid double-setting of tool band id -> handle mappings, which could
    // otherwise lead to inconsistencies. In practice, this should never
    // happen.
    NOTREACHED();
    return;
  }
  if (handle == reinterpret_cast<HWND>(INVALID_HANDLE_VALUE) ||
      tool_band_id == kInvalidChromeSessionId) {
    NOTREACHED();
    return;
  }
  // A BHO tab ID should not be registered with this function.
  DCHECK(tab_id_map_.end() == tab_id_map_.find(tool_band_id));

  tool_band_id_map_[tool_band_id] = handle;
  tool_band_handle_map_[handle] = tool_band_id;
}

void ExecutorsManager::DeleteTabHandle(HWND handle) {
  AutoLock lock(lock_);
  HandleMap::iterator handle_it = handle_map_.find(handle);
  DCHECK(handle_map_.end() != handle_it);
  if (handle_map_.end() != handle_it) {
    TabIdMap::iterator tab_id_it = tab_id_map_.find(handle_it->second);
    DCHECK(tab_id_map_.end() != tab_id_it);
    if (tab_id_map_.end() != tab_id_it) {
#ifdef DEBUG
      tab_id_map_[handle_it->second] =
          reinterpret_cast<HWND>(INVALID_HANDLE_VALUE);
      handle_map_[handle] = kInvalidChromeSessionId;
#else
      tab_id_map_.erase(handle_it->second);
      handle_map_.erase(handle);
#endif  // DEBUG
    }
  }

  handle_it = tool_band_handle_map_.find(handle);
  if (tool_band_handle_map_.end() != handle_it) {
    TabIdMap::iterator tool_band_id_it =
        tool_band_id_map_.find(handle_it->second);
    DCHECK(tool_band_id_map_.end() != tool_band_id_it);
    if (tool_band_id_map_.end() != tool_band_id_it) {
#ifdef DEBUG
      tool_band_id_map_[handle_it->second] =
          reinterpret_cast<HWND>(INVALID_HANDLE_VALUE);
      tool_band_handle_map_[handle] = kInvalidChromeSessionId;
#else
      tool_band_id_map_.erase(handle_it->second);
      tool_band_handle_map_.erase(handle);
#endif  // DEBUG
    }
  }
}

HWND ExecutorsManager::GetTabHandleFromId(int tab_id) {
  AutoLock lock(lock_);
  TabIdMap::const_iterator it = tab_id_map_.find(tab_id);
  DCHECK(it != tab_id_map_.end());

  if (it == tab_id_map_.end())
    return reinterpret_cast<HWND>(INVALID_HANDLE_VALUE);

  // Deleted? I hope not.
  DCHECK(it->second != reinterpret_cast<HWND>(INVALID_HANDLE_VALUE));
  return it->second;
}

int ExecutorsManager::GetTabIdFromHandle(HWND tab_handle) {
  AutoLock lock(lock_);
  HandleMap::const_iterator it = handle_map_.find(tab_handle);
  DCHECK(it != handle_map_.end());
  if (it == handle_map_.end())
    return kInvalidChromeSessionId;
  DCHECK(it->second != kInvalidChromeSessionId);  // Deleted? I hope not.
  return it->second;
}

HWND ExecutorsManager::GetTabHandleFromToolBandId(int tool_band_id) {
  AutoLock lock(lock_);
  TabIdMap::const_iterator it = tool_band_id_map_.find(tool_band_id);

  if (it == tool_band_id_map_.end())
    return reinterpret_cast<HWND>(INVALID_HANDLE_VALUE);

  return it->second;
}

HRESULT ExecutorsManager::GetExecutorCreator(
    ICeeeExecutorCreator** executor_creator) {
  return ::CoCreateInstance(CLSID_CeeeExecutorCreator, NULL,
      CLSCTX_INPROC_SERVER, IID_ICeeeExecutorCreator,
      reinterpret_cast<void**>(executor_creator));
}

size_t ExecutorsManager::GetThreadHandles(
    CHandle thread_handles[], ThreadId thread_ids[], size_t num_threads) {
  AutoLock lock(lock_);
  ExecutorsMap::iterator iter = executors_.begin();
  size_t index = 0;
  for (; index < num_threads && iter != executors_.end(); ++index, ++iter) {
    DCHECK(thread_handles[index].m_h == NULL);
    // We need to duplicate the handle to make sure the caller will not wait
    // on a closed handle.
    BOOL success = ::DuplicateHandle(
        kProcessHandle, iter->second.thread_handle, kProcessHandle,
        &thread_handles[index].m_h, 0, FALSE, DUPLICATE_SAME_ACCESS);
    DCHECK(success) << com::LogWe();
    thread_ids[index] = iter->first;
  }

  return index;
}

DWORD ExecutorsManager::WaitForSingleObject(HANDLE wait_handle, DWORD timeout) {
  return ::WaitForSingleObject(wait_handle, timeout);
}

DWORD ExecutorsManager::WaitForMultipleObjects(DWORD num_handles,
    const HANDLE* wait_handles, BOOL wait_all, DWORD timeout) {
  return ::WaitForMultipleObjects(num_handles, wait_handles, wait_all, timeout);
}


DWORD ExecutorsManager::ThreadProc(LPVOID parameter) {
  // We must make sure to join the multi thread apartment so that the executors
  // get released properly in the same apartment they were acquired from.
  ::CoInitializeEx(NULL, COINIT_MULTITHREADED);

  ThreadStartData* thread_start_data =
      reinterpret_cast<ThreadStartData*>(parameter);
  DCHECK(thread_start_data != NULL);
  ExecutorsManager* me = thread_start_data->me;
  DCHECK(me != NULL);

  // Let our parent know that we are old enough now!
  ::SetEvent(thread_start_data->thread_started_gate);
  // Setting the event will destroy the thread start data living on the stack
  // so make sure we don't use it anymore.
  thread_start_data = NULL;

  while (true) {
    CHandle smart_handles[MAXIMUM_WAIT_OBJECTS];
    HANDLE handles[MAXIMUM_WAIT_OBJECTS];
    ThreadId thread_ids[MAXIMUM_WAIT_OBJECTS];
    // Get as many handles as we can, leaving room for kExtraHandles.
    size_t num_threads = me->GetThreadHandles(
        smart_handles, thread_ids, MAXIMUM_WAIT_OBJECTS - kExtraHandles);
    // Wait function needs an array of raw handles, not smart ones.
    for (size_t index = 0; index < num_threads; ++index)
      handles[index] = smart_handles[index];

    // We also need to wait for our termination signal.
    handles[num_threads + kTerminationHandleIndexOffset] =
        me->termination_gate_;
    // As well as a signal warning us to go fetch more thread handles.
    handles[num_threads + kUpdateHandleIndexOffset] =
        me->update_threads_list_gate_;

    size_t num_handles = num_threads + kExtraHandles;
    DWORD result = me->WaitForMultipleObjects(num_handles, handles, FALSE,
                                              INFINITE);
    if (result == WAIT_OBJECT_0 + num_threads +
                  kUpdateHandleIndexOffset) {
      // We got a new thread added,
      // simply let the loop turn to add it to our watch list.
    } else if (result >= WAIT_OBJECT_0 &&
               result < WAIT_OBJECT_0 + num_threads) {
      // One of our threads have died, cleanup time.
      me->RemoveExecutor(thread_ids[result - WAIT_OBJECT_0]);
    } else if (result == WAIT_OBJECT_0 + num_threads +
               kTerminationHandleIndexOffset) {
      // we are being terminated, break the cycle.
      break;
    } else {
      DCHECK(result == WAIT_FAILED);
      LOG(ERROR) << "ExecutorsManager::ThreadProc " << com::LogWe();
      break;
    }
  }
  // Merci... Bonsoir...
  ::CoUninitialize();
  return 1;
}
