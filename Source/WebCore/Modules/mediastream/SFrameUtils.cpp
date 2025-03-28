/*
 * Copyright (C) 2020-2024 Apple Inc. All rights reserved.
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
#include "SFrameUtils.h"

#if ENABLE(WEB_RTC)

#include <wtf/Function.h>
#include <wtf/StdLibExtras.h>

namespace WebCore {

static inline bool isSliceNALU(uint8_t data)
{
    return (data & 0x1F) == 1;
}

static inline bool isSPSNALU(uint8_t data)
{
    return (data & 0x1F) == 7;
}

static inline bool isPPSNALU(uint8_t data)
{
    return (data & 0x1F) == 8;
}

static inline bool isIDRNALU(uint8_t data)
{
    return (data & 0x1F) == 5;
}

static inline void findNalus(std::span<const uint8_t> frameData, size_t offset, NOESCAPE const Function<bool(size_t)>& callback)
{
    for (size_t i = 4 + offset; i < frameData.size(); ++i) {
        if (frameData[i - 4] == 0 && frameData[i - 3] == 0 && frameData[i - 2] == 0 && frameData[i - 1] == 1) {
            if (callback(i))
                return;
        }
    }
}

size_t computeH264PrefixOffset(std::span<const uint8_t> frameData)
{
    size_t offset = 0;
    findNalus(frameData, 0, [&offset, frameData](auto position) {
        if (isIDRNALU(frameData[position]) || isSliceNALU(frameData[position])) {
            // Skip 00 00 00 01, nalu type byte and the next byte.
            offset = position + 2;
            return true;
        }
        return false;
    });
    return offset;
}

bool needsRbspUnescaping(std::span<const uint8_t> frameData)
{
    for (size_t i = 0; i < frameData.size() - 3; ++i) {
        if (frameData[i] == 0 && frameData[i + 1] == 0 && frameData[i + 2] == 3)
            return true;
    }
    return false;
}

Vector<uint8_t> fromRbsp(std::span<const uint8_t> frameData)
{
    Vector<uint8_t> buffer;
    buffer.reserveInitialCapacity(frameData.size());

    size_t i;
    for (i = 0; i < frameData.size() - 3; ++i) {
        if (frameData[i] == 0 && frameData[i + 1] == 0 && frameData[i + 2] == 3) {
            buffer.append(frameData[i]);
            buffer.append(frameData[i + 1]);
            // Skip next byte which is delimiter.
            i += 2;
        } else
            buffer.append(frameData[i]);
    }
    for (; i < frameData.size(); ++i)
        buffer.append(frameData[i]);

    return buffer;
}

SFrameCompatibilityPrefixBuffer computeH264PrefixBuffer(std::span<const uint8_t> frameData)
{
    // Delta and key prefixes assume SPS/PPS with IDs equal to 0 have been transmitted.
    static const uint8_t prefixDeltaFrame[6] = { 0x00, 0x00, 0x00, 0x01, 0x21, 0xe0 };

    if (frameData.size() < 5)
        return std::span<const uint8_t> { };

    // We assume a key frame starts with SPS, then PPS. Otherwise we wrap it as a delta frame.
    if (!isSPSNALU(frameData[4]))
        return std::span<const uint8_t> { prefixDeltaFrame };

    // Search for PPS
    size_t spsPpsLength = 0;
    findNalus(frameData, 5, [frameData, &spsPpsLength](auto position) {
        if (isPPSNALU(frameData[position]))
            spsPpsLength = position;
        return true;
    });
    if (!spsPpsLength)
        return std::span<const uint8_t> { prefixDeltaFrame };

    // Search for next NALU to compute the real spsPpsLength, including the next 00 00 00 01.
    findNalus(frameData, spsPpsLength + 1, [&spsPpsLength](auto position) {
        spsPpsLength = position;
        return true;
    });

    Vector<uint8_t> buffer(spsPpsLength + 2);
IGNORE_GCC_WARNINGS_BEGIN("restrict")
    // https://bugs.webkit.org/show_bug.cgi?id=246862
    memcpySpan(buffer.mutableSpan(), frameData.first(spsPpsLength));
IGNORE_GCC_WARNINGS_END
    buffer[spsPpsLength] = 0x25;
    buffer[spsPpsLength + 1] = 0xb8;
    return buffer;
}

static inline void findEscapeRbspPatterns(const Vector<uint8_t>& frame, size_t offset, NOESCAPE const Function<void(size_t, bool)>& callback)
{
    size_t numConsecutiveZeros = 0;
    auto data = frame.span();
    for (size_t i = offset; i < frame.size(); ++i) {
        bool shouldEscape = data[i] <= 3 && numConsecutiveZeros >= 2;
        if (shouldEscape)
            numConsecutiveZeros = 0;

        if (data[i] == 0)
            ++numConsecutiveZeros;
        else
            numConsecutiveZeros = 0;

        callback(i, shouldEscape);
    }
}

void toRbsp(Vector<uint8_t>& frame, size_t offset)
{
    size_t count = 0;
    findEscapeRbspPatterns(frame, offset, [&count](size_t, bool shouldBeEscaped) {
        if (shouldBeEscaped)
            ++count;
    });
    if (!count)
        return;

    Vector<uint8_t> newFrame;
    newFrame.reserveInitialCapacity(frame.size() + count);
    newFrame.append(frame.subspan(0, offset));

    findEscapeRbspPatterns(frame, offset, [data = frame.span(), &newFrame](size_t position, bool shouldBeEscaped) {
        if (shouldBeEscaped)
            newFrame.append(3);
        newFrame.append(data[position]);
    });

    frame = WTFMove(newFrame);
}

static inline bool isVP8KeyFrame(std::span<const uint8_t> frame)
{
    ASSERT(frame.size());
    return !(frame.front() & 0x01);
}

size_t computeVP8PrefixOffset(std::span<const uint8_t> frame)
{
    return isVP8KeyFrame(frame) ? 10 : 3;
}

SFrameCompatibilityPrefixBuffer computeVP8PrefixBuffer(std::span<const uint8_t> frame)
{
    Vector<uint8_t> prefix(frame.first(isVP8KeyFrame(frame) ? 10 : 3));
    return prefix;
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC)
