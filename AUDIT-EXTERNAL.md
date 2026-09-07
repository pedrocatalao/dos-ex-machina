# DOS ex Machina — Code & Architecture Audit

**Audit date:** 2026-09-06  
**Repository:** `pedrocatalao/dos-ex-machina`  
**Audited branch:** `main`  
**Audited revision:** `2447e9a88cb1fcaac7edf685fba9f114966bb526`  
**Scope:** Architecture, C code quality, portability, concurrency, networking, catalogue/install pipeline, native module ABI, archive extraction, CI/CD, security model, maintainability, and production readiness.

---

## Executive summary

DOS ex Machina is unusually well structured for a project at this stage of development. The current codebase is substantially more mature than the older audit suggests: large implementation units have been decomposed, the native-core ABI is deliberately small and platform-neutral, formatting is enforced, unit and golden tests exist, sanitizer builds execute the application, and Linux/macOS CI performs real packaging and verification.

A key clarification materially changes the security assessment in the first version of this audit:

> **The official DXM catalogue is curated. Only the project owner can publish catalogue entries, and each game/core is audited before being accepted into the catalogue.**

Under that model, DOS ex Machina is **not intended to safely execute arbitrary third-party plugins**. It is a curated native-code platform. The catalogue is the project's authorization boundary, while the SHA-256 values in that catalogue bind an approved release to the exact bytes DXM is allowed to install.

That makes several previously high-severity findings defense-in-depth concerns rather than fundamental blockers. In particular, catalogue signing, ZIP expansion limits, and strict download-size enforcement remain worthwhile hardening, but their practical security severity is substantially lower when every published artifact is reviewed before catalogue inclusion.

The most important remaining pre-1.0 work is therefore **native-code correctness and robustness**, especially thread synchronization and framebuffer ownership.

### Overall assessment

| Area | Score | Assessment |
|---|---:|---|
| Architecture | **9.0/10** | Excellent separation for the project's size |
| Code quality | **8.5/10** | Clean, disciplined native C |
| Build / CI | **9.0/10** | Strong multi-platform pipeline |
| Testing | **8.0/10** | Good and improving; fuzzing would strengthen parser coverage |
| Portability | **8.5/10** | Platform abstractions are generally well handled |
| Defensive programming | **8.0/10** | Good foundations; several boundary cases remain |
| Security model | **8.0/10** | Appropriate for a curated, owner-controlled native-code catalogue |
| Maintainability | **9.0/10** | Strong modularization and documentation |
| **Overall** | **8.5/10** | **Production-capable; close to a 1.0-quality release** |

### Verdict

**Would I release this publicly? Yes.**

**Is the security model reasonable for 1.0? Yes, provided the curated-catalogue trust model is explicit.**

I would **not** consider catalogue signing a mandatory 1.0 blocker under the stated operating model. It is valuable defense in depth against compromise of the catalogue publishing path, but it is not required to make the existing SHA-256 verification meaningful.

The issues I would prioritize before declaring 1.0 are:

1. Replace cross-thread `volatile` state with real synchronization.
2. Resolve framebuffer ownership/data-race semantics.
3. Improve robustness around non-cooperative core shutdown and failure paths.

Archive limits, download-size enforcement, catalogue signing, parser fuzzing, and CI supply-chain pinning remain recommended hardening work.


# 1. The good

## 1.1 Architecture is genuinely strong

The current source tree has clear subsystem boundaries rather than one growing application file. Responsibilities are separated across orchestration, rendering, DOS UI/shell/boot/navigation, chassis/theatre presentation, networking, catalogue/install handling, ZIP/PNG/SHA utilities, native-core hosting/loading, and the public module contract.

This is appropriate modularity rather than abstraction for abstraction's sake.

`main.c` acts as orchestration instead of becoming the implementation of every subsystem. That is exactly the direction a project like this should take as more games and platform features are added.

The architecture document also establishes useful size/responsibility discipline. More importantly, the implementation now broadly reflects the documented architecture, which was not true of several concerns recorded in the historical audit.

**Assessment: excellent.**

---

## 1.2 The `.dxm` ABI is one of the best-designed parts of the project

`contract/dxm_core.h` provides a deliberately small C ABI with no SDL or OpenGL types exposed to game modules.

That is an important architectural decision.

