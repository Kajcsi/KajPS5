# Public ELF validation

KajPS5 was tested with the `hello_world` sample from PS5 Payload SDK v0.41.
The GPL-3.0-or-later sample stays outside this repository.

- Repository: `https://github.com/ps5-payload-dev/sdk.git`
- Tag: `v0.41`
- Commit: `d2e2e585740362976a39fdd5ccf390f199a7bc37`
- Release archive SHA-256:
  `ebfb0acb5260511951a80e17db41650c62d20a8caf8659a230b928dc85005984`
- Built `hello_world.elf` SHA-256:
  `e0815526f8f79727ed9a9b504dca6ec6b8bbb89d989a5b7125e4f602f925a17c`

The Windows build used LLVM 22.1.0 and the SDK's compiler wrapper. LLVM 22
needed an explicit SDK sysroot. It also needed one explicit `crt1.o` with
`-nostartfiles` because the wrapper otherwise supplied the start object twice.

The result is a little-endian x86-64 ELF shared object with the FreeBSD ABI,
four program headers, and three `PT_LOAD` segments. KajPS5 found 24 non-null
dynamic entries, a 107-byte string table, three `DT_NEEDED` names, 142 standard
`RELA` entries, and one PLT `RELA` entry. Its `DT_HASH` table describes four
dynamic symbols, including three unresolved imports.

The load check copied 53,860 file bytes, cleared another 2,324 bytes, and kept
the `R/W/X` flags from each segment. The complete guest range was 84,264 bytes.

This check used no PS5 console, game, firmware, key, SELF file, or proprietary
system module.
