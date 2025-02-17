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
 *      This file implements WatchableEventManager using libevent.
 */

#include <platform/CHIPDeviceBuildConfig.h>
#include <support/CodeUtils.h>
#include <system/SystemLayer.h>
#include <system/WatchableEventManager.h>
#include <system/WatchableSocket.h>

#if CHIP_DEVICE_CONFIG_ENABLE_MDNS && !__ZEPHYR__

namespace chip {
namespace Mdns {
void GetMdnsTimeout(timeval & timeout);
void HandleMdnsTimeout();
} // namespace Mdns
} // namespace chip

#endif // CHIP_DEVICE_CONFIG_ENABLE_MDNS && !__ZEPHYR__

#ifndef CHIP_CONFIG_LIBEVENT_DEBUG_CHECKS
#define CHIP_CONFIG_LIBEVENT_DEBUG_CHECKS 1 // TODO(#5556): default to off
#endif

namespace chip {
namespace System {

namespace {

System::SocketEvents SocketEventsFromLibeventFlags(short eventFlags)
{
    return System::SocketEvents()
        .Set(SocketEventFlags::kRead, eventFlags & EV_READ)
        .Set(SocketEventFlags::kWrite, eventFlags & EV_WRITE);
}

void TimeoutCallbackHandler(evutil_socket_t fd, short eventFlags, void * data)
{
    event * const ev = reinterpret_cast<event *>(data);
    evtimer_del(ev);
}

} // anonymous namespace

CHIP_ERROR WatchableEventManager::Init(System::Layer & systemLayer)
{
#if CHIP_CONFIG_LIBEVENT_DEBUG_CHECKS
    static bool enabled_event_debug_mode = false;
    if (!enabled_event_debug_mode)
    {
        enabled_event_debug_mode = true;
        event_enable_debug_mode();
    }
#endif // CHIP_CONFIG_LIBEVENT_DEBUG_CHECKS

    mEventBase     = event_base_new();
    mTimeoutEvent  = evtimer_new(mEventBase, TimeoutCallbackHandler, event_self_cbarg());
    mActiveSockets = nullptr;
    mSystemLayer   = &systemLayer;
    return CHIP_NO_ERROR;
}

void WatchableEventManager::PrepareEvents()
{
    // TODO(#5556): Integrate timer platform details with WatchableEventManager.
    timeval nextTimeout = { 0, 0 };
    PrepareEventsWithTimeout(nextTimeout);
}

void WatchableEventManager::PrepareEventsWithTimeout(struct timeval & nextTimeout)
{
    // TODO(#5556): Integrate timer platform details with WatchableEventManager.
    mSystemLayer->GetTimeout(nextTimeout);
    if (nextTimeout.tv_sec || nextTimeout.tv_usec)
    {
        evtimer_add(mTimeoutEvent, &nextTimeout);
    }
}

void WatchableEventManager::WaitForEvents()
{
    VerifyOrDie(mEventBase != nullptr);
    event_base_loop(mEventBase, EVLOOP_ONCE);
}

void WatchableEventManager::HandleEvents()
{
    mSystemLayer->HandleTimeout();

#if CHIP_DEVICE_CONFIG_ENABLE_MDNS && !__ZEPHYR__
    chip::Mdns::HandleMdnsTimeout();
#endif // CHIP_DEVICE_CONFIG_ENABLE_MDNS && !__ZEPHYR__

    while (mActiveSockets != nullptr)
    {
        WatchableSocket * const watcher = mActiveSockets;
        mActiveSockets                  = watcher->mActiveNext;
        watcher->InvokeCallback();
    }
}

CHIP_ERROR WatchableEventManager::Shutdown()
{
    event_base_loopbreak(mEventBase);
    event_free(mTimeoutEvent);
    mTimeoutEvent = nullptr;
    event_base_free(mEventBase);
    mEventBase = nullptr;
    return CHIP_NO_ERROR;
}

void WatchableEventManager::Signal()
{
    /*
     * Wake up the I/O thread by writing a single byte to the wake pipe.
     *
     * If p WakeIOThread() is being called from within an I/O event callback, then writing to the wake pipe can be skipped,
     * since the I/O thread is already awake.
     *
     * Furthermore, we don't care if this write fails as the only reasonably likely failure is that the pipe is full, in which
     * case the select calling thread is going to wake up anyway.
     */
#if CHIP_SYSTEM_CONFIG_POSIX_LOCKING
    if (pthread_equal(mSystemLayer->mHandleSelectThread, pthread_self()))
    {
        return;
    }
#endif // CHIP_SYSTEM_CONFIG_POSIX_LOCKING

    // Send notification to wake up the select call.
    CHIP_ERROR status = mWakeEvent.Notify();
    if (status != CHIP_NO_ERROR)
    {
        ChipLogError(chipSystemLayer, "System wake event notify failed: %" CHIP_ERROR_FORMAT, ChipError::FormatError(status));
    }
}

// static
void WatchableEventManager::LibeventCallbackHandler(evutil_socket_t fd, short eventFlags, void * data)
{
    WatchableSocket * const watcher = reinterpret_cast<WatchableSocket *>(data);
    VerifyOrDie(watcher != nullptr);
    VerifyOrDie(watcher->mFD == fd);

    watcher->mPendingIO = SocketEventsFromLibeventFlags(eventFlags);

    // Add to active list.
    WatchableSocket ** pp = &watcher->mSharedState->mActiveSockets;
    while (*pp != nullptr)
    {
        if (*pp == watcher)
        {
            return;
        }
        pp = &(*pp)->mActiveNext;
    }
    *pp                  = watcher;
    watcher->mActiveNext = nullptr;
}

void WatchableEventManager::RemoveFromQueueIfPresent(WatchableSocket * watcher)
{
    VerifyOrDie(watcher != nullptr);
    VerifyOrDie(watcher->mSharedState == this);

    WatchableSocket ** pp = &mActiveSockets;
    while (*pp != nullptr)
    {
        if (*pp == watcher)
        {
            *pp = watcher->mActiveNext;
            return;
        }
        pp = &(*pp)->mActiveNext;
    }
}

} // namespace System
} // namespace chip