The contract provides:

- explicit ABI versioning (`DXM_ABI 2`),
- a stable `dxm_core_info` structure,
- framebuffer format/mode descriptions,
- host presentation/input/time/logging/path services,
- host locking primitives,
- three fixed exported entry points.

The exported interface is small enough that future ports do not need to know how the DOS ex Machina shell, SDL window, GPU renderer, chassis, catalogue, or platform packaging works.

Using a pure-C ABI is also the right choice for long-term compatibility across compilers and languages.

The fact that `abi` is deliberately the first member of the info structure is a small but good compatibility detail.

**Assessment: excellent foundation for an ecosystem of native ports.**

---

## 1.3 Dynamic loading is restrained and sensible

The loader resolves a very small known symbol set and validates the module ABI before attempting to run it.

On POSIX systems the use of:

```c
dlopen(path, RTLD_NOW | RTLD_LOCAL)
```

is a good choice. `RTLD_LOCAL` avoids unnecessarily exposing module symbols into the process-global namespace.

Windows follows the equivalent native loading model.

The implementation does not attempt to build an unnecessarily complicated plugin framework around a problem that only requires a handful of exported functions.

**Assessment: simple and appropriate.**

---

## 1.4 Network downloads have several good defensive properties

The libcurl wrapper keeps TLS certificate validation enabled and bounds redirects. HTTP errors are treated as failures rather than blindly accepting whatever body was returned.

Downloads use temporary `.part` files and only rename them after a successful transfer. This is substantially better than writing directly over the destination.

Memory downloads also have an explicit memory cap.

The installer then verifies SHA-256 before accepting downloaded modules and data archives.

This means accidental corruption, CDN/proxy corruption, incomplete downloads, or tampering that does not also alter the trusted manifest is detected.

**Assessment: good integrity engineering.**

---

## 1.5 ZIP extraction already anticipates path traversal

The ZIP implementation is deliberately constrained and rejects dangerous filenames, including:

- Unix absolute paths,
- Windows absolute paths,
- drive-qualified paths,
- `..` traversal components,
- control characters.

Flattening the archive into the game data directory further reduces the attack surface for ZIP-slip style attacks.

The compressed archive itself is also subject to a size limit.

This is considerably better than a naive “unzip whatever the server supplied” implementation.

**Assessment: good security instincts, with resource-limit issues discussed later.**

---

## 1.6 Filesystem handling is mostly defensive

The library subsystem uses SDL's filesystem facilities rather than spreading platform-specific directory code throughout the application.

`path_join()` explicitly fails when the result cannot fit rather than silently accepting a truncated `snprintf()` path. This is a meaningful improvement over the concern recorded in the earlier audit.

Installed modules are unloaded before deletion, which is particularly important on Windows.

The reset/re-extraction workflow is also straightforward and understandable.

**Assessment: good cross-platform implementation.**

---

## 1.7 CI/CD is much stronger than a typical project of this size

The Linux workflow currently performs substantially more than a compile check.

It includes:

- x86-64 and ARM64 builds,
- SDL3 build/cache handling,
- generated-file verification,
- `clang-format` verification,
- warnings-as-errors,
- unit tests,
- executable verification,
- OpenGL runtime-link checks,
- libcurl checks,
- packaging,
- dependency verification,
- release artifact publication.

The sanitizer workflow is especially valuable because it does not merely build with ASan/UBSan: it runs tests and actual application/core scenarios, including golden rendering tests using software OpenGL.

That addresses one of the largest weaknesses identified in the old audit: code was previously much less exercised automatically.

The current revision also has successful Linux and macOS workflow runs.

**Assessment: excellent engineering discipline.**

---

# 2. The bad

These are real weaknesses, but none suggests the project is badly engineered.

## 2.1 `volatile` is being used as thread synchronization

This is the most important correctness issue in the current C implementation.

Several values shared between SDL threads are declared `volatile`, including state such as:

- running/quit flags,
- current framebuffer index,
- key state,
- character queue indices,
- catalogue publication state,
- installer cancellation state.

In C, `volatile` does **not** make cross-thread access safe.

It prevents certain compiler optimizations around an object, but it does not provide atomicity, memory ordering, or a happens-before relationship between threads.

