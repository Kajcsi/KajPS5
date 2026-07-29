# Public ELF validation

KajPS5 was tested with the `hello_world` sample from PS5 Payload SDK v0.41.
The sample is GPL-3.0-or-later and remains outside this repository.

- Repository: `https://github.com/ps5-payload-dev/sdk.git`
- Tag: `v0.41`
- Commit: `d2e2e585740362976a39fdd5ccf390f199a7bc37`
- Release archive SHA-256:
  `ebfb0acb5260511951a80e17db41650c62d20a8caf8659a230b928dc85005984`
- Built `hello_world.elf` SHA-256:
  `e0815526f8f79727ed9a9b504dca6ec6b8bbb89d989a5b7125e4f602f925a17c`

The Windows build used LLVM 22.1.0 and the SDK's Windows compiler wrapper.
LLVM 22 needed an explicit SDK sysroot and one explicit `crt1.o` with
`-nostartfiles` to avoid the wrapper's duplicate start object.

The result is a little-endian x86-64 ELF shared object with FreeBSD ABI, four
program headers, and three `PT_LOAD` segments. KajPS5 parsed all metadata and
24 non-null dynamic entries. It resolved a 107-byte dynamic string table and
three `DT_NEEDED` names. It also validated 142 standard `RELA` entries and one
PLT `RELA` entry. The standard `DT_HASH` table identified four dynamic symbols,
including three undefined imports. KajPS5 loaded 53,860 file bytes, cleared
2,324 additional bytes, and applied each segment's `R/W/X` flags. The required
guest range was 84,264 bytes.

No PS5 console, game, firmware, key, SELF file, or proprietary system module
was used.
