---
description: Remove all ::DEBUG:: WTFLogAlways debug logging from WebKit source files
argument-hint: <description of target scope>
allowed-tools: Bash(git diff:*), Bash(git log:*)
---

# WebKit Debug Log — Remove

Find and remove all lines containing `::DEBUG::` from the codebase.

## Scope

Hint: $ARGUMENTS

If no hint is given, use `git diff` to identify files you have modified, and search those files for `::DEBUG::` lines. This focuses removal on your own changes rather than scanning the entire tree.

If the user provided a hint, interpret it flexibly:
- A project name like "WebCore" or "WebKit" → search `Source/WebCore/` or `Source/WebKit/`
- A subdirectory path like "WebCore/loader" or "Tools/TestWebKitAPI" → search that path
- A feature area like "navigation" or "back-forward" → find files containing `::DEBUG::` and filter to those whose path or content relates to the described area
- A filename or glob pattern like "FrameLoader*" → search matching files only

## Steps

1. Search for all files containing `::DEBUG::` within the scope:
   ```
   Grep for "::DEBUG::" across the target directory
   ```
2. For each file, remove every line that contains `WTFLogAlways("::DEBUG::` — remove the entire line including the trailing semicolon and newline.
3. After removing debug log lines, clean up braces to match WebKit style (single-statement bodies must not have braces):
   - Scan each `if`, `else`, `else if`, `for`, and `while` block in the modified file.
   - If a block `{ ... }` now contains exactly one statement, revert it to brace-less form.
   - This is a purely structural check — count the statements inside the block regardless of how the braces got there.
   - Example — before removal:
     ```cpp
     if (!parentItem || !parentItem->children().size()) {
         WTFLogAlways("::DEBUG:: ...");
         return false;
     }
     ```
     After removal, revert to:
     ```cpp
     if (!parentItem || !parentItem->children().size())
         return false;
     ```
   - **Do NOT remove braces from blocks that still contain multiple statements.** Double-check the statement count before removing any braces.
4. Collapse any resulting double-blank-lines into a single blank line.
5. Do NOT remove or modify any other lines.
6. Report how many lines were removed from how many files.
