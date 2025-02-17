/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *      This file declares an implementation of WatchableEvents using libevent.
 */

#pragma once

#if !INCLUDING_CHIP_SYSTEM_WATCHABLE_EVENT_MANAGER_CONFIG_FILE
#error "This file should only be included from <system/WatchableEventManager.h>"
#include <system/WatchableEventManager.h>
#endif //  !INCLUDING_CHIP_SYSTEM_WATCHABLE_EVENT_MANAGER_CONFIG_FILE

#include <event2/event.h>

namespace chip {

namespace System {

class WatchableEventManager
{
public:
    WatchableEventManager() : mActiveSockets(nullptr), mSystemLayer(nullptr), mEventBase(nullptr), mTimeoutEvent(nullptr) {}
    CHIP_ERROR Init(Layer & systemLayer);
    CHIP_ERROR Shutdown();
    void Signal();

    void EventLoopBegins() {}
    void PrepareEvents();
    void WaitForEvents();
    void HandleEvents();
    void EventLoopEnds() {}

    // TODO(#5556): Some unit tests supply a timeout at low level, due to originally using select(); these should a proper timer.
    void PrepareEventsWithTimeout(timeval & nextTimeout);

private:
    /*
     * In this implementation, libevent invokes LibeventCallbackHandler from beneath WaitForEvents(),
     * which means that the CHIP stack is unlocked. LibeventCallbackHandler adds the WatchableSocket
     * to a queue (implemented as a simple intrusive list to avoid dynamic memory allocation), and
     * then HandleEvents() invokes the WatchableSocket callbacks.
     */
    friend class WatchableSocket;
    static void LibeventCallbackHandler(evutil_socket_t fd, short eventFlags, void * data);
    void RemoveFromQueueIfPresent(WatchableSocket * watcher);
    WatchableSocket * mActiveSockets; ///< List of sockets activated by libevent.

    Layer * mSystemLayer;
    event_base * mEventBase; ///< libevent shared state.
    event * mTimeoutEvent;

    WakeEvent mWakeEvent;
};

} // namespace System
} // namespace chip
