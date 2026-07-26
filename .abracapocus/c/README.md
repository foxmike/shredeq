# c

## Goal

Per-band dynamic EQ: envelope followers, soft-knee gain computation, transient/sustain separation, and dynamic processor integration.

Envelope follower (Source/DSP/EnvelopeFollower.h/.cpp): attack/release with configurable ballistics. Soft-knee gain computation. Transient/sustain separation via fast and slow envelopes.

Dynamic processor (Source/DSP/DynamicProcessor.h/.cpp): wraps envelope + gain per band. All modes: static, downward/upward dynamic, compression, expansion, transient-only, sustain-only, sidechained. All detector sources.

Integrate into ParametricBand. Dynamic visualization data struct for GUI.

See SHREDEQ_SPEC.md sections 4.1-4.5 for complete DSP math.

## Notes

- Async: Wave 1: c.1 (EnvelopeFollower + gain + transient/sustain) standalone. Wave 2: c.2 (DynamicProcessor) depends on c.1. Wave 3: c.3 (ParametricBand integration + CMakeLists) depends on c.2. Wave 4: c.4 (tests) depends on c.1, c.2.
- Risk: ParametricBand modified — c.3 serialized after phase B. c.3 is sole CMakeLists owner.
