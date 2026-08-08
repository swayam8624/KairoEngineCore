# Portability

`Kairo.EngineCore.Platform` is the operating-system boundary for EngineCore. Feature code should use this module instead of adding platform conditionals directly.

The current boundary covers host-family detection, native path conventions, environment access, and replace-existing file moves. Cross-platform behavior must be validated on Linux, macOS, and Windows.

C++ standard-library and module portability is treated separately: translation units include what they use, and exported value types use explicit semantic comparison when defaulted cross-module comparisons are not portable.