Code that happens to work reliably on current x86 machines can therefore still contain a formal C data race and behave differently under another compiler, optimization level, or architecture.

### Recommendation

Use one of:

- SDL atomic primitives,
- C11 `_Atomic`,
- the mutex already associated with the relevant state.

Do not mechanically convert every variable to an atomic. Decide which state represents lock-free signalling and which state belongs under a mutex.

### Severity

**High for correctness / medium for practical current-user risk.**

This should be fixed before 1.0.

---

## 2.2 Framebuffer ownership deserves tighter synchronization

The framebuffer uses double buffering. The producer writes the back buffer and swaps `front`/`back` under a mutex. The consumer locks briefly to discover the front buffer, unlocks, and then uses the returned pointer.

That leaves a possible lifetime/ownership race.

After the consumer releases the mutex, the producer can publish another frame and eventually begin writing into the buffer the consumer is still reading.

Whether visible corruption occurs depends on timing, but the ownership model is weaker than it appears.

### Recommendation

Possible solutions include:

- copy/upload the selected frame while the relevant lock is held,
- triple buffering with explicit ownership/generation state,
- an atomic generation protocol where producer buffers are not reused until the consumer has advanced.

For this application, triple buffering is probably the cleanest compromise between simplicity and avoiding renderer/core stalls.

### Severity

**Medium.**

---

## 2.3 Core shutdown can still block forever

`corehost_stop()` requests that the core quit and waits for approximately two seconds for cooperative termination.

However, if the core never exits, the implementation eventually calls `SDL_WaitThread()` anyway.

Therefore the apparent timeout is not actually a hard timeout. A broken native module can still hang application shutdown indefinitely.

This is fundamentally difficult to solve safely while arbitrary native code runs in-process. Killing a thread containing unknown C code is unsafe.

### Recommendation

For the current trusted-core architecture:

- document that cores must cooperatively honor `should_quit`,
- report cores that exceed the expected shutdown interval,
- consider a watchdog/error UI.

Long term, process isolation is the only robust solution if untrusted third-party cores are ever supported.

### Severity

**Medium.**

---

## 2.4 Mutex allocation failures are not consistently handled

The core host creates mutexes and a worker thread, but mutex creation failures should be explicitly checked before the thread can use them.

Memory/resource exhaustion is unusual on a desktop system, but failure paths should remain deterministic rather than turning into null-mutex use.

### Recommendation

Check every SDL resource creation call and unwind already-created resources on failure.

### Severity

**Low.**

---

## 2.5 The RGB framebuffer capacity/comment appears inconsistent

The core RGB storage is sized as:

```c
320 * 400 * 3
```

bytes per buffer.

That stores a 320×400 RGB888 framebuffer, not a 640×400 RGB framebuffer.

A comment describing it as generous for 640×400 is therefore incorrect, and a valid larger RGB mode would fail the capacity test.

### Recommendation

Either:

- size the buffer according to the maximum mode the ABI intentionally supports,
- dynamically allocate it from the advertised mode,
- or explicitly constrain/document the supported maximum.

Also correct the comment so the contract and implementation cannot drift silently.

### Severity

**Low to medium depending on planned video modes.**

---

## 2.6 Catalogue parsing is intentionally minimal but becoming technical debt

The handwritten JSON parser is admirably small and easy to audit for the current controlled schema, but it is not a complete JSON implementation.

For example, Unicode escape handling is intentionally incomplete.

The unknown-value skipping logic is also simpler than a general parser and becomes increasingly fragile if future catalogue versions introduce nested structures of mixed types.

String truncation is generally tolerated rather than rejected, which can turn malformed catalogue data into confusing downstream behavior.

### Recommendation

This does not need immediate replacement.

If the catalogue schema grows, migrate to a small mature JSON parser or make the existing parser explicitly schema-strict:

- reject overlong mandatory values,
- validate IDs,
- validate SHA-256 length/characters,
- validate URLs,
- validate numeric ranges,
- reject unsupported structures rather than attempting loose forward compatibility.

### Severity

**Low now, medium as the catalogue evolves.**

---

# 3. The ugly

With the clarified trust model, there is considerably less here that qualifies as an architectural security flaw. The remaining concerns are mostly about making the trusted-content boundary explicit and adding defense in depth.

## 3.1 The catalogue is the authorization boundary

