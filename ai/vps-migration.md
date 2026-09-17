---
status: draft
verified: master @ 12f99f2a9
---
# VPS migration & AI-assisted server setup

**Covers:** the planned replacement of the Bridgewater-Brunel legacy VPS: new-server provisioning
and hardening, OpenCode CLI installation, reproduction of the smoke/test harness off GitHub CI,
artifact provisioning (CI builds plus the live server's native self-build), DNS/web migration and
game-server cutover, and steady-state AI-assisted administration. Facts
about the current VPS and its pipeline: [build-and-toolchain](build-and-toolchain.md);
test-harness mechanics: [scripting-and-tests](scripting-and-tests.md). This doc holds the plan only.

## Status & decisions

- Future plan; not started. Recorded at the user's direction [RECOLLECTION:2026-09-17 user decision].
- Target machine: a **new migration VPS** — not the legacy box, and not a separate test-only
  machine [RECOLLECTION:2026-09-17 user decision].
- Primary goal: **general AI-assisted server administration** — test runs, artifact provisioning,
  backups and security hardening [RECOLLECTION:2026-09-17 user decision].
- **DNS and web presence are kept, not replaced:** the `bridgewater-brunel.me.uk` DNS is
  retained and repointed to the new VPS, and the current contents of `/var/www` (public download
  endpoint, apt repository) migrate to it [RECOLLECTION:2026-09-17 user statement].
- **CI builds serve everything except the live server's own binaries:** provisionally, no
  reproduction of the legacy nightly build pipeline on the new server; the server instead builds
  its own hardware-optimised binaries nightly and restarts with them (Phase 3)
  [RECOLLECTION:2026-09-17 user statement]. Whether anything else must still be built on the
  server is an open question.
- The phased plan below was presented to the user and approved for recording
  [RECOLLECTION:2026-09-17]; individual steps are re-verified against the actual server during
  execution. Plan prose is directive (what will be done), not a claim about the codebase;
  factual claims about the codebase carry inline tags.

## Delivery model: interactive configuration sessions

The work is delivered as **interactive sessions in which the agent gives the user step-by-step
instructions**; the user runs (or explicitly approves) each step on the server, and results are
fed back before the next step is issued [RECOLLECTION:2026-09-17 user statement]. Consequences:

- No autonomous server configuration: the agent proposes each command/change with its purpose;
  nothing touching live services is applied without the user.
- OpenCode CLI runs on the new server under a dedicated non-root user, inside the repo checkout
  (so it picks up ../AGENTS.md and this KB). Headless use over SSH — interactive sessions, plus
  non-interactive `opencode run` for later read-only cron monitoring
  [UNVERIFIED: confirm OpenCode's Linux/headless and non-interactive capabilities against its
  docs at the first session].
- Session outputs are captured as **idempotent, version-controlled setup scripts** wherever
  practical, so the new server's configuration is reproducible — this answers the long-standing
  open question about bringing the VPS scripts under version control
  ([build-and-toolchain](build-and-toolchain.md)).

## Phased plan

### Phase 0 — Provision and harden

- OS: **flexible** — the distro may well be whatever the VPS provider supplies
  [RECOLLECTION:2026-09-17 user statement]. Ubuntu 22.04 would match the CI runner image and
  clang-14 toolchain exactly [CODE master @ 12f99f2a9: .github/workflows/smoke-harness.yml]; on
  any other distro, the toolchain versions in the harness recipe are adjusted and re-verified
  during Phase 2.
- Hardening baseline before any other work: non-root sudo user; SSH key-only auth with root
  login disabled; firewall allowing only SSH, the game port (13353) and HTTP/HTTPS for the
  download endpoint; unattended-upgrades; fail2ban.
- Provider snapshot at baseline, as the rollback point for all later phases.

### Phase 1 — OpenCode installation

- Dedicated unprivileged user (e.g. `simdev`) owns the repo checkouts and all test/build runs;
  the agent never runs as root.
- API key stored in that user's private environment (file mode 0600), never in any file the
  agent reads, writes or commits.

### Phase 2 — Test harness reproduction (first substantive task)

Transcribe the CI smoke-harness recipe into an idempotent, version-controlled script. The
recipe, as run by CI [CODE master @ 12f99f2a9: .github/workflows/smoke-harness.yml,
tsan-smoke.yml]:

1. Packages: `libbz2-dev zlib1g-dev libpng-dev autoconf clang-14 ccache`.
2. `autoconf`, then `CC="ccache clang-14" CXX="ccache clang++-14" ./configure --enable-server`.
3. Build plain makeobj first; then append `DEBUG = 2` plus sanitizer flags to `config.default`
   and build the engine (`-fsanitize=address,undefined -fno-sanitize-recover=all
   -fno-sanitize=shift,function` for the ASan variant; `-fsanitize=thread` for the TSan variant).
4. Check out `jamespetts/simutrans-pak128.britain` at the pin in `tests/pakset-pin.tab`; compile
   it with the freshly built makeobj; symlink into `simutrans/`.
5. Run `scripts/run-smoke-tests.sh --mode network` (fixture `tests/demo.sve`, horizon 1945.6).

Server-specific adaptations:

- **Port:** the CI invocation passes `--server-port 13353`, the port the live game server is
  recorded as using ([build-and-toolchain](build-and-toolchain.md)); test runs on the new box
  must use a different port once the live server is installed there.
- **Resources:** builds and test runs get resource guardrails (systemd slice / nice / memory
  limits) so they can never starve the game server. TSan runs are several times slower than
  ASan [CODE master @ 12f99f2a9: tsan-smoke.yml]; their memory overhead on the new hardware is
  to be measured at the first run.
- Exit criterion: green smoke runs on both `master` and `ex-15`.

### Phase 3 — Artifacts: CI for publication, native self-build for the live server

- **Public artifacts come from CI**, not from the server: client builds, Windows cross-builds,
  makeobj, nettool and paksets are produced by the GitHub workflows
  ([build-and-toolchain](build-and-toolchain.md)); provisionally, nothing of the legacy nightly
  build pipeline is reproduced on the new server
  [RECOLLECTION:2026-09-17 user statement]. How CI artifacts reach the public download endpoint
  is an open question — GitHub's own nightlies publish GitHub releases and track master only
  ([build-and-toolchain](build-and-toolchain.md)).
- **The live server builds its own binaries nightly and restarts with them**, hyper-optimised
  for its own hardware [RECOLLECTION:2026-09-17 user statement]:
  - Scope: the server binary, and nettool (used by the server operations scripts —
    [build-and-toolchain](build-and-toolchain.md)).
  - Build: GNU-make server configuration (the legacy `CFG=server` pattern: posix backend,
    headless — [build-and-toolchain](build-and-toolchain.md)) plus `TUNE_NATIVE`
    (`-march=native -mtune=native`), `OPTIMISE` (`-O3`) and optionally `LTO`
    [CODE master @ 12f99f2a9: Makefile].
  - Schedule: the quietest time of day, circa 04:00 GMT
    [RECOLLECTION:2026-09-17 user statement], followed by the nightly restart (legacy restart
    mechanics: [build-and-toolchain](build-and-toolchain.md)).
  - **Revision constraint:** the self-build must come from the same commit as the published
    client builds (master, as under the legacy pipeline) — the `REVISION` define must match
    between server and clients for network play
    ([build-and-toolchain](build-and-toolchain.md)).
  - **Determinism check before enabling:** synced simulation state is integer/fixed-point only
    (no-floating-point rule: [project-architecture](project-architecture.md)), so native
    optimisation is expected to be determinism-neutral; verify by running the network
    determinism smoke test with the native-optimised server against a standard-built client
    before the self-build goes live.
  - Safeguard: a failed or bad nightly build must not take the live server down — keep the
    previous binaries and a restart-with-fallback path.
- Fallback, if the pipeline-scope open question resolves to "some pipeline must run on the
  server": inventory the legacy box first (crontab, `/root` scripts, MXE tree, source-built
  dependency trees), then reproduce each stage as a version-controlled script, fixing the
  recorded broken items (Windows nettool `.exe` copy failure; hardcoded `.deb` version in
  `package.sh`) instead of copying them, and verify artifacts against the legacy box before
  cutover. The mingw64 cross-build environment would be the largest effort item.

