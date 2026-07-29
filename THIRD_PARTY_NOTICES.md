# Third-party notices

The current source does not copy source code from KytyPS5 or SharpEmu. It uses
both projects as research references. See `docs/stage1-loader.md` and
`docs/stage2-kernel.md` for the reviewed files and pinned commits.

The event-flag behavior in `src/kernel/event_flag.cpp` and
`tests/kernel_event_flag_test.cpp` was implemented from focused observations
of KytyPS5 and SharpEmu. The SharpEmu reference file states:

`Copyright (C) 2026 SharpEmu Emulator Project`

Both references are GPL-2.0-or-later. No upstream scheduler, ownership model,
or source code was copied.

Add an entry here when a later change imports or adapts code. Each entry must
state the project, commit, source path, destination path, copyright notice, and
license.