The application retrieves the official catalogue from the project repository. Catalogue entries specify the downloadable native module/data and their expected SHA-256 hashes.

The important operational fact is:

> **Catalogue inclusion is an approval decision. A game/core is audited before it is added, and catalogue publication is controlled by the project owner.**

That changes the interpretation of SHA-256 substantially.

For an approved release:

```text
audited artifact
      │
      ├── SHA-256 calculated at approval time
      │
      ▼
official catalogue
      │
      ▼
DXM downloads artifact
      │
      ▼
DXM calculates SHA-256
      │
      ├── match    → exact approved bytes
      └── mismatch → reject
```

Therefore SHA-256 does exactly the important job expected of it here: it prevents a downloadable asset from being silently changed after approval while continuing to be accepted by DXM.

It protects against, among other things:

- accidental replacement of a release asset,
- corruption,
- modified mirrors/origins,
- an attacker who can alter the downloadable asset but **cannot** alter the trusted catalogue.

What SHA-256 does **not** protect against is compromise of the catalogue publishing authority itself. An attacker able to change both the catalogue URL/hash and the downloadable asset can publish a new malicious artifact with its matching hash.

That is a different threat: compromise of the project's root of trust.

### Recommendation

Keep the current SHA-256 verification.

Catalogue signing remains a worthwhile future hardening measure, particularly if the signing private key is kept outside the GitHub/repository credentials used to publish the catalogue. It would separate:

```text
repository/catalogue publishing authority
```

from:

```text
release authorization authority
```

But under the current owner-controlled catalogue model, I would classify signing as **defense in depth rather than a 1.0 blocker**.

### Severity

**Low-to-medium under the stated curated catalogue model.**

**High only if catalogue publication becomes open, delegated, or insufficiently controlled.**

---

## 3.2 Native `.dxm` modules are intentionally trusted native code

A `.dxm` module is a native dynamic library loaded into the DOS ex Machina process.

The ABI restricts how a well-behaved core interacts with DXM, but it does not sandbox hostile native code. A module can call operating-system APIs directly and executes with the user's privileges.

This is not inherently a flaw.

Given the stated publishing model, the security boundary is **catalogue admission**, not the ABI:

```text
unreviewed game/core
        │
        ▼
maintainer audit
        │
        ├── rejected → never enters official catalogue
        │
        └── approved
               │
               ▼
       exact release hashed
               │
               ▼
        official catalogue
               │
               ▼
       DXM verifies SHA-256
               │
               ▼
          native execution
```

That is comparable to many curated native-software distribution models: code is trusted because it was reviewed/authorized for distribution, not because the runtime can safely execute arbitrary hostile binaries.

### Recommendation

Document this explicitly so users and future contributors do not mistake the ABI for a sandbox:

> DOS ex Machina cores are native executable code. Cores in the official catalogue are reviewed and approved before publication. DXM verifies downloaded artifacts against the SHA-256 recorded for the approved release. Manually loading cores obtained outside the official catalogue should be treated like running arbitrary native software.

Process isolation is only necessary if DXM later intends to support arbitrary or independently published untrusted cores.

### Severity

**Expected architectural property, not a vulnerability under the current model.**

---

## 3.3 ZIP expansion limits are still good robustness engineering

The ZIP implementation limits the compressed archive size and already rejects dangerous output paths, but it does not impose equally strong limits on total expanded data, individual expanded files, or file count.

Under an untrusted-content model this would be a meaningful resource-exhaustion security issue.

Under the official DXM model, however, archives are part of an audited catalogue release and their exact bytes are SHA-256 pinned. A malicious archive cannot simply replace the approved archive and remain valid.

The more realistic risks are therefore:

- an accidentally pathological archive being approved,
- parser bugs triggered by unusual but legitimate content,
- future changes to the publishing model,
- manually supplied/unofficial content.

### Recommendation

Still add explicit limits, for example:

```c
#define ZIP_MAX_FILE       (256ULL * 1024 * 1024)
#define ZIP_MAX_EXPANDED  (1024ULL * 1024 * 1024)
#define ZIP_MAX_FILES      10000
```

Tune the values to actual game packages and use overflow-safe accounting.

This is inexpensive hardening and makes the extractor safer independently of catalogue policy.

### Severity

**Low-to-medium under the curated catalogue model.**

