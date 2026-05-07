# Codex Notes

- Use `nix develop` for builds, simulator work, and `tools/ui-harness` commands in this repo. The flake provides CMake, Ninja, LLVM/libclang, Python dependencies, SDL2, and X11 headers expected by the native simulator build.
- Prefer commands shaped like `nix develop -c tools/ui-harness/edgetx-ui build tx16s` and `nix develop -c tools/ui-harness/edgetx-ui run-flow tools/ui-harness/flows/tx16s-smoke.json`.
- For firmware builds, use `nix develop -c env FLAVOR=tx16s tools/build-gh.sh` or set `FLAVOR=tx16smk3` for the MK3 target.
- This machine has 24 build threads available. Prefer `cmake --build ... --parallel 24`, or set `CMAKE_BUILD_PARALLEL_LEVEL=24` when invoking project build scripts.
- The UI harness writes simulator builds under `build/ui-harness` by default. Set `EDGETX_UI_BUILD_ROOT=/tmp/edgetx-ui-build` only when you intentionally want build output outside the worktree.

## C++ Semantic MCP

This repo provides a reproducible Serena + clangd MCP setup through the Nix flake. Do not install Serena, clangd, or C/C++ language servers with host package managers for this project.

Before using semantic tools, generate the firmware compilation database from the flake:

```sh
git submodule update --init --recursive
nix develop -c tools/edge16-cpp-lsp setup tx16s
```

Use `tx16smk3` instead of `tx16s` when you need MK3-specific preprocessor state. The command writes build files under `build/cpp-lsp/<target>` and creates the ignored repo-root `compile_commands.json` symlink required by Serena/clangd.

To verify the semantic backend directly:

```sh
nix develop -c tools/edge16-cpp-lsp check radio/src/crc.cpp
```

For Codex MCP, configure the client to start this repo's wrapper through Nix, replacing `<repo>` with the absolute checkout path:

```toml
[mcp_servers.edge16-serena]
startup_timeout_sec = 60
command = "nix"
args = ["develop", "<repo>", "-c", "<repo>/tools/edge16-serena-mcp"]
```

When the MCP server is available, prefer Serena symbol tools for C/C++ navigation and cross-reference questions: `find_symbol`, `find_referencing_symbols`, `find_declaration`, `get_symbols_overview`, and diagnostics. If references look stale or new files are missing, rerun `tools/edge16-cpp-lsp setup` and restart the MCP server.

## Simulator UI Harness

An MCP server is registered in `opencode.json` providing MCP tools for interactive simulator testing (edgetx_start_simulator, edgetx_click, edgetx_ui_tree, edgetx_screenshot, etc.). See `opencode.json` at the repo root for the server command.

In code review / stateless contexts where MCP tools are not available, use `HarnessService` from `tools/ui-harness/edgetx_ui/core.py` directly (same backend).

For exploratory simulator UI testing, prefer a persistent `tools/ui-harness/edgetx-mcp` session over one-off JSON flows. JSON flows are replay artifacts after a path is understood; they are not the best discovery tool.

Use this workflow:

1. Build the simulator with Nix, for example `nix develop -c tools/ui-harness/edgetx-ui build tx16s`.
2. Start one persistent MCP simulator session with `edgetx_start_simulator`.
3. Call `edgetx_status` until `startup_completed: true` before screenshot QA. Before that, the UI tree may already contain real nodes while the framebuffer still shows splash/startup frames.
4. If a red storage warning appears, call `edgetx_skip_storage_warning_if_present`. The warning can have more than one phase, especially on MK3. Do not fail the test just because the warning appeared; verify the post-warning UI instead.
5. Call `edgetx_ui_tree` before interacting. Read the visible `role`, `text`, `automation_id`, `bounds`, and `actions`.
6. Use `edgetx_click` or `edgetx_long_click` on a node that has the required action. These selector clicks run on the menu/UI thread without fixed sleeps.
7. Call `edgetx_ui_tree` again after every meaningful interaction and confirm the expected node/page appeared.
8. For visual QA, take screenshots and compare them. A tree change alone is not enough proof that the framebuffer changed.

Selector rules:

- Prefer stable `automation_id` selectors such as `nav.quick_menu`, `nav.back`, `dialog.action`, `page.manage_models`, or `model.model1.yml`.
- Use exact `text` when the text is unique. Use `text_contains` only when exact text is awkward, and avoid vague matches like `Setup` unless you also specify `role` or `index`.
- A node with no `click` or `long_click` action is not currently interactable. This is intentional. Background controls may still be listed, but modal dialogs and topmost overlays remove their actions.
- If multiple actionable nodes match a selector, the harness chooses the topmost match by default. Add `index` only when you have inspected the tree and intentionally want a specific match.
- Use raw `edgetx_touch` or `edgetx_drag` only when pointer timing, hit testing, or gesture behavior itself is under test. Do not use raw coordinates for normal menu navigation.

Common traps:

