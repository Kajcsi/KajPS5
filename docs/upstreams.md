# Upstream projects

KajPS5 uses these independent projects as references:

| Project | Role | Language | License | Pinned commit |
| --- | --- | --- | --- | --- |
| [KytyPS5](https://github.com/KytyPS5/KytyPS5) | Primary native architecture reference | C++ | GPL-2.0-or-later | `f6e01e54031a3c615f089f061a4eab2f3c59acba` |
| [SharpEmu](https://github.com/sharpemu/sharpemu) | Behavior and test reference | C# | GPL-2.0-or-later | `d5108e854d609808f17093a6f5dbbc711d09ad2e` |

The current code does not copy source from either project. Adapted behavior
and provenance are recorded in `THIRD_PARTY_NOTICES.md`.

At the start of a development session, run the read-only update check:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-upstreams.ps1
```

The command compares each pin with the upstream default branch. It changes
neither the working tree nor the lock file. A reported update is a reason to
review the new work, not a reason to move the pin automatically.

Move a pin only when a newer commit supplies behavior, a correction, or a test
needed by the active KajPS5 milestone. Review the relevant upstream diff
first. The same commit must update the lock file, the table above, and the
refresh record below. Record the old and new commits, reviewed paths, reason,
and KajPS5 validation. Update `THIRD_PARTY_NOTICES.md` when the referenced or
adapted paths change.

## Refresh record

No pin has been refreshed since the initial selection.

When KajPS5 imports code, identify the upstream file and commit, keep its
copyright and license notice, and update `THIRD_PARTY_NOTICES.md` in the same
commit.

Do not copy proprietary files, game data, keys, firmware, or system modules
into this repository.
