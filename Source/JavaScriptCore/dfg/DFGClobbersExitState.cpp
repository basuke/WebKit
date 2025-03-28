/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
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
#include "DFGClobbersExitState.h"

#if ENABLE(DFG_JIT)

#include "ButterflyInlines.h"
#include "DFGClobberize.h"
#include "DFGNode.h"

namespace JSC { namespace DFG {

bool clobbersExitState(Graph& graph, Node* node)
{
    // There are certain nodes whose effect on the exit state has nothing to do with what they
    // normally clobber.
    switch (node->op()) {
    case InitializeEntrypointArguments:
    case MovHint:
    case ZombieHint:
    case PutHint:
    case KillStack:
        return true;

    case SetLocal:
    case PutStack:
        // These nodes write to the stack, but they may only do so after we have already had a MovHint
        // for the exact same value and the same stack location. Hence, they have no further effect on
        // exit state.
        return false;

    case ArrayifyToStructure:
    case Arrayify:
    case NewObject:
    case NewGenerator:
    case NewAsyncGenerator:
    case NewInternalFieldObject:
    case NewRegExp:
    case NewMap:
    case NewSet:
    case NewStringObject:
    case NewBoundFunction:
    case PhantomNewObject:
    case MaterializeNewObject:
    case PhantomNewFunction:
    case PhantomNewGeneratorFunction:
    case PhantomNewAsyncGeneratorFunction:
    case PhantomNewAsyncFunction:
    case PhantomNewInternalFieldObject:
    case MaterializeNewInternalFieldObject:
    case PhantomCreateActivation:
    case MaterializeCreateActivation:
    case PhantomNewRegExp:
    case CountExecution:
    case SuperSamplerBegin:
    case SuperSamplerEnd:
    case StoreBarrier:
    case FencedStoreBarrier:
    case AllocatePropertyStorage:
    case ReallocatePropertyStorage:
    case FilterCallLinkStatus:
    case FilterGetByStatus:
    case FilterPutByStatus:
    case FilterInByStatus:
    case FilterDeleteByStatus:
    case FilterCheckPrivateBrandStatus:
    case FilterSetPrivateBrandStatus:
    case TryGetById:
        // These do clobber memory, but nothing that is observable. It may be nice to separate the
        // heaps into those that are observable and those that aren't, but we don't do that right now.
        // FIXME: https://bugs.webkit.org/show_bug.cgi?id=148440
        return false;

    case NewRegExpUntyped:
        return !(node->child1().useKind() == StringUse && node->child2().useKind() == StringUse);

    case CreateActivation:
        // Like above, but with the activation allocation caveat.
        return node->castOperand<SymbolTable*>()->singleton().isStillValid();

    case NewFunction:
    case NewGeneratorFunction:
    case NewAsyncGeneratorFunction:
    case NewAsyncFunction:
        // Like above, but with the JSFunction allocation caveat.
        return node->castOperand<FunctionExecutable*>()->singleton().isStillValid();

    default:
        // For all other nodes, we just care about whether they write to something other than SideState.
        bool result = false;
        clobberize(
            graph, node, NoOpClobberize(),
            [&] (const AbstractHeap& heap) {
                // There shouldn't be such a thing as a strict subtype of SideState or HeapObjectCount.
                // That's what allows us to use a fast != check, below.
                ASSERT(!heap.isStrictSubtypeOf(SideState) && !heap.isStrictSubtypeOf(HeapObjectCount));

                if (heap != SideState && heap != HeapObjectCount)
                    result = true;
            },
            NoOpClobberize());
        return result;
    }
}

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)
