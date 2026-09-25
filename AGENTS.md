# Development Rules

## Conversational Style

- Keep answers short and concise. Technical prose only, be direct. No emojis in commits, issues, PR comments, or code.
- Answer a question first before making edits or running implementation commands.
- When responding to feedback or an analysis, explicitly say whether you agree or disagree before describing changes.
- Prefer concrete behavior and a short trace over abstract summaries. For non-trivial designs explain: problem, example/trace, solution, and why it is necessary (distinguish from optional complexity).

## Code Quality

- Read files in full before wide-ranging changes, before editing files you have not fully inspected, and when asked to investigate or audit. Do not rely on search snippets for broad changes.
- C++11 only (`qcc -Vgcc_ntoaarch64le_cxx`, `-std=c++11`). No features beyond what QCC's libc++ supports; e.g. `ofstream::file()` is unsupported — use `close()` + `fsync_path()`.
- Check the QNX headers under `install/` and the SDP docs before guessing at an API. Do not invent QNX/system calls.
- Inline single-purpose helpers that have only one call site. Prefer the standard library or a native platform feature over reinventing it.
- **OTA invariants (do not break without explicit confirmation):**
  - A/B slot discipline: updates always target the *other* slot than the active one (detected from `config.txt`); never overwrite the IFS currently running.
  - Pending transaction state machine: write `ota_pending` before switching slots; commit the version **only on cold start** when the new slot is confirmed active. Never commit within the same session after a slot switch (that is a false commit).
  - SHA256 sidecar is a hard dependency: every published IFS must have `<file>.bin.sha256` next to it or all updates fail at verification.
  - FAT flushes: after writing `config.txt`, `ota_version`, or `ota_pending`, call `fsync_path()` (open with `O_RDWR`). Atomic config change = write `.tmp`, fsync, `rename`.
- Always ask before removing functionality or code that appears intentional.
- Do not preserve backward compatibility unless the user asks for it.

## Commands

Build (OTA client → IFS → `_B` copy; clean build by default):
```sh
bash build.sh
```

- Never commit unless the user asks.
- For ad-hoc scripts, write them to a temp file (`/tmp`), run, edit if needed, remove when done. Do not embed multi-line scripts in `bash` commands.

## Dependency and Install Security

- No npm/package manager here; external deps are QNX prebuilt libs linked via `LDFLAGS` in `src/ota/Makefile` (`libcurl`, `openssl`, `libsocket`).
- Never add a new dependency for what the standard library or a native platform feature already provides.
- Do not remove/downgrade an existing prebuilt lib to silence a build error; fix the code or upgrade the SDP instead.
- OTA server contract: must serve `/version.txt`, `/ifs-rpi5_v<ver>.bin`, and `<file>.bin.sha256` (single 64-hex hash, one per line).

## Git

Multiple sessions may edit different files in this cwd at once. Staging or touching other sessions' unstaged/untracked work will stomp it. Follow these rules:

Committing:
- Only commit files YOU changed in THIS session.
- Stage explicit paths (`git add <path1> <path2>`); never `git add -A` / `git add .`.
- Before committing, run `git status` and confirm you are staging only your files.
- Message format: `{feat,fix,docs,build,chore}[(ota|images|net|hwstatusd)]: <message>`. English, informative and concise (multiple lines allowed).

Never run (destroys other sessions' work or bypasses checks):
- `git reset --hard`, `git checkout .`, `git clean -fd`, `git stash`, `git add -A`, `git add .`, `git commit --no-verify`.

On rebase conflicts:
- Resolve only files you modified. If a conflict is in a file you did not modify, abort and ask the user.
- Never force push.

## Issues and PRs

- Label affected areas when creating issues/PRs (`ota`, `images`, `net`, `hwstatusd`); use all that apply.
- To auto-close an issue from a commit, include `fixes #<n>` or `closes #<n>` in the message; repeat per issue for multiple closes (a shared keyword closes only the first).
- Inspect PRs with `gh pr diff`, `git show`/`git diff` against fetched refs — do not switch branches unless asked.

## Changelog

Location: `CHANGELOG.md`. Sections under each version: `### Added`, `### Changed`, `### Fixed`, `### Removed`; group entries by area in parentheses, e.g. `### Fixed (OTA Client)`.

Rules:
- New entries go under the current/latest version header; read it first and append — never duplicate subsections.
- Released version sections are immutable; do not modify them.
- Do not add entries on a branch other than `main` / the PR that ships the change.

## Releasing (publishing a new OTA)

1. Bump the version (Tesla-style `YYYY.WW.N`, e.g. `2026.39.2`).
2. Build IFS: `cd images && mkifs -v rpi5.build ifs-rpi5.bin`.
3. Upload to the OTA server, all three required:
   ```sh
   cp <new>.bin  /var/www/ota/ifs-rpi5_v<VER>.bin
   sha256sum /var/www/ota/ifs-rpi5_v<VER>.bin | awk '{print $1}' > /var/www/ota/ifs-rpi5_v<VER>.bin.sha256
   echo "<VER>" > /var/www/ota/version.txt   # last step: this is the update trigger
   chmod 644 /var/www/ota/*
   ```
4. Verify from the board: `curl http://<server>/version.txt` and a fresh OTA log cycle.

## User Override

If the user's instructions conflict with any rule in this document, ask for explicit confirmation before overriding. Only then execute their instructions.
