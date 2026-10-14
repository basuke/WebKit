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
#include "DebuggerSuspensionController.h"

#include "ActiveDOMObject.h"
#include "CommonVM.h"
#include "Document.h"
#include "FrameDestructionObserverInlines.h"
#include "InspectorBackendClient.h"
#include "LocalFrame.h"
#include "LocalFrameInlines.h"
#include "Page.h"
#include "PageInspectorController.h"
#include "ScriptController.h"
#include "Settings.h"
#include <JavaScriptCore/VM.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(DebuggerSuspensionController);

static bool s_isFrameDebuggerPaused;
static unsigned s_pageCountWithSessionHold;
static bool s_isInContinuation;

// The paused frame may be detached by the time the pause ends, so remember its page and identifier for the end-of-pause notification.
static WeakPtr<Page>& pausedPage()
{
    static NeverDestroyed<WeakPtr<Page>> page;
    return page;
}

static std::optional<FrameIdentifier> s_pausedFrameID;
static bool s_isWaitingForIdleToReportContinue;

static void reportFrameDebuggerDidContinue(Page* page, std::optional<FrameIdentifier> frameID)
{
    if (!page || !frameID)
        return;
    if (auto* client = page->inspectorController().inspectorBackendClient())
        client->frameDebuggerDidContinue(*frameID);
}

DebuggerSuspensionController::DebuggerSuspensionController(Page& page)
    : m_page(page)
{
}

DebuggerSuspensionController::~DebuggerSuspensionController() = default;

void DebuggerSuspensionController::frameDebuggerDidPause(LocalFrame& frame)
{
    ASSERT(!s_isFrameDebuggerPaused);
    s_isFrameDebuggerPaused = true;
    updateHeldDocumentsInAllPages();

    RefPtr previousPage = std::exchange(pausedPage(), nullptr).get();
    auto previousFrameID = std::exchange(s_pausedFrameID, std::nullopt);

    RefPtr page = frame.page();
    if (page && page->settings().siteIsolationGlobalDebuggerPauseEnabled()) {
        pausedPage() = page.get();
        s_pausedFrameID = frame.frameID();
        if (auto* client = page->inspectorController().inspectorBackendClient())
            client->frameDebuggerDidPause(frame.frameID());
    }

    // A previous pause whose end is still waiting for VM idle is over only if another frame paused; a step pauses the same frame again.
    if (previousFrameID != s_pausedFrameID)
        reportFrameDebuggerDidContinue(previousPage.get(), previousFrameID);
}

void DebuggerSuspensionController::frameDebuggerDidContinue()
{
    ASSERT(s_isFrameDebuggerPaused);
    s_isFrameDebuggerPaused = false;

    // The code that continues runs synchronously, and must be able to dispatch events into every page of the process, so a session hold does not apply until the VM is idle again.
    if (s_pageCountWithSessionHold && !s_isInContinuation) {
        s_isInContinuation = true;
        commonVM().whenIdle([] {
            s_isInContinuation = false;
            updateHeldDocumentsInAllPages();
        });
    }
    updateHeldDocumentsInAllPages();

    if (!s_pausedFrameID || s_isWaitingForIdleToReportContinue)
        return;

    // A step continues the frame and pauses it again before the VM goes idle, so report the end of the pause only then: reporting it now
    // would let the UI process resume the frames in other processes between steps. A step that leaves the VM, for example over an await, still ends the pause.
    s_isWaitingForIdleToReportContinue = true;
    commonVM().whenIdle([] {
        ASSERT(!s_isFrameDebuggerPaused);
        s_isWaitingForIdleToReportContinue = false;
        RefPtr page = std::exchange(pausedPage(), nullptr).get();
        reportFrameDebuggerDidContinue(page.get(), std::exchange(s_pausedFrameID, std::nullopt));
    });
}

void DebuggerSuspensionController::addSessionHold(DebuggerPauseSessionIdentifier sessionID)
{
    bool hadSessionHold = !m_sessionHolds.isEmpty();
    if (!m_sessionHolds.add(sessionID).isNewEntry)
        return;
    if (!hadSessionHold)
        ++s_pageCountWithSessionHold;
    updateHeldDocumentsInAllPages();
}

void DebuggerSuspensionController::removeSessionHold(DebuggerPauseSessionIdentifier sessionID)
{
    if (!m_sessionHolds.remove(sessionID))
        return;
    if (m_sessionHolds.isEmpty())
        --s_pageCountWithSessionHold;
    updateHeldDocumentsInAllPages();
}

void DebuggerSuspensionController::inspectedPageDestroyed()
{
    if (m_sessionHolds.isEmpty())
        return;
    m_sessionHolds.clear();
    --s_pageCountWithSessionHold;
    updateHeldDocumentsInAllPages();
}

void DebuggerSuspensionController::updateHeldDocumentsInAllPages()
{
    Page::forEachPage([](Page& page) {
        CheckedRef { page.inspectorController().debuggerSuspensionController() }->updateHeldDocuments();
    });
}

void DebuggerSuspensionController::documentDidBecomeCurrent(Document& document)
{
    if (shouldHoldDocuments())
        holdDocument(document);
}

void DebuggerSuspensionController::documentWillSuspendScheduledTasks(Document& document)
{
    releaseDocument(document);
}

void DebuggerSuspensionController::documentDidResumeScheduledTasks(Document& document)
{
    if (shouldHoldDocuments())
        holdDocument(document);
}

bool DebuggerSuspensionController::shouldHoldDocuments()
{
    return s_isFrameDebuggerPaused || (s_pageCountWithSessionHold && !s_isInContinuation);
}

void DebuggerSuspensionController::updateHeldDocuments()
{
    bool shouldHold = shouldHoldDocuments();
    Ref page = m_page.get();
    page->forEachLocalFrame([&](LocalFrame& frame) {
        RefPtr document = frame.document();
        if (!document)
            return;
        if (shouldHold)
            holdDocument(*document);
        else
            releaseDocument(*document);
    });

    if (shouldHold)
        return;

    // A held frame may have navigated to a document that was not held, so unpause frames rather than only the frames of held documents.
    for (Ref frame : std::exchange(m_pausedFrames, { }))
        CheckedRef { frame->script() }->setPaused(false);
}

void DebuggerSuspensionController::holdDocument(Document& document)
{
    if (m_heldDocuments.contains(document))
        return;

    // A document restored from the back/forward cache becomes current while it is still suspended for the cache, and a second suspension would be dropped.
    if (document.activeDOMObjectsAreSuspended())
        return;

    RefPtr frame = document.frame();
    if (!frame)
        return;

    CheckedRef script = frame->script();
    if (!script->canExecuteScripts(ReasonForCallingCanExecuteScripts::NotAboutToExecuteScript))
        return;

    m_heldDocuments.add(document);
    m_pausedFrames.add(*frame);
    script->setPaused(true);
    document.suspendScriptedAnimationControllerCallbacks();
    document.suspendActiveDOMObjects(ReasonForSuspension::JavaScriptDebuggerPaused);
}

void DebuggerSuspensionController::releaseDocument(Document& document)
{
    if (!m_heldDocuments.remove(document))
        return;

    document.resumeActiveDOMObjects(ReasonForSuspension::JavaScriptDebuggerPaused);
    document.resumeScriptedAnimationControllerCallbacks();
}

} // namespace WebCore
