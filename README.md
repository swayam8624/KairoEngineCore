# KairoEngineCore

`KairoEngineCore` is the platform-neutral scene and application layer between
Kairo's renderer, physics engine, and future editor. It intentionally owns no
GLFW/Vulkan or physics world; host applications inject those systems.

```text
KairoMath -> KairoEngineCore -> KairoRenderer / KairoPhysicsEngine adapters
```

V1 currently provides stable `Entity` IDs, a scene registry, name/transform
components, and a monotonic frame clock. Renderer and physics components land
only after their ownership/lifetime contracts are exercised in integration
examples.

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
cmake --build build
ctest --test-dir build --output-on-failure
```
