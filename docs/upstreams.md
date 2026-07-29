# Upstream projects

KajPS5 studies these independent projects:

| Project | Role | Language | License | Pinned commit |
| --- | --- | --- | --- | --- |
| [KytyPS5](https://github.com/KytyPS5/KytyPS5) | Primary native architecture reference | C++ | GPL-2.0-or-later | `f6e01e54031a3c615f089f061a4eab2f3c59acba` |
| [SharpEmu](https://github.com/sharpemu/sharpemu) | Behavior and test reference | C# | GPL-2.0-or-later | `d5108e854d609808f17093a6f5dbbc711d09ad2e` |

The foundation commit does not contain source code from either project.

Run the read-only update check at the start of each development session:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-upstreams.ps1
```

The command compares each pin with the remote default-branch HEAD. It does not
change the working tree or the lock file. An update report starts a review; it
does not automatically approve a new pin.

Refresh a pin only when a newer commit provides behavior, a correction, or a
test needed by the active KajPS5 milestone. Inspect the relevant upstream diff
first. In the pin-refresh commit, update the lock file and the table above, and
add a refresh record below. Include the old and new commits, the reviewed
paths, the reason, and the KajPS5 validation evidence. Update
`THIRD_PARTY_NOTICES.md` when the referenced or adapted paths change.

## Refresh record

No pin has been refreshed after the initial foundation selection.

When KajPS5 imports code, the change must identify the upstream file and
commit. The change must keep the original copyright and license notice. Add a
record to `THIRD_PARTY_NOTICES.md` in the same commit.

Do not copy proprietary files, game data, keys, firmware, or system modules
into this repository.