- Do not screenshot before `startup_completed: true`.
- Do not click a background node while a dialog is visible. If the node has no action, first handle the dialog, usually with `dialog.action` or `nav.back`.
- Do not add sleeps to make selector clicks work. If a semantic click needs a sleep, that is a harness or UI refresh bug to investigate.
- Do not assume one storage-warning skip is enough. Use `edgetx_skip_storage_warning_if_present`.
- Do not claim a navigation path works unless the tree changed and at least one screenshot or pixel diff confirms the visual state changed.

## Repeated UI Invariant Analysis

Use the AST-based repeated invariant checker when reviewing UI branches for scattered guard logic or repeated predicates. It uses libclang, so run it through Nix and make sure a native compile database exists first, for example with `nix develop -c tools/ui-harness/edgetx-ui build tx16s`.

Default branch-changed scan:

```sh
nix develop -c python3 tools/check-repeated-if-invariants.py
```

Folder or file scan:

```sh
nix develop -c python3 tools/check-repeated-if-invariants.py radio/src/gui/colorlcd/libui
```

The checker scans real C/C++ `if` statements, decomposes compound conditions into predicate atoms, and reports repeated full conditions, repeated atoms, predicate calls, null/boolean variables, and semantic families such as LVGL object lifetime or window event availability. It is not limited to early-return guards; repeated predicates around `emitEvent`, state mutation, LVGL calls, and other side effects are also reported with the body action shown as context. By default it reports repeats that cross file boundaries and separates production from tests to avoid noise. Use `--show-local` when Bob is explicitly reviewing duplication inside one file.

## Before Commit

Run the CI-equivalent safety sweep before committing firmware, simulator, build-system, or docs changes. Use Nix for every command; do not rely on host tools. Use 24 threads locally via `CMAKE_BUILD_PARALLEL_LEVEL=24`, `--threads=24`, `--jobs 24`, or `-j 24` where the tool supports it.

Common local environment:

```sh
export EDGE16_THREADS=24
export EDGE16_XDG_CONFIG_HOME=/tmp/edge16-xdg
export EDGE16_NIX_HARDENING='bindnow format libcxxhardeningfast pic relro stackclashprotection stackprotector strictflexarrays1 strictoverflow zerocallusedregs'
```

Native tests and sanitizer tests for both supported targets:

```sh
for flavor in tx16s tx16smk3; do
  nix develop -c env XDG_CONFIG_HOME="$EDGE16_XDG_CONFIG_HOME" NIX_HARDENING_ENABLE="$EDGE16_NIX_HARDENING" FLAVOR="$flavor" EDGE16_UV_ACTIVE=1 EXTRA_OPTIONS='-DWARNINGS_AS_ERRORS=YES -DEDGE16_SAFETY_CHECKS=ON -DDISABLE_COMPANION=ON' CMAKE_BUILD_PARALLEL_LEVEL="$EDGE16_THREADS" uv run --with-requirements requirements.txt bash ./tools/commit-tests.sh
  nix develop -c env XDG_CONFIG_HOME="$EDGE16_XDG_CONFIG_HOME" NIX_HARDENING_ENABLE="$EDGE16_NIX_HARDENING" FLAVOR="$flavor" EDGE16_UV_ACTIVE=1 EXTRA_OPTIONS='-DEDGE16_SANITIZERS=address,undefined -DEDGE16_SAFETY_CHECKS=ON -DDISABLE_COMPANION=ON' ASAN_OPTIONS='abort_on_error=1:detect_leaks=0:check_initialization_order=1' UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' CMAKE_BUILD_PARALLEL_LEVEL="$EDGE16_THREADS" uv run --with-requirements requirements.txt bash ./tools/commit-tests.sh
  nix develop -c env XDG_CONFIG_HOME="$EDGE16_XDG_CONFIG_HOME" NIX_HARDENING_ENABLE="$EDGE16_NIX_HARDENING" FLAVOR="$flavor" EDGE16_UV_ACTIVE=1 EXTRA_OPTIONS='-DEDGE16_SANITIZERS=thread -DEDGE16_SAFETY_CHECKS=ON -DDISABLE_COMPANION=ON' TSAN_OPTIONS='halt_on_error=1:second_deadlock_stack=1' CMAKE_BUILD_PARALLEL_LEVEL="$EDGE16_THREADS" uv run --with-requirements requirements.txt bash ./tools/commit-tests.sh
done
```

`-DDISABLE_COMPANION=ON` is a local Nix workaround when Qt6 is unavailable. Drop it only when the Nix shell provides Qt6 and companion is expected to build.

Firmware safety and build checks:

