## WebPage
- AddConsoleMessage(FrameIdentifier, MessageSource, MessageLevel, String message,
                    optional<ResourceLoaderIdentifier> requestID)
    - => WebFrame::addConsoleMessage(source, level, message, requestID)

## WebFrame

- addConsoleMessage(MessageSource, MessageLevel, String message, uint64_t requestID)
    - [ ] convert to LocalFrame
    - => Document::addConsoleMessage(source, level, message, requestID)

## Document

- addConsoleMessage(MessageSource, MessageLevel, String message, unsigned long requestID)
    - if main thread()
        - PageConsoleClient::addMessage(source, level, message, requestID, this);
    - else
        - queue
        - [ ] requestID was gone
        - postTask(AddConsoleMessageTask(source, level, message))

## PageConsoleClient

- addMessage(MessageSource, MessageLevel, String message, unsigned long requestID, Document)
    - addMessage(source, level, message, url, line, col, null, JSExecState::currentState(), requestId)
- addMessage(MessageSource, MessageLevel, String message, String url, unsigned line, unsigned col,
             ScriptCallStack, JSGlobalObject, unsigned long requestID)
    - if muteCount and source is ! ConsoleAPI, then return
    - [ ] make ConsoleMessage
    - addMessage(consoleMessage)
- addMessage(ConsoleMessage)
    - if page is not ephemeral session
        - [ ] if message type == Image, generate string message
        - WebChromeClient::addMessageToConsole(source, level, string message, line, column, url)
        - if listener for console message { m_consoleMessageListener }
            - [ ] send string message to listener
            - this is only used by layout test via `internals.setConsoleMessageListener()`.
        - logMessageToSystemConsole(consoleMessage)
    - InspectorInstrumentation::addMessageToConsole(page, WTFMove(consoleMessage)) ✅
- logMessageToSystemConsole(ConsoleMessage)
    - ConsoleClient::printConsoleMessage(source, level, string message, url, line, column)

## WebChromeClient

- addMessageToConsole(MessageSource, MessageLevel, String message, unsigned line, unsigned col, String sourceID)
    - on Cocoa, this doesn't do anything.
    - for testing
        - <IPC> WebPageProxy::addMessageToConsoleForTesting()

## InspectorInstrumentation

- addMessageToConsole(xxx, ConsoleMessage)
    - [ ] various conversion to InstrumentingAgents.
        - from LocalFrameView, Frame, Document, Page, ScriptExecutionContext, etc.
    - addMessageToConsoleImpl()
- addMessageToConsoleImpl(InstrumentingAgents, ConsoleMessage)
    - InspectorConsoleAgent::addMessageToConsole(consoleMessage)
    - [ ] FIXME: This should just pass the message on to the debugger agent.
            JavaScriptCore InspectorDebuggerAgent should know Console MessageTypes.
    - if isConsoleAssertMessage()
        WebDebuggerAgent::handleConsoleAssert(consoleMessage)

## InspectorConsoleAgent
- addMessageToConsole(ConsoleMessage)
    - addConsoleMessage(consoleMessage)
- addConsoleMessage(ConsoleMessage)
    - if message is equeal to previous message
        previousMessage::incrementCount()
        previousMessage::updateRepeatCountInConsole()
    - else
        message::addToFrontend(consoleFrontendDispatcher, injectedScriptManager, generate)

## ConsoleMessage

> it doesn't to have frame id. is that good?

- addToFrontend(ConsoleFrontendDispatcher, InjectedScriptManager, bool generatePreview)
    - generate messageObject
    - ConsoleFrontendDispatcher::messageAdded(message)

## ConsoleFrontendDispatcher (derived : InspectorFrontendDispatchers.cpp)

- messageAdded(Protocol::Console::ConsoleMessage)
    - generate json message
    - Inspector::FrontendRouter::sendEvent(jsonMessage)

## Inspector::FrontendRouter (InspectorFrontendRouter.cpp)

- sendEvent(String)
    - Inspector::FrontendChannel::sendMessageToFrontend(message)

## WebPageInspectorTargetFrontendChannel (Inspector::FrontendChannel)

> This object is created by `WebPageInspectorTarget::connect()`.
> The targetId is 

- sendMessageToFrontend(String)
    - <IPC> WebPageProxy::sendMessageToInspectorFrontend(targetId, message)

---

## WebPageProxy

- sendMessageToInspectorFrontend(String targetId, String message)
    - WebPageInspectorController::sendMessageToInspectorFrontend(targetId, message)

## WebPageInspectorController

> Source/WebKit/UIProcess/Inspector/WebPageInspectorController.cpp
> This is really complicated class. 
> `FrontendRouter` and `BackendDispatcher` are created in constructor

- sendMessageToInspectorFrontend(String targetId, String message)
    - Inspector::InspectorTargetAgent::sendMessageFromTargetToFrontend(targetId, message)

## Inspector::InspectorTargetAgent

> created by WebPageInspectorController

- sendMessageFromTargetToFrontend(String targetId, String message)
    TargetFrontendDispatcher::dispatchMessageFromTarget(targetId, message)

## TargetFrontendDispatcher (derived : InspectorFrontendDispatchers.cpp)

> created by `Inspector::InspectorTargetAgent`

- dispatchMessageFromTarget(const String& targetId, const String& message)
    - generate json message
    - Inspector::FrontendRouter::sendEvent(jsonMessage)

## WebInspectorUIProxy (Inspector::FrontendChannel)

> This object is created by `WebPageProxy`.
> The targetId is 

- sendEventToFrontend(String)
    - <IPC> WebPageProxy::sendMessageToInspectorFrontend(targetId, message)

