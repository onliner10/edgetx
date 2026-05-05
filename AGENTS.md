# Codex Notes

- Use `nix develop` for builds, simulator work, and `tools/ui-harness` commands in this repo. The flake provides CMake, Ninja, LLVM/libclang, Python dependencies, SDL2, and X11 headers expected by the native simulator build.
- Prefer commands shaped like `nix develop -c tools/ui-harness/edgetx-ui build tx16s` and `nix develop -c tools/ui-harness/edgetx-ui run-flow tools/ui-harness/flows/tx16s-smoke.json`.
- For firmware builds, use `nix develop -c env FLAVOR=tx16s tools/build-gh.sh` or set `FLAVOR=tx16smk3` for the MK3 target.
- This machine has 24 build threads available. Prefer `cmake --build ... --parallel 24`, or set `CMAKE_BUILD_PARALLEL_LEVEL=24` when invoking project build scripts.
- The UI harness writes simulator builds under `build/ui-harness` by default. Set `EDGETX_UI_BUILD_ROOT=/tmp/edgetx-ui-build` only when you intentionally want build output outside the worktree.
