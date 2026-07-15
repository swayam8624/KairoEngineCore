# KairoEngineCore

`KairoEngineCore` is the platform-neutral scene and application layer between
Kairo's renderer, physics engine, and future editor. It intentionally owns no
GLFW/Vulkan or physics world; host applications inject those systems.

```text
KairoMath -> KairoEngineCore -> KairoRenderer / KairoPhysicsEngine adapters
```

V1 provides stable `Entity` IDs, a scene registry, name/transform/runtime
components, a monotonic frame clock, typed events/layers, a bounded thread-safe
logger, a fixed worker job system, and platform-neutral input state. Renderer and physics bindings remain
outside the core so each backend can preserve its own lifetime rules.

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
cmake --build build
ctest --test-dir build --output-on-failure
```
