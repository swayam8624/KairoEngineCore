# Phase 7 and Phase 12 runtime contracts

Phase 7 uses `Kairo.EngineCore.NativeGameplay` as the compiler-independent reflected C++ gameplay boundary. Project-owned attachments persist in `Config/NativeGameplay.knative`, keyed by stable scene entity IDs and validated against a linked `NativeGameplayRegistry`. Editor and Player consume the same reflected property metadata; unlinked or mistyped native behaviour configuration fails before play instead of silently degrading.

Phase 12 keeps animation, terrain/foliage, particles, cloth, fluids, and world streaming behind the versioned `Config/Production.kproduction` manifest. `ProductionPerformanceBudget` validates subsystem sizes and an estimated per-frame work ceiling before runtime construction. `ProductionRuntime` orchestrates the configured systems and publishes deterministic operation/profiling counters so Editor previews, Player execution, tests, and future external profilers share one workload contract.

The current terrain/foliage, particle, cloth, fluid, streaming and animation implementations remain modular Core kernels. More specialized GPU or large-world backends can replace individual kernels behind these persisted descriptors and budgets without changing project configuration.
