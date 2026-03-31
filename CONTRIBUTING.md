# Contributing to rust-ppc-tiger

Thanks for contributing to `rust-ppc-tiger`.

This repository is not a typical Rust crate. Most work here happens in:

- large C sources that implement the PowerPC compiler and runtime
- shell scripts that build Tiger or Leopard tooling and Firefox-related experiments
- a small Python helper (`cargo_fetch.py`) for dependency fetching and vendoring
- tiny smoke-test fixtures under `tests/`

Please keep contributions focused, easy to review, and tied to the repository's actual PowerPC Tiger or Leopard goal.

## Good Contribution Areas

- compiler fixes in files such as `rustc_100_percent.c`, `rustc_build_system.c`, and the feature-specific `rustc_*.c` modules
- build tooling improvements in scripts like `cargo_ppc.sh`, `build_pocketfox.sh`, `firefox_ppc_build.sh`, or the Tiger helper scripts
- compatibility fixes for PowerPC, AltiVec, Tiger, or Leopard assumptions
- documentation updates that match the current scripts and repository layout
- small test fixtures or smoke checks in `tests/`

## Before You Start

- Read [README.md](README.md) first so your change matches the current roadmap and hardware assumptions.
- If you are editing build logic, skim the relevant script end-to-end before patching it.
- Keep one PR to one logical change. A small docs fix, a shell-script fix, and a compiler refactor should not ship together.

## Local Setup

Most contributors will work from a modern machine even if the target is PowerPC Tiger or Leopard hardware.

Recommended baseline tools:

- `bash`
- `gcc`
- `python3`
- `git`

If you have real Tiger or Leopard hardware, mention that in the PR. Real-machine validation is especially useful for changes touching compiler output, AltiVec flags, SDK paths, or Firefox and PocketFox build flows.

## Fast Validation

Run the lightest checks that fit your change before opening a PR.

For shell-script changes:

```bash
bash -n cargo_ppc.sh build_pocketfox.sh firefox_ppc_build.sh \
  build_curl_mbedtls.sh build_git_tiger.sh build_openssh_tiger.sh \
  build_rsync_tiger.sh tiger_toolkit_installer.sh
```

For Python helper changes:

```bash
python3 -m py_compile cargo_fetch.py
```

For docs-only changes:

```bash
git diff --check
```

If your change is scoped to a specific file or script, it is fine to run a narrower version of the relevant command and mention that in the PR body.

## Change Guidelines

- Preserve Tiger or Leopard compatibility assumptions unless your PR explicitly updates them.
- Do not silently rewrite large generated or hand-tuned C files just for formatting.
- Avoid committing new build outputs, extracted archives, or temporary directories unless the PR is specifically about tracked artifacts.
- Keep README and script usage in sync. If a script supports `firefox`, `bridge`, or `package`, the docs should say the same thing.
- If you add a new helper script, document the expected entrypoint and the fastest smoke test.

## Pull Request Expectations

In your PR description, include:

- what changed
- why it was needed
- the exact command or commands you used for verification
- whether the change was tested on modern macOS only, or also on real Tiger or Leopard hardware

Small, targeted PRs are much more likely to be reviewed and merged than broad cleanup bundles.
