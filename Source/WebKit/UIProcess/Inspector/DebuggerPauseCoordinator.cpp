/*
 * Copyright (C) 2026 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "DebuggerPauseCoordinator.h"

#include "FrameInspectorTarget.h"
#include "MessageSenderInlines.h"
#include "ProvisionalPageProxy.h"
#include "WebInspectorBackendMessages.h"
#include "WebPageProxy.h"
#include "WebProcessProxy.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {

WTF_MAKE_TZONE_ALLOCATED_IMPL(DebuggerPauseCoordinator);

DebuggerPauseCoordinator::DebuggerPauseCoordinator(WebPageProxy& page)
    : m_page(page)
{
}

DebuggerPauseCoordinator::~DebuggerPauseCoordinator()
{
    m_isDestroying = true;
    closeSession();
}

void DebuggerPauseCoordinator::frameDidPause(WebProcessProxy& process, WebCore::FrameIdentifier frameID)
{
    bool isNewStoppingFrame = m_stoppingFrames.add({ process.coreProcessIdentifier(), frameID }).isNewEntry;
    if (!m_sessionID)
        openSession();
    else if (isNewStoppingFrame)
        notifySessionChanged(true);
}

void DebuggerPauseCoordinator::frameDidContinue(WebProcessProxy& process, WebCore::FrameIdentifier frameID)
{
    if (!m_stoppingFrames.remove({ process.coreProcessIdentifier(), frameID }))
        return;
    if (m_stoppingFrames.isEmpty())
        closeSession();
    else
        notifySessionChanged(true);
}

// A frame that leaves its process, or is removed, may never report that it continued: the process can lose the
// page that would send the report, and stay alive.
void DebuggerPauseCoordinator::frameDidChangeProcess(WebCore::FrameIdentifier frameID, WebProcessProxy& newProcess)
{
    removeStoppingFramesIf([&](auto& entry) {
        return entry.second == frameID && entry.first != newProcess.coreProcessIdentifier();
    });
}

void DebuggerPauseCoordinator::frameWasRemoved(WebCore::FrameIdentifier frameID)
{
    removeStoppingFramesIf([&](auto& entry) {
        return entry.second == frameID;
    });
}

void DebuggerPauseCoordinator::processDidTerminate(WebProcessProxy& process)
{
    m_suspendedPages.removeAllMatching([&](auto& entry) {
        RefPtr suspendedProcess = entry.first.get();
        return !suspendedProcess || suspendedProcess == &process;
    });

    // A process that dies while one of its frames is paused never reports that the frame continued.
    removeStoppingFramesIf([&](auto& entry) {
        return entry.first == process.coreProcessIdentifier();
    });
}

template<typename Predicate>
void DebuggerPauseCoordinator::removeStoppingFramesIf(const Predicate& predicate)
{
    if (!m_stoppingFrames.removeIf(predicate))
        return;
    if (m_stoppingFrames.isEmpty())
        closeSession();
    else
        notifySessionChanged(true);
}

void DebuggerPauseCoordinator::openSession()
{
    ASSERT(!m_sessionID);
    m_sessionID = WebCore::DebuggerPauseSessionIdentifier::generate();

    Ref page = m_page.get();
    page->forEachWebContentProcess([&](WebProcessProxy& process, WebCore::PageIdentifier pageID) {
        pageWasAdded(process, pageID);
    });

    // forEachWebContentProcess() does not list a provisional page, which can commit while the session is open.
    if (RefPtr provisionalPage = page->provisionalPageProxy())
        pageWasAdded(provisionalPage->process(), provisionalPage->webPageID());

    notifySessionChanged(true);
}

void DebuggerPauseCoordinator::notifySessionChanged(bool isPaused)
{
    ASSERT(m_sessionID);
    if (m_isDestroying)
        return;

    Vector<String> stoppingTargetIDs;
    for (auto& [processID, frameID] : m_stoppingFrames)
        stoppingTargetIDs.append(FrameInspectorTarget::toTargetID(frameID, processID));
    std::ranges::sort(stoppingTargetIDs, WTF::codePointCompareLessThan);
    protect(m_page)->debuggerPauseSessionDidChange(*m_sessionID, isPaused, stoppingTargetIDs);
}

void DebuggerPauseCoordinator::pageWasAdded(WebProcessProxy& process, WebCore::PageIdentifier pageID)
{
    if (!m_sessionID)
        return;
    suspendPage(process, pageID);
}

void DebuggerPauseCoordinator::suspendPage(WebProcessProxy& process, WebCore::PageIdentifier pageID)
{
    // A provisional page can take over the identifier of a remote page the process already had, which may start from scratch,
    // so send the message every time and only track the page once.
    process.send(Messages::WebInspectorBackend::SuspendForDebugger(*m_sessionID), pageID);
    bool isTracked = m_suspendedPages.containsIf([&](auto& entry) {
        return entry.first.get() == &process && entry.second == pageID;
    });
    if (!isTracked)
        m_suspendedPages.append({ process, pageID });
}

void DebuggerPauseCoordinator::closeSession()
{
    if (!m_sessionID) {
        m_stoppingFrames.clear();
        return;
    }
    m_stoppingFrames.clear();
    notifySessionChanged(false);
    auto sessionID = std::exchange(m_sessionID, std::nullopt);

    for (auto& [weakProcess, pageID] : std::exchange(m_suspendedPages, { })) {
        if (RefPtr process = weakProcess.get())
            process->send(Messages::WebInspectorBackend::ResumeFromDebugger(*sessionID), pageID);
    }
}

} // namespace WebKit
