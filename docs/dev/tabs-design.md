# tabs (multiple buffers)

⏺ I'll create a visual representation showing the expected behavior when
opening new tabs in the Loki editor.

⏺ Perfect! All tests pass. Now let me create the updated visual
  representation showing the simplified UX:

Initial State - Single Buffer (no tabs shown)
┌─────────────────────────────────────────────────────────────┐
│ Testing buffer functionality                                │
│                                                             │
│                                                             │
│ ~                                                           │
│ ~                                                           │
│ ~                                                           │
├─────────────────────────────────────────────────────────────┤
│ NORMAL  test_buffer_ux.txt - 1 lines                  1,1   │
│                                                             │
└─────────────────────────────────────────────────────────────┘


After pressing Ctrl-T (creates buffer #2)
┌─────────────────────────────────────────────────────────────┐
│ [1] [2] ← Simple tab bar with buffer numbers                │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│                                                             │
│ ~                                                           │
│ ~                                                           │
│ ~                                                           │
├─────────────────────────────────────────────────────────────┤
│ NORMAL  [No Name] - 1 lines                           1,1   │
│ Created buffer 2                                            │
└─────────────────────────────────────────────────────────────┘
     ↑
  [2] is highlighted (reverse video) showing current buffer
  Filename "[No Name]" shown in status bar below


After pressing Ctrl-T again (creates buffer #3)
┌─────────────────────────────────────────────────────────────┐
│ [1] [2] [3] ← All buffer numbers shown                      │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│                                                             │
│ ~                                                           │
│ ~                                                           │
│ ~                                                           │
├─────────────────────────────────────────────────────────────┤
│ NORMAL  [No Name] - 1 lines                           1,1   │
│ Created buffer 3                                            │
└─────────────────────────────────────────────────────────────┘
         ↑
      [3] is highlighted


After editing buffer #3 and typing "Hello world"
┌─────────────────────────────────────────────────────────────┐
│ [1] [2] [3]                                                 │
├─────────────────────────────────────────────────────────────┤
│ Hello world                                                 │
│                                                             │
│ ~                                                           │
│ ~                                                           │
│ ~                                                           │
├─────────────────────────────────────────────────────────────┤
│ INSERT  [No Name] - 1 lines (modified)                1,12  │
│                                                             │
└─────────────────────────────────────────────────────────────┘
         ↑
      [3] still highlighted
      Modified status shown in status bar below


After pressing Ctrl-X n (switch to next buffer = #1)
┌─────────────────────────────────────────────────────────────┐
│ [1] [2] [3]                                                 │
├─────────────────────────────────────────────────────────────┤
│ Testing buffer functionality                                │
│                                                             │
│                                                             │
│ ~                                                           │
│ ~                                                           │
│ ~                                                           │
├─────────────────────────────────────────────────────────────┤
│ NORMAL  test_buffer_ux.txt - 1 lines                  1,1   │
│ Switched to buffer 1                                        │
└─────────────────────────────────────────────────────────────┘
 ↑
[1] now highlighted, original file content shown
Filename shown in status bar


After pressing Ctrl-X p (switch to previous buffer = #3)
┌─────────────────────────────────────────────────────────────┐
│ [1] [2] [3]                                                 │
├─────────────────────────────────────────────────────────────┤
│ Hello world                                                 │
│                                                             │
│ ~                                                           │
│ ~                                                           │
│ ~                                                           │
├─────────────────────────────────────────────────────────────┤
│ NORMAL  [No Name] - 1 lines (modified)                1,12  │
│ Switched to buffer 3                                        │
└─────────────────────────────────────────────────────────────┘
         ↑
      [3] highlighted again
      Content preserved


After saving buffer #3 as "new_file.txt" with :w new_file.txt
┌─────────────────────────────────────────────────────────────┐
│ [1] [2] [3]                                                 │
├─────────────────────────────────────────────────────────────┤
│ Hello world                                                 │
│                                                             │
│ ~                                                           │
│ ~                                                           │
│ ~                                                           │
├─────────────────────────────────────────────────────────────┤
│ NORMAL  new_file.txt - 1 lines                        1,12  │
│ "new_file.txt" 1L written                                   │
└─────────────────────────────────────────────────────────────┘
         ↑
Tab bar unchanged (just shows [3])
Filename "new_file.txt" now shown in status bar


After pressing Ctrl-X k (close current buffer = #3)
┌─────────────────────────────────────────────────────────────┐
│ [1] [2] ← Buffer #3 removed                                 │
├─────────────────────────────────────────────────────────────┤
│ Testing buffer functionality                                │
│                                                             │
│                                                             │
│ ~                                                           │
│ ~                                                           │
│ ~                                                           │
├─────────────────────────────────────────────────────────────┤
│ NORMAL  test_buffer_ux.txt - 1 lines                  1,1   │
│ Closed buffer 3                                             │
└─────────────────────────────────────────────────────────────┘
 ↑
Automatically switched to buffer [1]


After closing buffer #2 (only one buffer left)
┌─────────────────────────────────────────────────────────────┐
│ Testing buffer functionality                                │
│                                                             │
│                                                             │
│ ~                                                           │
│ ~                                                           │
│ ~                                                           │
├─────────────────────────────────────────────────────────────┤
│ NORMAL  test_buffer_ux.txt - 1 lines                  1,1   │
│ Closed buffer 2                                             │
└─────────────────────────────────────────────────────────────┘

Tab bar disappears when only one buffer remains (cleaner UI)

Key Features of the Simplified UX:

1. Minimal Tab Bar: Shows only [1] [2] [3] - just the buffer numbers
2. Current Buffer: Highlighted with reverse video (inverted colors)
3. Status Bar Shows Details: Filename, modified status, line count all in
status bar
4. No Redundancy: Tab bar doesn't duplicate information from status bar
5. Clean and Fast: Easy to scan at a glance
6. Auto-Hide: Tab bar only appears with 2+ buffers

Benefits:
- Much cleaner visual appearance
- All detailed info (filename, modified status) in status bar where it
belongs
- Quick visual identification of which buffer you're in
- No clutter with long filenames in the tab bar

The implementation is complete and all tests pass!

Keybindings Summary:
- Ctrl-T: Create new empty buffer
- Ctrl-X n: Switch to next buffer (circular)
- Ctrl-X p: Switch to previous buffer (circular)
- Ctrl-X k: Close current buffer (with unsaved changes warning)
- Ctrl-X 1-9: Switch directly to buffer by number
