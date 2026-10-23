# How we built this port: harness, methodology and working with AI

This document summarizes **how** the *Hot Wheels Micro Racers* port (PC, Win9x, Direct3D 6) to PS Vita was made, not **what** the code does (see the [README](../README.md) and [PORTING.md](PORTING.md) for that).

In short: one person directing a team of AI agents, under a strict engineering methodology, verified against real hardware, with a harness that spreads work across several machines. No line got in "because the AI said it worked".

## Project numbers (development monorepo, ~3 weeks)

| | |
|---|---|
| Commits | ~1,500 |
| Specs (SDD) | 80 |
| Bugs documented with root cause | 55 |
| Tests / verification scripts | ~170 |
| Written code reviews | ~490 |

## 1. Methodology: SDD + TDD + milestones

- **Spec-Driven Development.** Every non-trivial change starts with a spec (`specs/NNN-*.md`): problem, evidence, contract, acceptance criteria and how it is verified. Code is written *against* the spec, not the other way around.
- **TDD.** Acceptance criteria become tests before implementation: host tests on x86-64, smoke tests under QEMU ARM, a harness on the Vita3K emulator and, finally, the real console.
- **Milestones.** Work is organized into milestones (boot, menus, race, performance, quality). A `feature_list` item only moves to *done* with evidence and an approved review, and only the orchestrator can mark it.
- **Bug post-mortems.** Every bug has its own file with symptoms, discarded hypotheses, root cause and verified fix. Example: the graphical glitches on the tracks (polygons "jumping") were isolated with A/B videos until it was proven that the cause was 60 Hz frame interpolation; the decision was to return to the original 40 Hz mode, with no interpolation.

## 2. The multi-agent harness

An **orchestrator** (a high-capability AI agent) plans, assigns and decides; several **worker agents** implement, research, measure and review in parallel. Everything runs in visible, controllable terminals (a terminal multiplexer for agents, *herdr*), so you can watch each agent live, interrupt it or redirect it.

- **Model per task.** Large models (Claude Opus, high-capability GPT) for the hard parts: dynarec, concurrency, rendering. Cheaper models for research, measurements and scoped implementations. A different model from the author does the review.
- **Coordination through files, not memory.** Each agent gets a written *brief* (goal, scope, what not to touch, how to report). There are review and work queues, a decision log and a *handoff* document so the orchestrator role can move from one model to another without losing context.
- **Cost control.** The token budget is managed like any other resource: small teams, scoped reads, expensive models only where they pay off.

## 3. Distributed compute over SSH

The development machine (Linux) could not do everything at once: emulation, builds and analysis in parallel.

- A **Windows PC** on the local network joined as a *worker* over SSH: agents send it jobs (builds, Vita3K emulator runs, video recording, running the original game), with locks to avoid collisions and disk guards.
- **The real PS Vita** is part of the harness: builds are uploaded over FTP with SHA-256 verification and runtime logs are pulled back for analysis. The console is the final source of truth; the emulator is not enough (it showed neither the noisy audio nor the software rendering caused by an old save file).

## 4. Reverse engineering

- **Ghidra** and **REA** (an agent-operable reverse-engineering framework) to understand the original executable: hot functions, track format, audio.
- The **original game running on Windows**, instrumented with Frida, as a behavioral reference (what it calls, when, at what rate) to compare against our runtime.

## 5. Why this is not *vibecoding*

*Vibecoding* is asking an AI for code, seeing that it "seems to work" and moving on. Here it was the opposite:

1. **Evidence before opinion.** No fix is accepted without measurement: console logs, frame-time telemetry, A/B videos, tests. Reviews reject causal conclusions the data does not support.
2. **Mandatory adversarial review**, with a checklist focused on what AI tends to get wrong: concurrency, memory ownership between CPU and GPU, memory ordering (x86 TSO vs weak ARMv7), real-time constraints, use-after-free. About 490 written reviews.
3. **Thinking at the operating-system level.** Threads and per-core affinity (game, rendering and audio on separate cores), bounded queues, per-frame time budgets (25 ms for 40 Hz) and measuring where every millisecond goes.
4. **Human in the loop for decisions.** The human sets goals, priorities and constraints ("make it work like the original", "zero interpolation"), plays on the real console and reports back. The AI executes, measures and proposes; it does not decide on its own what is acceptable.
5. **Legal and publication hygiene.** No original game data is versioned; the public repository went through a license, attribution and private-data review before release.

## 6. What we learned

- Agents perform far better with **written contracts** (specs, briefs, checklists) than with conversation.
- **Model diversity** in review catches errors the author does not see.
- **Real hardware** contradicts the emulator more often than you would expect.
- **Cost** (tokens, disk, the human's time) is one more design requirement, not a detail.

## Status

Experimental: the game boots and full races can be played on the console without graphical glitches, at a cadence still below the 40 Hz target. The measured bottleneck is the CPU executing the translated x86 code; current work focuses on profiling it and distributing more work across the Vita's three cores.