**High if arbitrary untrusted archives are ever accepted.**

---

## 3.4 Download-size enforcement is defense in depth

The catalogue carries size information, while the download layer ultimately relies on SHA-256 to determine whether the received file is the approved artifact.

The hash prevents a different file from being installed, but it does not prevent an endpoint from sending excessive data before verification fails.

Under the current model, the approved URL is itself reviewed as part of catalogue publication, so this is not a major practical threat.

### Recommendation

Pass the expected size into the download layer and abort once:

```text
received_bytes > expected_bytes
```

Then require:

```text
received_bytes == expected_bytes
```

before SHA-256 verification.

This improves failure behavior and protects against server/CDN mistakes without changing the trust architecture.

### Severity

**Low-to-medium.**


# 4. Additional hardening opportunities

## 4.1 Validate catalogue IDs before using them as filesystem names

Game IDs eventually influence paths below the local games directory.

They should therefore have an explicit grammar rather than merely being JSON strings.

For example:

```text
[A-Za-z0-9][A-Za-z0-9_-]{0,63}
```

This prevents accidental separators, `..`, unusual Unicode/path behavior, truncated collisions, and platform-specific filename surprises.

The same principle should apply to any catalogue field that becomes a local filename.

---

## 4.2 Make catalogue cache replacement atomic

Downloaded binaries already use `.part` + rename semantics.

The catalogue cache should follow the same pattern:

```text
catalogue.json.tmp
        ↓ fsync/close
atomic rename
        ↓
catalogue.json
```

This prevents a crash/power loss during writing from replacing a previously valid cache with a truncated file.

With signatures added, only persist the catalogue after signature and schema validation succeeds.

---

## 4.3 Consider transactional installs

Installation currently has enough state awareness to identify an unfinished installation, which is good.

A cleaner long-term model would install everything into a staging directory:

```text
games/.installing-skyroads-XXXX/
```

Then, only after module verification, data verification, extraction, and validation succeed:

```text
rename(staging, games/skyroads)
```

This makes installation almost transactional and greatly simplifies recovery from interruption.

---

## 4.4 Reject ZIP output collisions

Because archive paths are flattened, two entries with different original paths can potentially resolve to the same output filename.

The extractor should detect duplicate destination names and reject the archive instead of depending on extraction order/overwrite behavior.

---

## 4.5 Pin CI dependencies more aggressively

The GitHub Actions configuration is already strong functionally, but release infrastructure can be hardened further.

Consider:

- pinning third-party GitHub Actions to immutable commit SHAs,
- pinning SDL source to a known commit rather than only a mutable Git tag,
- giving the workflow `contents: read` by default,
- granting `contents: write` only to the release publication job that needs it.

This protects the build/release supply chain rather than the application runtime itself.

---

## 4.6 Add fuzz testing

The highest-value fuzz targets are parsers exposed to externally supplied bytes:

1. ZIP parser/extractor,
2. PNG decoder,
3. catalogue JSON parser.

libFuzzer + ASan/UBSan would fit the current toolchain naturally.

Even a small persistent corpus run in CI would likely find boundary cases that ordinary unit tests never exercise.

---

# 5. Testing assessment

The testing situation has improved dramatically compared with the historical audit.

The project now has multiple useful layers:

```text
format/generated checks
        ↓
compiler warnings-as-errors
        ↓
unit tests
        ↓
ASan / UBSan
        ↓
headless application execution
        ↓
core launch/unwind/relaunch tests
        ↓
golden rendering tests
        ↓
platform packaging verification
```

This is a good strategy for a graphical native application because it tests more than isolated functions.

### Still missing

The main missing test class is adversarial input testing.

Recommended additions:

- fuzz ZIP input,
- fuzz PNG input,
- fuzz catalogue input,
- malformed `.dxm` metadata tests,
- deliberately non-cooperative core test,
- repeated install/cancel/remove cycles,
- interrupted/corrupt catalogue cache tests,
- extreme framebuffer mode metadata tests,
- concurrency stress tests where feasible.

---

# 6. Maintainability assessment

Maintainability is one of the project's strongest qualities.

The implementation has avoided two common failure modes of hobby/native projects:

1. one enormous application source file,
2. an abstraction-heavy “engine” that becomes harder to understand than the application.

