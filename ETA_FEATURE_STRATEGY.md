# ETA Feature — Maintenance & Merge Strategy (not an implementation plan)

> Companion to `plan.md`. The other file describes *what* to build. This file
> describes *how to build it so upstream INX updates keep applying cleanly*.
> Nothing here is implemented — it is a strategy + risk analysis to agree on
> before writing code.

---

## 0. TL;DR

1. **Blocker:** `INX/` is **not a git repository** (and `CrossInk-main/` isn't either).
   You cannot "move upstream changes in" without version control. Step 0 is to
   `git init`, pin the INX upstream commit you built from, and keep your feature
   on a topic branch that you **rebase** (not merge) onto upstream.
2. **CrossInk is a reference, not your upstream.** `README.md` says INX "is a fork
   of CrossPoint." CrossInk is a *sibling* project you are borrowing the algorithm
   from. Your merge target is the **INX upstream repo**, not CrossInk. Treat
   CrossInk only as a code example.
3. **The plan has one factual error and several under-specified merge hazards**
   (stats versioning, `StatusBarItem` integer serialization, `endPageTimer`
   direction). Details in §3.
4. **Recommended shape:** keep all ETA logic inside `EpubReadingStats` (compute
   there, render in `StatusBar`), add a *separate* `recordForwardPageRead()`
   instead of overloading `endPageTimer()`, bump the stats file to **v3** with a
   version guard, and append the new `StatusBarItem` at the **end** of the enum.

---

## 1. The real upstream-tracking problem

You said: *"I just want to add this feature and then still use the updates of the
main repo."* That is only possible if the project is a git checkout with the
upstream configured as a remote. Today it is a plain folder.

### What to do (Step 0, before any feature code)

```sh
cd INX
git init
git add -A
git commit -m "vendor: INX baseline as of <date> / <upstream commit>"

# Add the real INX upstream (replace URL with the actual repo you cloned from)
git remote add upstream <INX_UPSTREAM_URL>
git fetch upstream
# Find the exact upstream commit your folder corresponds to and tag it
git tag upstream-baseline <commit>
# Put your work on a branch
git checkout -b feature/chapter-eta
```

### Branching model (recommended)

- `main` / `master` = a clean, unmodified mirror of `upstream/<branch>` (only
  `git fetch` + `git merge --ff-only` here). Never edit directly.
- `feature/chapter-eta` = your branch, **rebased** onto `upstream/<branch>` whenever
  you want updates:
  ```sh
  git fetch upstream
  git rebase upstream/<branch>      # resolves conflicts in your feature only
  ```
- **Rebase, not merge.** Merge commits make every future rebase harder and create
  duplicate "fix conflict" commits. A linear topic branch is the lowest-friction
  way to keep a single downstream feature on top of a moving upstream.

### Why a topic branch + rebase beats the alternatives here

| Approach | Verdict | Why |
|---|---|---|
| Topic branch + `rebase` | **Recommended** | One feature, isolated commits, clean history, easy to drop/reapply if upstream adopts ETA natively. |
| `git merge` of upstream | Avoid | Merge commits pile up; conflicts re-resolved repeatedly. |
| Quilt / patch queue | Overkill | Good for many out-of-tree patches; you have one feature. |
| `git subtree` / `submodule` | Not applicable | INX is the whole firmware, not a library you vendor. |

---

## 2. Where the feature should live (modularity)

Keep the **computation** and the **rendering** in separate layers so the smallest
possible surface touches upstream-hot files:

- **Compute** in `EpubReadingStats` (already owns the reading session, the timer,
  and `save()`). Add:
  - `recordForwardPageRead(uint32_t seconds)` — updates pace.
  - `bool estimateChapterTimeLeftSeconds(uint32_t& out) const`.
  - `static std::string formatCompactTimeLeft(uint32_t seconds)`.
- **Render** in `StatusBar` — only a new `case StatusBarItem::TIME_LEFT_CHAPTER`
  that *calls* the method above. `StatusBar` should not contain ETA math.
- **Persist** via the existing `saveBookStats`/`loadBookStats` (see §3.3).
- **Expose** via one new `StatusBarItem` enum value + one new name in
  `getStatusBarItemName()`.

This keeps edits to: `Statistics.h/.cpp`, `EpubReadingStats.h/.cpp`,
`BookSetting.h`, `StatusBar.h/.cpp`, `SettingsDrawer.cpp`, and one wiring line in
`EpubActivity.cpp`. That is the minimal, reviewable surface.

---

## 3. Concrete risks found by reading the actual code

### 3.1 — No git repo (BLOCKER)
See §1. Without this, none of the rest is maintainable.

### 3.2 — `StatusBarItem` is serialized as a raw `uint8_t`
`BookSetting.h` serializes the item as a single byte
(`StatusBarSectionConfig::toBytes`/`fromBytes`), and `BookSettings::kSerializedSize`
is a hardcoded `20`. `deserialize()` does **no range check** — it just
`static_cast`s the byte back to the enum.

**Risk:** If upstream inserts a new `StatusBarItem` *before* your value (or you
rebase and the enum shifts), a `settings.bin` written by an older INX build will
map your `TIME_LEFT_CHAPTER` integer to whatever upstream put at that slot. The
menu's `change` lambda clamps to `STATUS_BAR_ITEM_COUNT`, but stored files are not
re-validated on load.

**Mitigations (pick at least the first two):**
- Append your enum value at the **very end**, after `PAGE_NUMBERS_WITH_PERCENT`,
  right before `STATUS_BAR_ITEM_COUNT`. Keep it last through every rebase.
- In `deserialize()`, clamp: `if ((uint8_t)item >= (uint8_t)STATUS_BAR_ITEM_COUNT) item = NONE;`
  (this also hardens against any corrupt/old file).
- Document the integer value in a comment so a rebase conflict is obvious.
- (Bigger change, optional) Persist the item by **name** instead of integer. Not
  recommended now — it changes the file format and touches more upstream code.

The plan's claim *"the menu entries already cycle through STATUS_BAR_ITEM_COUNT, so
they pick up the new option automatically"* is true for the **UI**, but it ignores
the **serialization-by-integer** fragility. That is the part that bites on rebase.

### 3.3 — `statistics.bin` **already has** a version field (plan was wrong)
`Statistics.cpp` defines `STATS_FILE_VERSION = 2` and `STATS_MAGIC_NUMBER`.
`loadBookStats()` already does a version-guarded read:

```cpp
if (version >= 2) {
  file.read(&stats.sessionCount, sizeof(uint32_t));
} else {
  stats.sessionCount = 0;
}
```

So the plan's Step 1 worry (*"INX's statistics.bin has no visible version field"*)
is **inaccurate** — the mechanism exists. **Do exactly what `sessionCount` did:**
- Bump `STATS_FILE_VERSION` to `3`.
- Insert new binary fields **immediately after `sessionCount` and before the
  `path` string** (the strings are length-prefixed, so order matters; do not append
  after the strings).
- In `loadBookStats`, guard the new reads with `if (version >= 3) { ... } else {
  zero }`.
- Old v2 files load fine (new fields default to 0 → ETA shows `"-"` until enough
  samples accrue). No migration tool needed.

This is the clean, already-established pattern. Use it.

### 3.4 — `endPageTimer()` has no forward/back direction
CrossInk records pace **only on forward page turns** (with a ≥2 s dwell threshold).
INX's `EpubReadingStats::endPageTimer()` currently:
- is called from `EpubActivity::endPageTimer()` which takes **no `forward`
  argument**, and
- unconditionally increments `totalPagesRead` and recomputes `avgPageTimeMs`
  regardless of direction.

So you **cannot** tell forward from back inside `endPageTimer()` today. The plan's
Step 2 ("detect forward page turns") is under-specified.

**Recommended fix (low merge surface):** do **not** overload `endPageTimer()`.
Instead add a dedicated path:
- In `EpubActivity::pageTurn(bool forward)`, call a new
  `readingStats_.recordForwardPageRead(...)` **only in the `if (forward)` branch**
  (after the page actually advanced). This is a single, well-contained call site and
  mirrors CrossInk's semantics exactly.
- Keep `endPageTimer()` untouched so upstream changes to it don't conflict with
  your pace logic.

This also avoids the next problem:

### 3.5 — `endPageTimer()` fires on menu/drawer opens, not just page turns
Grepping `EpubActivity.cpp`, `endPageTimer()` is called when opening the settings
drawer (L772), the menu drawer (L922), entering annotations (L877), TOC jumps,
gestures, auto-page-turn, etc. Many of these are **not** "I read a page" events. If
you sample pace inside `endPageTimer()`, you will feed menu-dwell and
chapter-skip dwell into the average and skew the ETA.

**Fix:** sample pace **only from the forward branch of `pageTurn()`** (per 3.4),
never from `endPageTimer()`. This is both more correct and more merge-friendly.

### 3.6 — `StatusBar` needs a handle to the live `EpubReadingStats`
Today `StatusBar` is constructed in `fastPath()`/`slowPath()` as
`StatusBar(renderer, *epub, bookSettings)` and only holds `m_renderer`,
`m_epub`, `m_settings`. It has no access to `readingStats_`.

**Fix:** add a `const EpubReadingStats*` member (set in the constructor or via a
setter) and pass `readingStats_` when constructing the status bar in
`fastPath`/`slowPath`. Two call sites. Confirm `readingStats_` is initialized
**before** the status bar is built (it is a member of `EpubActivity`, and
`initStats()` runs in `slowPath` before `statusBar` is created — verify ordering
during implementation). This is a small, localized change and the cleanest option
the plan suggested.

### 3.7 — Pace source: persisted forward pace + corrected `endPageTimer` (DECIDED)
**Decision: do the CrossInk-faithful approach, AND correct INX's pre-existing
`endPageTimer` inflation (see §3.10).** Do not reuse `avgPageTimeMs` as-is — it
inherits the pages-read inflation bug.

Two separable layers, kept as separate commits (see §7):

**Layer A — correct `endPageTimer()` (fixes the bug, benefits all stats):**
`EpubReadingStats::endPageTimer()` currently does `totalPagesRead++` and recomputes
`avgPageTimeMs` *unconditionally*, and is called from non-page-turn handlers
(menu/drawer opens, annotation entry). Fix: pass a `bool pageTurned` (or `forward`)
flag into `endPageTimer()` and guard the `totalPagesRead++` / `avgPageTimeMs`
recompute behind it. Update the ~10 call sites in `EpubActivity.cpp` to pass the
flag (most already know whether a real turn happened). ~15–25 lines. This makes
INX's existing "Pages" and "Average / Page" stats correct for everyone, not just
for the ETA.

**Layer B — CrossInk-faithful forward pace (the ETA algorithm):**
Add `recordForwardPageRead(uint32_t seconds)` that updates persisted
`avgSecondsPerForwardPage` + `paceSampleCount` (running average, minimum-dwell
threshold 10 s to ignore quick flips — see §3.11), gate the ETA behind `paceSampleCount >= 3`
samples, and persist the fields via the §3.3 stats v3 bump. Call
`recordForwardPageRead()` only from the `if (forward)` branch of `pageTurn()` —
never from `endPageTimer()` — so menu/drawer dwell never pollutes the pace.

Why this is the right call (not disproportionately costly):
- Layer A is ~20 lines and fixes a real bug for all of INX's stats.
- Layer B touches the same `EpubReadingStats` / `Statistics` surface you would
touch anyway; the extra cost over "reuse avgPageTimeMs" is just Layer A's
parameter + guard.
- Both are upstream-hot, but kept as **small, separate commits** so each rebases
  independently and can be dropped if upstream fixes it first.

**Upstream-bug interaction (important):** the user filed the `totalPagesRead`
inflation as an upstream bug. If upstream merges that fix, **drop the Layer A
commit** and rebase Layer B on top of their change. If upstream rejects/ignores
it, keep Layer A. Either way the ETA (Layer B) is unaffected because it samples
pace only from the forward branch of `pageTurn()`.

Trade-off vs. the minimal alternative:
- **Minimal (reuse `avgPageTimeMs`):** smallest surface, but ETA inherits the
  inflation bias and shows a guess after 1 page.
- **This approach (forward pace + Layer A):** medium, well-contained surface;
  accurate, robust, and fixes a real bug. **Chosen.**

### 3.10 — Pre-existing INX quirk: `endPageTimer()` inflates `totalPagesRead` (out of scope)
`EpubReadingStats::endPageTimer()` unconditionally runs
`stats_.totalPagesRead++` and recomputes `avgPageTimeMs`, and it is invoked from
many input handlers that are **not** page turns — opening the settings drawer,
opening the menu drawer, and entering annotations (verified in
`EpubActivity.cpp`). Consequences:

- `totalPagesRead` is **inflated** by every menu/drawer open and annotation entry,
  even though no page was turned. This makes the end-of-book "Pages" stat too
  high.
- Reading *time* is mostly unaffected: the page timer is ended at menu-open and
  restarted at menu-close (via `startPageTimer()` in drawer-close handlers), so
  menu dwell generally does not leak into `totalReadingTimeMs`.
- Net effect: `avgPageTimeMs` (= time ÷ pages) is biased **smaller than reality**.

This is a pre-existing INX behavior, **not** something the ETA feature should fix.
The ETA work avoids it automatically by sampling pace only from the forward
branch of `pageTurn()` (§3.4–3.5), never from `endPageTimer()`. Flagged here so
it is not mistaken for an ETA bug during testing, and so the user knows why
reusing `avgPageTimeMs` is the weaker option (§3.7).

### 3.11 — How the forward-pace dwell is actually obtained (implementation gap)
§3.4–3.7 say "call `recordForwardPageRead()` from the forward branch of
`pageTurn()`", but never say *where the seconds come from*. This is the one
concrete gap in the plan, because of a timing detail in `EpubReadingStats`:

- `endPageTimer()` (`EpubReadingStats.cpp:58`) computes
  `timeSpent = millis() - pageStartTime_`, then at the **end** sets
  `pageStartTime_ = 0` (line 94). So once `endPageTimer()` has run, the dwell of
the page you just read is gone — `pageTurn()` cannot recompute it.
- `pageTurn(bool forward)` (`EpubActivity.cpp:1451`) currently does **not** call
  `endPageTimer()` itself; the callers do `endPageTimer(); pageTurn(X);` (e.g.
  lines 893/900/907). So the dwell is consumed *before* `pageTurn()` runs.

**Required wiring (small, contained):**
1. Change `EpubReadingStats::endPageTimer(...)` to **return `uint32_t`** =
   elapsed ms if a valid sample was recorded (dwell ≥ 1 s and `section`
   non-null), else `0`. Do **not** change its existing side effects (it still
   updates `totalReadingTimeMs`/`totalPagesRead`/`avgPageTimeMs` per Layer A).
2. Change `EpubActivity::endPageTimer()` (line 2110) to return that `uint32_t`
   (and return `0` if `!epub`).
3. **Fold** the `endPageTimer()` call *into* `pageTurn()` at the very top
   (before the `!epub` / `!section` early returns, so stats behavior is
   unchanged), capturing `const uint32_t dwellMs = endPageTimer();`. Remove the
   now-redundant `endPageTimer();` from the 6 real page-turn call sites
   (`prev`/`next`/gesture/short-pwr/auto-turn/long-press). The non-page-turn call
   sites (menu `:772`, annotate `:877`, `onExit` `:2110`) keep their explicit
   `endPageTimer()` calls.
4. In the `if (forward)` branch of `pageTurn()`, **after** the page has advanced
   (currentPage incremented or spine advanced), gate on dwell:
   ```cpp
   if (dwellMs >= kMinForwardDwellMs) {
     readingStats_.recordForwardPageRead(dwellMs / 1000);
   }
   ```
   where `kMinForwardDwellMs = 10000` (see threshold note below).

This keeps pace sampling strictly on forward turns and never feeds menu/annotate
dwell into the average — exactly the §3.5 intent, now actually implementable.

**Threshold note (deviation from CrossInk):** CrossInk uses a ~2 s minimum dwell
to ignore quick flips. For INX we require **every page turn under 10 s to be
ignored** — i.e. `kMinForwardDwellMs = 10000`. Rationale: <10 s/page is almost
always a skim/scroll, not a real read, and a 10 s floor keeps the pace estimate
from being polluted by fast navigation. Apply this 10 s floor in **both** layers:
- Layer B pace sampling (above) — only forward turns with dwell ≥ 10 s update
  `avgSecondsPerForwardPage`.
- Layer A is unaffected by the threshold (it counts a page whenever a real turn
  occurred, regardless of dwell) — but per §3.10, Layer A should still only
  increment `totalPagesRead` when an actual turn happened, not on menu/drawer
  opens.

**`getStatusBarItemName()` array lockstep (separate hazard):**
`SettingsDrawer.cpp:631` indexes a **static `names[]`** array by
`static_cast<int>(item)`. Today it has exactly 13 entries (indices 0–12)
matching the 13 enum values. If you add `TIME_LEFT_CHAPTER` at index 13 but
forget to append the 14th string, `names[13]` reads **out of bounds → garbage
`const char*` → crash on render**. So the enum addition and the `names[]` append
are a single, indivisible change: add `TIME_LEFT_CHAPTER` to the enum **and**
`"Time Left (Chapter)"` as the 14th element, in the same commit, keeping array
order == enum order. (This is the concrete version of the §4 "add one string at
the new index" row.)

### 3.8 — Chapter-end edge cases
`remainingPages = section->pageCount - section->currentPage - 1`.
- On the **last page of a chapter**, `remainingPages == 0` → return `false` →
  render `"-"`. Correct.
- `StatusBar::render()` already guards `section->pageCount == 0`, so the ETA case
  is safe there.
- CrossInk's pace is per *page*; INX chapter `pageCount` can be large, so the
  estimate scales linearly. Fine.

### 3.9 — Testing without hardware
`StatusBar.cpp` already has `#ifdef SIMULATOR` (see `getBatteryPercentString()`),
and the project builds via `platformio.ini`. You can validate the ETA in the
**simulator**: read a few pages forward, assign "Time Left (Chapter)" to a status-bar
slot, and watch it count down. Use the simulator for the smoke test in `plan.md`
Step 7 — don't flash the device just to verify the string.

---

## 4. Merge-conflict hotspots to watch when rebasing

When you `git rebase upstream/<branch>`, expect conflicts in exactly these files
(your edits vs upstream edits):

| File | Why it conflicts | How to keep it small |
|---|---|---|
| `src/state/BookSetting.h` | Enum + `kSerializedSize` + serialize/deserialize | Append enum at end; clamp in deserialize; bump size last. |
| `src/state/Statistics.h/.cpp` | Struct fields + version bump | Mirror the `sessionCount` v2 pattern exactly. |
| `src/activity/reader/Epub/SettingsDrawer.cpp` | `getStatusBarItemName()` name array | Append one string at the new index **in the same commit as the enum add** (array/enum lockstep, §3.11); keep array order = enum order. |
| `src/activity/reader/Epub/StatusBar.cpp` | New `case` in `renderSection` switch | One `case`, one method call. |
| `src/activity/reader/Epub/EpubActivity.cpp` | StatusBar ctor + `pageTurn()` forward branch | One ctor arg + one call in the forward branch. |

If upstream ever adds its **own** ETA/time-left feature, drop your branch and adopt
theirs — that's the payoff of keeping this isolated on a topic branch.

---

## 5. Validation plan (pre-merge checklist)

1. `platformio` build for the **simulator** target compiles with no new warnings.
2. Simulator: open an EPUB, turn a few pages forward, open status-bar settings,
   assign "Time Left (Chapter)" to a slot → sensible estimate appears and counts
   down toward chapter end.
3. Simulator: on the last page of a chapter, ETA shows `"-"` (not a crash/garbage).
4. Backward-compat: load an old `statistics.bin` (v2, no pace fields) → no crash,
   ETA shows `"-"` until samples accrue.
5. Forward-only pace: opening menus / jumping via TOC does **not** change the
   estimate (confirms §3.5).
5b. Pace floor: a forward page turn with dwell **< 10 s** is ignored (no pace
   update, no ETA change) — confirms the §3.11 threshold and that skims/scrolls
   don't pollute the estimate.
6. `git rebase upstream/<branch>` applies cleanly (or with only the expected,
   small conflicts listed in §4).

---

## 6. Decisions (status)

- [x] **Upstream URL:** the INX repo is the upstream (user-confirmed). Set
      `git remote add upstream <INX_REPO_URL>` when the project is forked.
- [x] **Clamp `StatusBarItem` on deserialize?** **YES.** Add the out-of-range →
      `NONE` guard in `BookSettings::deserialize()` (§3.2).
- [x] **Topic branch + rebase workflow?** **YES.** Clean mirror on `main`;
      ETA on `feature/chapter-eta`; `git rebase upstream/<branch>` for updates (§1).
- [x] **Pace approach?** **YES — forward pace (CrossInk-faithful) + correct
      `endPageTimer` inflation (Layer A + Layer B, §3.7).** Submitted the
      `totalPagesRead` inflation as an upstream bug; if upstream fixes it, drop
      the Layer A commit and rebase Layer B on top.

---

## 7. Suggested commit split (keeps rebase clean)

1. `chore: git init + pin INX upstream baseline` (Step 0)
2. `fix(stats): endPageTimer only counts a page when one was actually turned`
   (Layer A — the `totalPagesRead` inflation fix; **drop this if upstream merges
   the bug fix**)
3. `feat(stats): bump statistics.bin to v3, add forward-pace fields (guarded read)`
   (Layer B persistence)
4. `feat(eta): record forward-page pace in EpubReadingStats` (Layer B sampling,
   called only from `pageTurn()` forward branch)
5. `feat(eta): compute + format chapter time-left`
6. `feat(ui): add TIME_LEFT_CHAPTER status-bar item + name`
7. `feat(ui): wire StatusBar to live EpubReadingStats`

Small, single-purpose commits rebase and review far better than one big diff.
Commits 2 and 3/4 are independent, so the Layer A fix can be removed without
touching the ETA logic.
