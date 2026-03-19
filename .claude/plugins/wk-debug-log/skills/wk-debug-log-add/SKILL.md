---
description: Add WTFLogAlways debug logging to WebKit source files
argument-hint: <description of what to trace>
allowed-tools: Bash(git diff:*), Bash(git log:*)
---

# WebKit Debug Log — Add

Add temporary `WTFLogAlways` debug logging to trace the execution flow described by the user.

User argument: $ARGUMENTS

---

## Use WTFLogAlways only

- Use `WTFLogAlways("::DEBUG:: ...")` for all debug output.
- **DO NOT** use `RELEASE_LOG`, `LOG`, `printf`, `fprintf`, `NSLog`, or any other logging mechanism. These depend on channel settings or build configuration and may not produce output.
- `WTFLogAlways` works in all processes: UIProcess, WebProcess, NetworkProcess, and GPUProcess.

## Prefix every message with `::DEBUG::`

This allows easy grep-based search and bulk removal:
```cpp
WTFLogAlways("::DEBUG:: functionName: description key=%s", value);
```

## Include function name in every message

```cpp
WTFLogAlways("::DEBUG:: loadDifferentDocumentItem: POST formData found url=%s", url.string().utf8().data());
```

## Output values that identify which code path was taken

- bool results: `"canContinue=%d", canContinue`
- enum values: `"state=%d", static_cast<int>(state)`
- pointer null/non-null: `"item=%p", item.get()`
- URL: `"url=%s", url.string().utf8().data()`
- isMainFrame: `"isMainFrame=%d", frame->isMainFrame()`

## ObjectIdentifier output

```cpp
// Simple identifiers (FrameIdentifier, PageIdentifier, ProcessIdentifier, etc.)
WTFLogAlways("::DEBUG:: frameID=%" PRIu64, frameID.toUInt64());
// or
WTFLogAlways("::DEBUG:: frameID=%s", frameID.loggingString().utf8().data());

// Compound identifiers (BackForwardItemIdentifier, etc.)
WTFLogAlways("::DEBUG:: itemID=%s", itemID.toString().utf8().data());
```

## Handling std::optional and nullable types

Many WebKit getters return `std::optional<T>` or pointer types. **Always check before dereferencing.**

```cpp
// std::optional<FrameIdentifier> — use -> or * after checking
WTFLogAlways("::DEBUG:: frameID=%s", frameID ? frameID->loggingString().utf8().data() : "none");

// Pointer types — guard with ternary
WTFLogAlways("::DEBUG:: url=%s", item ? item->url().string().utf8().data() : "<null>");
```

**IMPORTANT**: Before formatting any value for logging, check the actual return type of the getter. If it returns `std::optional<T>`, use `->` after a truthiness check. Do NOT call `.member()` directly on an `std::optional`.

## Place logs at every branch of a conditional

```cpp
if (condition) {
    WTFLogAlways("::DEBUG:: functionName: condition=true ...");
    // ...
} else {
    WTFLogAlways("::DEBUG:: functionName: condition=false ...");
    // ...
}
```

## Log before AND after calls with side effects

```cpp
WTFLogAlways("::DEBUG:: before stopAllLoaders");
stopAllLoaders(ClearProvisionalItem::No);
WTFLogAlways("::DEBUG:: after stopAllLoaders");
```

## Log at IPC boundaries

Add logs on both the sending side and receiving side of IPC messages.

## Sensitive data

Do not log values that may contain user credentials, authentication tokens, cookie contents, passwords, or user-entered form data. When logging URLs, be aware they may contain sensitive query parameters. Prefer logging the *existence* or *type* of data rather than its full contents when security-sensitive data may be involved.

## NEVER add debug logs to header files

- **Only add `WTFLogAlways` calls to `.cpp` or `.mm` implementation files** in `Source/` and `Tools/`.
- **NEVER add debug logs to `.h` header files.** Header files are included by many translation units — modifying a header triggers recompilation of every file that includes it (directly or transitively), dramatically increasing build time.
- If the code you want to trace is in a header (e.g., an inline function or template), find the call site in a `.cpp` file and log there instead.

## Scope control

- Start narrow — prefer instrumenting a single function or file first, then expand if needed.
- If the trace request would result in more than ~15 log insertions, confirm with the user before proceeding. Show the list of functions/files you plan to instrument and let the user decide the scope.

## Code safety rules

- **Prefer inserting new lines** — add `WTFLogAlways(...)` lines between existing lines whenever possible.
- **Modifying existing lines is allowed when necessary** — e.g., converting a single-statement `if` to a block `{ ... }` to insert a log inside the branch is acceptable.
- **NEVER delete or alter control flow logic** — do not change conditions, remove `return` statements, or restructure code. Only add braces to accommodate log insertion.
- **NEVER introduce new variables** — use only expressions already in scope.
- **Keep `old_string` in Edit minimal** — match only 2-3 lines of context to avoid accidentally deleting code.
- **Verify braces and function boundaries** — ensure closing `}` is not merged with the next function declaration. Always check that a blank line separates the end of one function from the start of the next.
- **NEVER commit changes** — debug logging is temporary instrumentation. Do not run `git commit` or suggest committing the changes. The user will commit when ready.
