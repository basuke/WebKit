TestPage.registerInitializer(function() {

window.GlobalPause = {
    sleep(ms)
    {
        return new Promise((resolve) => setTimeout(resolve, ms));
    },

    async evaluate(target, expression)
    {
        let {result} = await target.RuntimeAgent.evaluate.invoke({expression, objectGroup: "test", returnByValue: true});
        return result.value;
    },

    // Runs `expression` (by default a function containing a `debugger;` statement) in the target and resolves once it is paused there.
    async pause(target, expression = "pauseHere()")
    {
        let paused = new Promise((resolve) => {
            let listener = WI.debuggerManager.addEventListener(WI.DebuggerManager.Event.CallFramesDidChange, (event) => {
                if (event.data.target !== target || !WI.debuggerManager.dataForTarget(target).paused)
                    return;
                WI.debuggerManager.removeEventListener(WI.DebuggerManager.Event.CallFramesDidChange, listener);
                resolve();
            });
        });
        target.RuntimeAgent.evaluate.invoke({expression, objectGroup: "test"}).catch(() => { });
        await paused;
    },

    async resumeAll()
    {
        let resumed = WI.debuggerManager.awaitEvent(WI.DebuggerManager.Event.Resumed);
        await WI.debuggerManager.resume();
        await resumed;
    },

    // DebuggerManager asks a target that appears during a pause to pause too. Request and cancel it, so the target does not pause once the others resume.
    async cancelPauseRequest(target)
    {
        await target.DebuggerAgent.pause();
        await target.DebuggerAgent.resume();
    },

    // Expects `counter` to stay put for a while, and returns its value. A document that is held as it loads can run its first script late, so count "not run yet" as 0.
    async expectHeld(target, counter, description, message = "while a frame is paused")
    {
        let before = await GlobalPause.evaluate(target, counter);
        await GlobalPause.sleep(300);
        InspectorTest.expectEqual(await GlobalPause.evaluate(target, counter), before, `${description} should not advance ${message}.`);
        return before;
    },

    async expectRunning(target, counter, before, description)
    {
        InspectorTest.expectThat(await waitForExpressionInTarget(target, `${counter} > ${before}`, 50), `${description} should advance.`);
    },

    // Evaluating in the first target of an inserted frame, which is in the inserting process, would pause it there (DebuggerManager asks new targets to pause), and that pause never ends once the frame has moved.
    async waitForFrameTarget(urlSubstring, excludedProcesses = [])
    {
        for (let i = 0; i < 50; ++i) {
            for (let target of WI.targets.filter((t) => t.type === WI.TargetType.Frame && !excludedProcesses.includes(GlobalPause.processIdentifier(t)))) {
                let url = await Promise.race([GlobalPause.evaluate(target, "document.location.href").catch(() => ""), GlobalPause.sleep(1000).then(() => "")]);
                if (url.includes(urlSubstring))
                    return target;
            }
            await GlobalPause.sleep(100);
        }
        return null;
    },

    // The backend target serves the Target domain, including the pause session commands.
    pageTarget()
    {
        return WI.backendTarget;
    },

    // Resolves with the next pause session event for which `predicate({state, stoppingTargetIds})` is true.
    awaitSessionEvent(predicate)
    {
        return new Promise((resolve) => {
            let listener = WI.debuggerManager.addEventListener(WI.DebuggerManager.Event.PauseSessionChanged, (event) => {
                if (!predicate(event.data))
                    return;
                WI.debuggerManager.removeEventListener(WI.DebuggerManager.Event.PauseSessionChanged, listener);
                resolve(event.data);
            });
        });
    },

    processIdentifier(target)
    {
        return target.identifier.split("-").pop();
    },
};

});
