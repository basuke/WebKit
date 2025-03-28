/*
 * Copyright (C) 2017-2024 Apple Inc. All rights reserved.
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

#pragma once

#if !BUSE(TZONE)

#include "BPlatform.h"
#include "DeferredDecommitInlines.h"
#include "DeferredTriggerInlines.h"
#include "EligibilityResultInlines.h"
#include "FreeListInlines.h"
#include "IsoAllocatorInlines.h"
#include "IsoDeallocatorInlines.h"
#include "IsoDirectoryInlines.h"
#include "IsoDirectoryPageInlines.h"
#include "IsoHeapImplInlines.h"
#include "IsoHeap.h"
#include "IsoPageInlines.h"
#include "IsoTLSAllocatorEntryInlines.h"
#include "IsoTLSDeallocatorEntryInlines.h"
#include "IsoTLSEntryInlines.h"
#include "IsoTLSInlines.h"

namespace bmalloc { namespace api {

#if !BUSE(LIBPAS)

#if BENABLE_MALLOC_HEAP_BREAKDOWN
template<typename Type>
IsoHeapBase<Type>::IsoHeapBase(const char* heapClass)
    : m_zone(malloc_create_zone(0, 0))
{
    if (heapClass)
        malloc_set_zone_name(m_zone, heapClass);
}
#endif

template<typename Type>
void IsoHeapBase<Type>::scavenge()
{
    IsoTLS::scavenge(*this);
}

template<typename Type>
bool IsoHeapBase<Type>::isInitialized()
{
    auto* atomic = reinterpret_cast<std::atomic<IsoHeapImpl<Config>*>*>(&m_impl);
    return atomic->load(std::memory_order_acquire);
}

template<typename Type>
void IsoHeapBase<Type>::initialize()
{
    // We are using m_impl field as a guard variable of the initialization of IsoHeap.
    // IsoHeap::isInitialized gets m_impl with "acquire", and IsoHeap::initialize stores
    // the value to m_impl with "release". To make IsoHeap changes visible to any threads
    // when IsoHeap::isInitialized returns true, we need to store the value to m_impl *after*
    // all the initialization finishes.
    auto* heap = new IsoHeapImpl<Config>();
    heap->addToAllIsoHeaps();
    setAllocatorOffset(heap->allocatorOffset());
    setDeallocatorOffset(heap->deallocatorOffset());
    auto* atomic = reinterpret_cast<std::atomic<IsoHeapImpl<Config>*>*>(&m_impl);
    atomic->store(heap, std::memory_order_release);
}

template<typename Type>
auto IsoHeapBase<Type>::impl() -> IsoHeapImpl<Config>&
{
    IsoTLS::ensureHeap(*this);
    return *m_impl;
}

template<typename Type>
void* IsoHeapBase<Type>::allocate()
{
    bool abortOnFailure = true;
    return IsoTLS::allocate(*this, abortOnFailure);
}

template<typename Type>
void* IsoHeapBase<Type>::tryAllocate()
{
    bool abortOnFailure = false;
    return IsoTLS::allocate(*this, abortOnFailure);
}

template<typename Type>
void IsoHeapBase<Type>::deallocate(void* p)
{
    IsoTLS::deallocate(*this, p);
}

#endif // !BUSE(LIBPAS)

// This is most appropraite for template classes.
#define MAKE_BISO_MALLOCED_INLINE(isoType, heapType) \
public: \
    static ::bmalloc::api::heapType<isoType>& bisoHeap() \
    { \
        static ::bmalloc::api::IsoHeap<isoType> heap("WebKit_"#isoType); \
        return heap; \
    } \
    \
    void* operator new(size_t, void* p) { return p; } \
    void* operator new[](size_t, void* p) { return p; } \
    \
    void* operator new(size_t size) \
    { \
        RELEASE_BASSERT(size == sizeof(isoType)); \
        return bisoHeap().allocate(); \
    } \
    \
    void operator delete(void* p) \
    { \
        bisoHeap().deallocate(p); \
    } \
    \
    void* operator new[](size_t size) = delete; \
    void operator delete[](void* p) = delete; \
    \
    static void freeAfterDestruction(void* p) \
    { \
        bisoHeap().deallocate(p); \
    } \
    \
    using WTFIsFastMallocAllocated = int; \
private: \
    using __makeBisoMallocedInlineMacroSemicolonifier BUNUSED_TYPE_ALIAS = int

#define MAKE_BISO_MALLOCED_IMPL(isoType, heapType) \
::bmalloc::api::heapType<isoType>& isoType::bisoHeap() \
{ \
    static ::bmalloc::api::heapType<isoType> heap("WebKit "#isoType); \
    return heap; \
} \
\
void* isoType::operator new(size_t size) \
{ \
    RELEASE_BASSERT(size == sizeof(isoType)); \
    return bisoHeap().allocate(); \
} \
\
void isoType::operator delete(void* p) \
{ \
    bisoHeap().deallocate(p); \
} \
\
void isoType::freeAfterDestruction(void* p) \
{ \
    bisoHeap().deallocate(p); \
} \
\
using __makeBisoMallocedInlineMacroSemicolonifier BUNUSED_TYPE_ALIAS = int

} } // namespace bmalloc::api

#endif // !BUSE(TZONE)
