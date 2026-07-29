# Third-party notices

The current source does not copy source code from KytyPS5 or SharpEmu. It uses
both projects as research references. See `docs/stage1-loader.md` and
`docs/stage2-kernel.md` for the reviewed files and pinned commits.

The clock, event-flag, file, semaphore, and scheduler behaviors in `src/kernel/`,
`tests/kernel_event_flag_test.cpp`, `tests/kernel_event_wait_test.cpp`, and
`tests/kernel_clock_test.cpp`, `tests/kernel_file_test.cpp`,
`tests/kernel_scheduler_test.cpp`, and `tests/kernel_semaphore_test.cpp` were
implemented from focused observations of KytyPS5 and SharpEmu. The SharpEmu
reference files state:

`Copyright (C) 2026 SharpEmu Emulator Project`

Both references are GPL-2.0-or-later. No upstream host executor, continuation
system, ownership model, or source code was copied.

The checked dynamic-table and standard dynamic-string behavior in
`src/loader/elf.cpp` and
`tests/elf_dynamic_test.cpp` was implemented from focused observations of the
pinned KytyPS5 and SharpEmu loader files. The SharpEmu reference file states:

`Copyright (C) 2026 SharpEmu Emulator Project`

No upstream loader source code was copied.

Add an entry here when a later change imports or adapts code. Each entry must
state the project, commit, source path, destination path, copyright notice, and
license.
