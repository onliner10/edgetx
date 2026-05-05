# Edge16 GitHub Actions

Edge16 CI and release automation intentionally supports only these radio targets:

| Radio | Build flavor | Release artifacts |
| --- | --- | --- |
| RadioMaster TX16S MK2 | `tx16s` | `tx16s-*.bin` |
| RadioMaster TX16S MK3 | `tx16smk3` | `tx16smk3-*` |

Do not add generic EdgeTX targets to firmware, nightly, release, or Companion
module workflows unless Edge16 product support has explicitly changed.

## Workflow roles

| Workflow | Purpose |
| --- | --- |
| `build_fw.yml` | Pull request and tag firmware CI for TX16S MK2/MK3. |
| `nightly.yml` | Scheduled nightly firmware release for TX16S MK2/MK3. |
| `publish_tx16s_firmware.yml` | Manual prerelease publisher for TX16S MK2/MK3 firmware. |
| `companion.yml` | Companion builds using TX16S MK2/MK3 WASM modules. |
| `release-drafter.yml` | Drafts Edge16 releases and attaches only Edge16 release artifacts. |
| `validate_fw_json.yml` | Enforces that `fw.json` lists exactly TX16S MK2 and TX16S MK3. |
| `sync_repos.yml` | Manual-only destructive upstream sync; not part of release CI. |
