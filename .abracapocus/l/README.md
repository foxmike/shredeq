# l

## Goal

Validation scripts, release documentation, and verification.

Validation script: build Release, ctest, RT-safety checks, zero-latency verification, bypass click-free, state round-trip.

Release docs: verification checklist per spec section 22. Honest documentation of what is verified vs unverified (host testing that cannot run in CI is documented as requiring manual validation).

Preset content refinement after build testing.

## Notes

- Async: Wave 1: l.1 (validation script) and l.2 (preset content) independent. Wave 2: l.3 (release docs) depends on l.1.
- Risk: Low risk — no DSP changes.
