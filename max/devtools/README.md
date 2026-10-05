# Max layer syntax check (no SDK required)

`sdk_stub/` is **not** the 3ds Max SDK. It is a tiny stand-in that declares
only the classes and functions TopoWeave uses, with the signatures from the
official SDK reference. It lets CI (and Linux/macOS contributors) compile the
plug-in sources with `-fsyntax-only` to catch typos and API misuse inside
TopoWeave's own code. It cannot produce a working plug-in - build the real
`.dlm` on Windows against the real SDK (see `docs/BUILD_WINDOWS.md`).

```bash
./max/devtools/check_syntax.sh
```