```sh
for flavor in tx16s tx16smk3; do
  nix develop -c env XDG_CONFIG_HOME="$EDGE16_XDG_CONFIG_HOME" NIX_HARDENING_ENABLE="$EDGE16_NIX_HARDENING" GITHUB_REF=refs/heads/local-ci FLAVOR="$flavor" FIRMWARE_TARGET=firmware EDGE16_UV_ACTIVE=1 EXTRA_OPTIONS='-DWARNINGS_AS_ERRORS=YES -DEDGE16_SAFETY_CHECKS=ON' CMAKE_BUILD_PARALLEL_LEVEL="$EDGE16_THREADS" uv run --with-requirements requirements.txt bash ./tools/build-gh.sh
done

nix develop -c env XDG_CONFIG_HOME="$EDGE16_XDG_CONFIG_HOME" NIX_HARDENING_ENABLE="$EDGE16_NIX_HARDENING" GITHUB_REF=refs/heads/local-ci 'FLAVOR=tx16s;tx16smk3' FIRMWARE_TARGET=firmware EDGE16_UV_ACTIVE=1 EXTRA_OPTIONS='-DWARNINGS_AS_ERRORS=YES -DEDGE16_SAFETY_CHECKS=ON -DEDGE16_GCC_ANALYZER=ON' CMAKE_BUILD_PARALLEL_LEVEL="$EDGE16_THREADS" uv run --with-requirements requirements.txt bash ./tools/build-gh.sh

nix develop -c env XDG_CONFIG_HOME="$EDGE16_XDG_CONFIG_HOME" NIX_HARDENING_ENABLE="$EDGE16_NIX_HARDENING" GITHUB_REF=refs/heads/local-ci 'FLAVOR=tx16s;tx16smk3' EDGE16_UV_ACTIVE=1 EXTRA_OPTIONS='-DWARNINGS_AS_ERRORS=YES -DEDGE16_SAFETY_CHECKS=ON' CMAKE_BUILD_PARALLEL_LEVEL="$EDGE16_THREADS" uv run --with-requirements requirements.txt bash ./tools/build-gh.sh
```

Run `clang-tidy` and `cppcheck` after the strict firmware build has produced `build/arm-none-eabi/compile_commands.json`. Match `.github/workflows/build_fw.yml`; use `run-clang-tidy -p build/arm-none-eabi -j 24 ...` and use `nix shell nixpkgs#cppcheck -c cppcheck ...` if `cppcheck` is not in `nix develop`. Keep `--error-exitcode=1`, `.cppcheck-suppressions`, the third-party/translations/tests excludes, and write XML to `build/safety/cppcheck.xml`.

CodeQL firmware analysis is also required before commit when C/C++ firmware code or analyzer config changes. Use the CodeQL CLI from Nix, with packs downloaded into `/tmp/edge16-codeql-packs` if needed:

```sh
NIXPKGS_ALLOW_UNFREE=1 nix shell --impure nixpkgs#codeql -c codeql pack download --dir /tmp/edge16-codeql-packs codeql/cpp-queries@1.6.1 codeql/suite-helpers@1.0.48
NIXPKGS_ALLOW_UNFREE=1 nix shell --impure nixpkgs#codeql -c codeql database create /tmp/edge16-codeql-db --language=cpp --source-root . --overwrite --threads=24 --command "nix develop -c env XDG_CONFIG_HOME=$EDGE16_XDG_CONFIG_HOME NIX_HARDENING_ENABLE='$EDGE16_NIX_HARDENING' GITHUB_REF=refs/heads/local-ci 'FLAVOR=tx16s;tx16smk3' FIRMWARE_TARGET=firmware EDGE16_UV_ACTIVE=1 EXTRA_OPTIONS='-DWARNINGS_AS_ERRORS=YES -DEDGE16_SAFETY_CHECKS=ON' CMAKE_BUILD_PARALLEL_LEVEL=$EDGE16_THREADS uv run --with-requirements requirements.txt bash ./tools/build-gh.sh"
NIXPKGS_ALLOW_UNFREE=1 nix shell --impure nixpkgs#codeql -c codeql database analyze /tmp/edge16-codeql-db .github/codeql/edge16-firmware-safety.qls --additional-packs=/tmp/edge16-codeql-packs --format=sarif-latest --output=/tmp/edge16-codeql.sarif --threads=24
```

After CodeQL, inspect the SARIF and fail the change for any first-party finding. Findings under `radio/src/thirdparty/`, `radio/src/translations/`, and `radio/src/tests/` may be treated the same way CI treats them, but do not ignore first-party warnings.

Policy, docs, and final hygiene:

```sh
nix develop -c python3 tools/check-safe-division.py
NIXPKGS_ALLOW_UNFREE=1 nix shell --impure nixpkgs#semgrep -c semgrep scan --error --jobs 24 --config p/c --config .semgrep/edge16-firmware.yml --include 'radio/src/**' --exclude 'radio/src/thirdparty/**' --exclude 'radio/src/translations/**' --exclude 'radio/src/tests/**'
nix develop -c uv run --with-requirements docs-requirements.txt mkdocs build --strict
git diff --check
```

Do not commit with sanitizer, CodeQL, cppcheck, clang-tidy, GCC analyzer, Semgrep, safe-division, docs, or whitespace failures still present. Treat sanitizer reports from simulator/native tests as real bugs until the root cause proves otherwise.