The current modules mostly correspond to real domain concepts.

That matters because DOS ex Machina can grow in at least three dimensions simultaneously:

- additional games/cores,
- richer shell/DOS behavior,
- richer visual/hardware simulation.

The current architecture provides reasonable seams for all three.

`ARCHITECTURE.md` is particularly valuable because it records the intended ownership of those seams before the project becomes much larger.

### Recommendation

Keep the architectural rule simple:

> A module should own one recognizable DXM concept, not merely exist because a source file became long.

The current decomposition broadly follows that principle.

---

# 7. Production readiness

“Production-ready” should be judged against the actual product and its intended trust model.

DOS ex Machina is a curated native application/runtime, not a hostile-code sandbox. Under that model, the current application already has many properties expected of production software:

- deterministic builds,
- multi-platform CI,
- warnings-as-errors,
- automated tests,
- sanitizer execution,
- package verification,
- explicit ABI versioning,
- controlled module loading,
- pre-publication review of catalogue content,
- SHA-256 pinning of approved downloadable artifacts,
- documented architecture,
- defensive archive path handling,
- structured error handling,
- platform abstraction.

## Current classification

**Production-capable and close to a 1.0-quality release.**

I would be comfortable distributing the application to real users under the stated curated-catalogue model.

The remaining concerns that most affect a 1.0-quality judgement are not primarily catalogue security concerns. They are C concurrency and runtime robustness concerns.

## Before 1.0

### Highest priority

1. **Replace cross-thread `volatile` synchronization.**
2. **Resolve framebuffer ownership/data-race semantics.**
3. **Harden non-cooperative core shutdown and mutex/resource failure paths.**

### Strongly recommended

4. Limit ZIP expanded size/file count.
5. Enforce catalogue-declared download sizes.
6. Validate catalogue IDs/path-derived fields.
7. Explicitly document the curated native-core trust model.
8. Reject ZIP output collisions / improve install transactionality.

### Defense in depth / can happen after 1.0

9. Cryptographically sign the catalogue.
10. Add parser fuzzing to CI.
11. Replace/upgrade the handwritten JSON parser if the schema grows.
12. Harden CI dependency/action pinning.
13. Consider process isolation only if an open untrusted third-party core ecosystem becomes a goal.

---

# 8. Security model for 1.0

The practical 1.0 security model can be simple and defensible:

```text
                    unreviewed game/core
                            │
                            ▼
                    maintainer audit
                            │
                    approve / reject
                            │
                            ▼
                 approved release artifact
                            │
                    calculate SHA-256
                            │
                            ▼
                    official catalogue
                            │
                            ▼
                    DXM downloads bytes
                            │
                            ▼
                    SHA-256 verification
                            │
                 mismatch ──┴── match
                    │              │
                  reject           ▼
                            native execution
```

The core security statement is:

> **DOS ex Machina's official catalogue is curated. Game cores and data packages are reviewed before publication. The catalogue records the SHA-256 of the approved release, and DXM verifies downloaded artifacts before installation/execution. Game cores are native code and execute with the user's privileges. Content obtained outside the official catalogue is outside this trust model.**

### What SHA-256 guarantees

For a trusted catalogue entry, SHA-256 establishes that the downloaded artifact is the exact artifact represented by the catalogue hash to a cryptographically negligible probability of accidental or adversarial collision.

In practical terms:

```text
approved bytes == downloaded bytes
```

or DXM rejects the download.

### What SHA-256 does not guarantee

SHA-256 does not determine whether those approved bytes are safe.

That decision is made during the pre-catalogue audit.

It also cannot protect against an attacker who has compromised the authority used to modify the catalogue itself and can therefore replace both the artifact reference and its expected hash.

### Optional future strengthening

Catalogue signatures can add a second authorization layer:

```text
offline/separate signing key
          │
          ▼
    signed catalogue
          │
          ▼
    SHA-256-pinned assets
```

This is valuable if the project wants protection against repository/account/token compromise, delegated catalogue maintenance, mirrors, or a larger publishing operation.

It is **not necessary to justify the current curated model**.


# 9. Comparison with the previous audit

The old `AUDIT.md` should now be considered a historical document rather than a description of the current codebase.

Several important earlier weaknesses have already been addressed.

