# Phases 13-15 shipping runtime

`Kairo.EngineCore.ShippingRuntime` contains three renderer, device, and transport
neutral contracts used by both authoring tools and shipped runtime hosts.

Phase 13 owns audio voices, named buses, looping, completion, listener state, and
distance attenuation. `RuntimeAudioMixer::Step` produces deterministic bus levels;
a platform audio backend turns those levels and decoded clip samples into device
buffers without taking ownership of voice lifecycle state.

Phase 14 owns normalized widget hierarchy/layout, locale fallback, focus order,
accessible labels, and semantic actions. Renderers consume pixel rectangles and
resolved text, while assistive-technology adapters consume the same semantic tree.

Phase 15 owns typed save snapshots, canonical ordering, atomic persistence,
baseline-checked deltas, bounded replay frames, and divergence hashes. A network
adapter may transport deltas, but it cannot bypass baseline verification. Save
paths in Player pass through the project-relative asset-path validator.
