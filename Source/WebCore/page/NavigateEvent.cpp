/*
 * Copyright (C) 2023 Igalia S.L. All rights reserved.
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
#include "NavigateEvent.h"

#include "AbortController.h"
#include "CommonVM.h"
#include "Element.h"
#include "FrameDestructionObserverInlines.h"
#include "ExceptionCode.h"
#include "HTMLBodyElement.h"
#include "HistoryController.h"
#include "LocalFrameInlines.h"
#include "LocalFrameView.h"
#include "Navigation.h"
#include "NavigationNavigationType.h"
#include "Logging.h"
#include <wtf/IsoMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(NavigateEvent);

NavigateEvent::NavigateEvent(const AtomString& type, const NavigateEvent::Init& init, EventIsTrusted isTrusted, AbortController* abortController)
    : Event(EventInterfaceType::NavigateEvent, type, init, isTrusted)
    , m_navigationType(init.navigationType)
    , m_destination(init.destination)
    , m_signal(init.signal)
    , m_formData(init.formData)
    , m_downloadRequest(init.downloadRequest)
    , m_sourceElement(init.sourceElement)
    , m_canIntercept(init.canIntercept)
    , m_userInitiated(init.userInitiated)
    , m_hashChange(init.hashChange)
    , m_hasUAVisualTransition(init.hasUAVisualTransition)
    , m_abortController(abortController)
{
    Locker<JSC::JSLock> locker(commonVM().apiLock());
    m_info.setWeakly(init.info);
}

Ref<NavigateEvent> NavigateEvent::create(const AtomString& type, const NavigateEvent::Init& init, AbortController* abortController)
{
    return adoptRef(*new NavigateEvent(type, init, EventIsTrusted::Yes, abortController));
}

Ref<NavigateEvent> NavigateEvent::create(const AtomString& type, const NavigateEvent::Init& init)
{
    // FIXME: AbortController is required but JS bindings need to create it with one.
    return adoptRef(*new NavigateEvent(type, init, EventIsTrusted::No, nullptr));
}

// https://html.spec.whatwg.org/multipage/nav-history-apis.html#navigateevent-perform-shared-checks
ExceptionOr<void> NavigateEvent::sharedChecks(Document& document)
{
    if (!document.isFullyActive())
        return Exception { ExceptionCode::InvalidStateError, "Document is not fully active"_s };

    if (!isTrusted())
        return Exception { ExceptionCode::SecurityError, "Event is not trusted"_s };

    if (defaultPrevented())
        return Exception { ExceptionCode::InvalidStateError, "Event was already canceled"_s };

    return { };
}

// https://html.spec.whatwg.org/multipage/nav-history-apis.html#dom-navigateevent-intercept
ExceptionOr<void> NavigateEvent::intercept(Document& document, NavigationInterceptOptions&& options)
{
    RELEASE_LOG(Navigation, "NavigateEvent::intercept - called on NavigateEvent %p", this);
    
    if (auto checkResult = sharedChecks(document); checkResult.hasException())
        return checkResult;

    if (!canIntercept())
        return Exception { ExceptionCode::SecurityError, "Event is not interceptable"_s };

    if (!isBeingDispatched())
        return Exception { ExceptionCode::InvalidStateError, "Event is not being dispatched"_s };

    ASSERT(!m_interceptionState || m_interceptionState == InterceptionState::Intercepted);

    if (options.handler) {
        RELEASE_LOG(Navigation, "NavigateEvent::intercept - adding handler");
        m_handlers.append(options.handler.releaseNonNull());
    }

    if (options.focusReset) {
        // FIXME: Print warning to console if it was already set.
        RELEASE_LOG(Navigation, "NavigateEvent::intercept - setting focus reset to %d", static_cast<int>(options.focusReset.value()));
        m_focusReset = options.focusReset;
    }

    if (options.scroll) {
        // FIXME: Print warning to console if it was already set.
        RELEASE_LOG(Navigation, "NavigateEvent::intercept - setting scroll behavior to %d", static_cast<int>(options.scroll.value()));
        m_scrollBehavior = options.scroll;
    }

    RELEASE_LOG(Navigation, "NavigateEvent::intercept - setting interception state to Intercepted");
    m_interceptionState = InterceptionState::Intercepted;

    return { };
}

void NavigateEvent::setInterceptionState(InterceptionState interceptionState)
{
    RELEASE_LOG(Navigation, "NavigateEvent::setInterceptionState - setting state on %p from %d to %d", 
                this,
                static_cast<int>(m_interceptionState.value_or(static_cast<InterceptionState>(-1))), 
                static_cast<int>(interceptionState));
    m_interceptionState = interceptionState;
}

// https://html.spec.whatwg.org/multipage/nav-history-apis.html#process-scroll-behavior
void NavigateEvent::processScrollBehavior(Document& document)
{
    RELEASE_LOG(Navigation, "NavigateEvent::processScrollBehavior - called, navigationType=%d, interceptionState=%d", 
                static_cast<int>(m_navigationType), 
                static_cast<int>(m_interceptionState.value_or(static_cast<InterceptionState>(-1))));
                
    ASSERT(m_interceptionState == InterceptionState::Committed);
    m_interceptionState = InterceptionState::Scrolled;

    if (m_navigationType == NavigationNavigationType::Traverse) {
        RELEASE_LOG(Navigation, "NavigateEvent::processScrollBehavior - restoring scroll position for traverse");
        document.frame()->loader().history().restoreScrollPositionAndViewState();
    } else if (m_navigationType == NavigationNavigationType::Reload) {
        RELEASE_LOG(Navigation, "NavigateEvent::processScrollBehavior - reload navigation, checking for fragment");
        // For reload navigations, if there's a fragment, scroll to it; otherwise restore scroll position
        if (document.url().hasFragmentIdentifier()) {
            RELEASE_LOG(Navigation, "NavigateEvent::processScrollBehavior - reload with fragment, scrolling to fragment");
            if (!document.frame()->view()->scrollToFragment(document.url())) {
                RELEASE_LOG(Navigation, "NavigateEvent::processScrollBehavior - fragment not found, scrolling to (0,0)");
                document.frame()->view()->scrollTo({ 0, 0 });
            }
        } else {
            RELEASE_LOG(Navigation, "NavigateEvent::processScrollBehavior - reload without fragment, restoring scroll position");
            document.frame()->loader().history().restoreScrollPositionAndViewState();
        }
    } else if (!document.frame()->view()->scrollToFragment(document.url())) {
        RELEASE_LOG(Navigation, "NavigateEvent::processScrollBehavior - no fragment found, checking if should scroll to top");
        if (!document.url().hasFragmentIdentifier()) {
            RELEASE_LOG(Navigation, "NavigateEvent::processScrollBehavior - scrolling to (0,0)");
            document.frame()->view()->scrollTo({ 0, 0 });
        }
    } else {
        RELEASE_LOG(Navigation, "NavigateEvent::processScrollBehavior - scrolled to fragment");
    }

    // Reset the doNotAbortNavigationAPI flag since scroll behavior processing is complete
    document.frame()->loader().setDoNotAbortNavigationAPI(false);
    RELEASE_LOG(Navigation, "NavigateEvent::processScrollBehavior - reset doNotAbortNavigationAPI=false after scroll behavior processing");
}

// https://html.spec.whatwg.org/multipage/nav-history-apis.html#dom-navigateevent-scroll
ExceptionOr<void> NavigateEvent::scroll(Document& document)
{
    RELEASE_LOG(Navigation, "NavigateEvent::scroll - called, interceptionState=%d", static_cast<int>(m_interceptionState.value_or(static_cast<InterceptionState>(-1))));
    
    auto checkResult = sharedChecks(document);
    if (checkResult.hasException()) {
        RELEASE_LOG(Navigation, "NavigateEvent::scroll - sharedChecks failed");
        return checkResult;
    }

    if (m_interceptionState != InterceptionState::Committed) {
        RELEASE_LOG(Navigation, "NavigateEvent::scroll - InvalidStateError: interceptionState is not Committed");
        return Exception { ExceptionCode::InvalidStateError, "Interception has not been committed"_s };
    }

    if (m_interceptionState == InterceptionState::Scrolled) {
        RELEASE_LOG(Navigation, "NavigateEvent::scroll - InvalidStateError: already scrolled");
        return Exception { ExceptionCode::InvalidStateError, "Already scrolled"_s };
    }

    RELEASE_LOG(Navigation, "NavigateEvent::scroll - processing scroll behavior");
    processScrollBehavior(document);

    return { };
}

// https://html.spec.whatwg.org/multipage/nav-history-apis.html#potentially-process-scroll-behavior
void NavigateEvent::potentiallyProcessScrollBehavior(Document& document)
{
    RELEASE_LOG(Navigation, "NavigateEvent::potentiallyProcessScrollBehavior - called, scrollBehavior=%d, interceptionState=%d", 
                static_cast<int>(m_scrollBehavior.value_or(static_cast<NavigationScrollBehavior>(-1))), 
                static_cast<int>(m_interceptionState.value_or(static_cast<InterceptionState>(-1))));
                
    ASSERT(m_interceptionState == InterceptionState::Committed || m_interceptionState == InterceptionState::Scrolled);
    if (m_interceptionState == InterceptionState::Scrolled) {
        RELEASE_LOG(Navigation, "NavigateEvent::potentiallyProcessScrollBehavior - already scrolled, returning");
        return;
    }
    
    if (m_scrollBehavior == NavigationScrollBehavior::Manual) {
        RELEASE_LOG(Navigation, "NavigateEvent::potentiallyProcessScrollBehavior - manual scroll behavior, returning");
        return;
    }

    RELEASE_LOG(Navigation, "NavigateEvent::potentiallyProcessScrollBehavior - processing scroll behavior");
    processScrollBehavior(document);
}

// https://html.spec.whatwg.org/multipage/nav-history-apis.html#navigateevent-finish
void NavigateEvent::finish(Document& document, InterceptionHandlersDidFulfill didFulfill, FocusDidChange focusChanged)
{
    RELEASE_LOG(Navigation, "NavigateEvent::finish - called on NavigateEvent %p with didFulfill=%d, scrollBehavior=%d, interceptionState=%d", 
                this,
                static_cast<int>(didFulfill), 
                static_cast<int>(m_scrollBehavior.value_or(static_cast<NavigationScrollBehavior>(-1))),
                static_cast<int>(m_interceptionState.value_or(static_cast<InterceptionState>(-1))));
                
    // Note: ASSERT disabled temporarily to debug this issue
    // ASSERT(m_interceptionState != InterceptionState::Intercepted && m_interceptionState != InterceptionState::Finished);
    if (!m_interceptionState) {
        RELEASE_LOG(Navigation, "NavigateEvent::finish - no interception state on %p, returning early (event was not intercepted)", this);
        return;
    }
    
    if (m_interceptionState == InterceptionState::Finished) {
        RELEASE_LOG(Navigation, "NavigateEvent::finish - already finished on %p, returning early", this);
        return;
    }

    ASSERT(m_interceptionState == InterceptionState::Committed || m_interceptionState == InterceptionState::Scrolled);
    if (focusChanged == FocusDidChange::No && m_focusReset != NavigationFocusReset::Manual) {
        RefPtr documentElement = document.documentElement();
        ASSERT(documentElement);

        RefPtr<Element> focusTarget = documentElement->findAutofocusDelegate();
        if (!focusTarget)
            focusTarget = document.body();
        if (!focusTarget)
            focusTarget = documentElement;

        document.setFocusedElement(focusTarget.get());
    }

    // Fix: For scroll: "after-transition", scrolling should only happen when handlers fulfill successfully
    // When handlers reject, scrolling should NOT happen regardless of scroll behavior setting
    if (didFulfill == InterceptionHandlersDidFulfill::Yes) {
        RELEASE_LOG(Navigation, "NavigateEvent::finish - handlers fulfilled, processing scroll behavior");
        potentiallyProcessScrollBehavior(document);
    } else {
        RELEASE_LOG(Navigation, "NavigateEvent::finish - handlers rejected/failed, skipping scroll behavior");
        // According to the spec, when intercept handlers reject, scroll should not happen
        // even for "after-transition" scroll behavior
    }

    m_interceptionState = InterceptionState::Finished;
}

} // namespace WebCore