| Earlier concern | Current state |
|---|---|
| Large source files / architectural drift | **Substantially improved** through subsystem split |
| No meaningful tests | **Fixed** — unit/golden/runtime tests now exist |
| Sanitizer build without meaningful execution | **Fixed** — sanitizer CI executes real scenarios |
| Embedded/generated shader concerns | **Improved** through generated-source checks |
| Formatting inconsistency | **Fixed** with `.clang-format` + CI check |
| Path truncation concerns | **Improved** with defensive `path_join()` behavior |
| Weak architecture documentation | **Fixed** with current `ARCHITECTURE.md` |
| Download integrity | **Good** SHA-256 verification now forms part of install flow |
| Catalogue authenticity | **Acceptable for current owner-controlled model**; signatures would add defense in depth |
| Parser attack surface | **Improved but still worth fuzzing** |

This is important: the project has not merely accumulated features since the earlier audit. Its engineering structure has materially improved.

---

# 10. Priority roadmap

If I were preparing this exact revision for 1.0, I would work in this order:

### P0 — Correct concurrency primitives

Remove cross-thread `volatile` synchronization and use SDL atomics, C11 atomics, or mutexes according to the ownership model.

### P0 — Frame publication correctness

Make framebuffer ownership explicit so the producer cannot overwrite a buffer while the renderer is still consuming it.

### P1 — Core robustness

Improve non-cooperative shutdown behavior/diagnostics, invalid framebuffer metadata handling, and resource-creation failure paths.

### P1 — Archive/install hardening

Limit expanded ZIP resources, enforce expected download sizes, validate IDs, reject output collisions, and preferably stage installations transactionally.

### P1 — Trust documentation

Clearly document that official catalogue entries are maintainer-reviewed, SHA-256 pins the approved artifact, and `.dxm` files are native executable modules rather than sandboxed content.

### P2 — Catalogue signing

Add an independent catalogue signature if stronger protection against repository/publishing-account compromise is desired.

### P2 — Fuzzing

Add continuous fuzz coverage for ZIP, PNG, and catalogue parsing.

### P2 — CI supply-chain hardening

Pin actions/dependencies to immutable revisions and reduce token permissions.

### P3 — Future ecosystem work

Only investigate process isolation if DOS ex Machina eventually accepts independently published/untrusted third-party cores.


# Final assessment

## The good

The architecture, ABI design, code organization, build engineering, portability strategy, testing discipline, curated catalogue process, and artifact-integrity checks are all strong. The project reads like software being deliberately engineered rather than a prototype that happened to grow.

## The bad

There are real native-C correctness and robustness issues, particularly around thread synchronization, framebuffer ownership, parser strictness, and some resource/failure boundaries. These are fixable without redesigning the application.

## The ugly

Under the clarified trust model, there is **no major architectural security flaw that requires redesigning DXM before 1.0**.

The most important caveat is simply that native cores are trusted code. The security of the official ecosystem therefore depends on maintaining the catalogue admission process: audit the release, record the SHA-256 of the exact approved artifact, and keep catalogue publication tightly controlled.

A compromise of the catalogue publishing authority remains capable of defeating the current hash trust chain because an attacker could replace both an artifact and its expected hash. Catalogue signing can reduce that risk, but it is defense in depth rather than evidence that the current model is unsound.

## Bottom line

**DOS ex Machina is production-capable under its intended curated-catalogue model and is close to a credible 1.0 release.**

SHA-256 is doing useful security work: once you approve a specific release and put its hash in the trusted catalogue, DXM can verify that users receive those exact approved bytes. The human/maintainer audit establishes trust; SHA-256 preserves the integrity of that decision through distribution.

The highest-value remaining work is therefore:

```text
correct thread synchronization
+ safe framebuffer ownership
+ robust core lifecycle handling
+ bounded archive/install behavior
+ explicit documentation of the curated trust model
```

Catalogue signing is a worthwhile future strengthening measure, especially as the project or publishing workflow grows, but I would **not block 1.0 solely on its absence** given the current single-maintainer, pre-audited catalogue model.

---

**Audit revision:** 2.0  
**Prepared against repository revision:** `2447e9a88cb1fcaac7edf685fba9f114966bb526`  
**Threat-model clarification:** Official catalogue entries are owner-controlled and audited before publication.
