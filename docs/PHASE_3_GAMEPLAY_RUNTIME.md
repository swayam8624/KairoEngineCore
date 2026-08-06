# Phase 3 — Gameplay Runtime

Phase 3 turns the stable authored scene and Phase 1 sample-specific behaviour into reusable runtime contracts.

## Transactional runtime world

`RuntimeWorld` queues structural commands and applies them to a candidate scene. Spawn, destroy, rename, transform, enable, hierarchy and tag operations either all commit or leave the live scene and revision unchanged. Spawn tickets resolve to stable scene entities only after a successful commit.

This boundary is intentionally deterministic and backend-neutral. Physics and rendering adapters observe a committed snapshot; they never see a partially applied gameplay transaction.

## Stateful gameplay VM

`GameplayVM` is a versioned, bounded bytecode runtime with:

- persistent typed instance variables;
- begin-play, tick, input, collision and custom events;
- arithmetic and comparison operations;
- deterministic branches and instruction budgets;
- owner/entity registers;
- tag queries;
- position and enabled-state access;
- spawn, destroy and tag mutation host calls;
- deterministic little-endian serialization with explicit sequential reads.

The existing visual-logic V1 runtime remains supported. Its parser is repaired to avoid stateful reader calls inside aggregate initializers, which produced incorrect ordering under MSVC module compilation.

## Acceptance gates

- A failed structural transaction leaves the runtime scene untouched.
- Persistent variables survive event dispatch and reset to declared defaults on request.
- Variable assignments cannot change declared runtime type.
- Infinite loops fail through a shared instruction budget.
- Gameplay bytecode round-trips on Clang and MSVC.
- Existing EngineCore and logic-artifact tests remain green.