### Phase 4 — Game server cutover

- Reproduce the live server installation, savegame and the restart/backup-rotation scripts;
  address the recorded security concerns (plaintext nettool password in scripts, privilege
  levels) as part of this phase.
- Migrate the current contents of `/var/www` (public download endpoint, apt repository) to the
  new VPS [RECOLLECTION:2026-09-17 user statement].
- Parallel-run and verify, then **repoint the `bridgewater-brunel.me.uk` DNS to the new VPS** —
  the domain is kept [RECOLLECTION:2026-09-17 user statement]; keep the legacy box alive for a
  fallback window.

### Phase 5 — Steady state

- AI administration by proposal: the agent produces scripts and diffs; the user applies anything
  that mutates live services.
- Non-interactive (`opencode run`) cron usage limited to read-only monitoring: logs, disk usage,
  update availability.

## Risks

1. **Nightly self-build** — a bad build must not take the live server down (previous binaries
   kept, restart-with-fallback); revision match and determinism verified per Phase 3.
2. **mingw64 cross-environment reproduction** — applies only if the pipeline-scope open question
   resolves to reproducing the legacy pipeline on the server; mitigated by inventory-first and
   keeping the legacy box until artifact parity is proven.
3. **Secrets handling** — nettool passwords, SSH keys and API keys must never enter version
   control or agent-readable files; a recorded existing concern on the legacy box.
4. **Agent blast radius** — mitigated by the dedicated non-root user, provider snapshots and the
   supervised-mutation rule.

## Open questions

- **Pipeline scope on the server:** provisionally, CI covers all published artifacts and only the
  live server's own binaries are built on the server
  [RECOLLECTION:2026-09-17 user statement, provisional]. Confirm that nothing else — themes,
  paksets, `.deb` packaging, apt-repository regeneration — needs a server-side pipeline.
- **Artifact publishing:** how CI-built artifacts reach the new VPS's web root (deploy step from
  CI, or a scheduled pull on the server), and how the apt repository is regenerated there.
- OS/distro: likely provider-supplied; the harness recipe's toolchain versions are adapted during
  Phase 2.
- Should the new server also act as a GitHub self-hosted runner, or stay off GitHub CI?
  Self-hosting would give workflow-triggerable code execution on the production box — a security
  trade-off to weigh.
- TSan resource requirements (memory, wall time) on the new hardware — measure at the first run.
- Migration timing and trigger (legacy-tier end-of-life, resource limits, security work) — not
  yet decided.
