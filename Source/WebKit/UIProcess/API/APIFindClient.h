/*
 * Copyright (C) 2014 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef APIFindClient_h
#define APIFindClient_h

#include <WebCore/PlatformLayer.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/WTFString.h>

OBJC_CLASS CALayer;

namespace WebCore {
class IntRect;
}

namespace WebKit {
class WebPageProxy;
}

namespace API {

class FindClient {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(FindClient);
public:
    virtual ~FindClient() { }

    virtual void didCountStringMatches(WebKit::WebPageProxy*, const WTF::String&, uint32_t) { }
    virtual void didFindString(WebKit::WebPageProxy*, const WTF::String&, const Vector<WebCore::IntRect>& matchRects, uint32_t, int32_t, bool didWrapAround) { }
    virtual void didFailToFindString(WebKit::WebPageProxy*, const WTF::String&) { }

    virtual void didAddLayerForFindOverlay(WebKit::WebPageProxy*, PlatformLayer*) { }
    virtual void didRemoveLayerForFindOverlay(WebKit::WebPageProxy*) { }

    virtual bool isWebKitFindClient() const { return false; }
};

} // namespace API

#endif // APIFindClient_h
