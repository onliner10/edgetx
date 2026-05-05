# Codex Notes

- Use `nix develop` for builds, simulator work, and `tools/ui-harness` commands in this repo. The flake provides CMake, Ninja, LLVM/libclang, Python dependencies, SDL2, and X11 headers expected by the native simulator build.
- Prefer commands shaped like `nix develop -c tools/ui-harness/edgetx-ui build tx16s` and `nix develop -c tools/ui-harness/edgetx-ui run-flow tools/ui-harness/flows/tx16s-smoke.json`.
- For firmware builds, use `nix develop -c env FLAVOR=tx16s tools/build-gh.sh` or set `FLAVOR=tx16smk3` for the MK3 target.
- This machine has 24 build threads available. Prefer `cmake --build ... --parallel 24`, or set `CMAKE_BUILD_PARALLEL_LEVEL=24` when invoking project build scripts.
- The UI harness writes simulator builds under `build/ui-harness` by default. Set `EDGETX_UI_BUILD_ROOT=/tmp/edgetx-ui-build` only when you intentionally want build output outside the worktree.

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
