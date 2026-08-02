# Outstanding Issues

Internal engineering document. Not user-facing.

## Confirmed Issues

### Exposed Lookahead Controls Do Not Enter the Active Signal Path

- **Area:** DSP / latency
- **Current behavior:** Global and per-band lookahead choices are exposed, but the processor does not own or call the implemented `LookaheadDelay`. Per-band selections are copied only into `ParametricBand::lookaheadMs`, whose setter/getter store and return the value without using it during processing. The plugin therefore continues to report zero latency.
- **Expected behavior:** A non-Off lookahead selection should delay the appropriate audio path, align detector processing, and report the corresponding latency to the host.
- **Impact:** Selecting 0.5, 1, 2, or 5 ms of lookahead does not provide anticipatory dynamic processing or change host delay compensation.
- **Evidence:** `Source/PluginProcessor.cpp:109-110` and `Source/PluginProcessor.cpp:172` expose the choices. `Source/PluginProcessor.cpp:553-594` maps per-band choices but only calls `setLookaheadMs()`. That method only stores the value (`Source/DSP/ParametricBand.cpp:673-680`). The processor member list contains no `LookaheadDelay` (`Source/PluginProcessor.h:64-87`), and the active processing sequence runs directly through Stage A, resonance, multiband, carve, and Stage B (`Source/PluginProcessor.cpp:788-839`).
- **Next action:** Define whether lookahead is global, per-band, or both; integrate `LookaheadDelay` at the corresponding processing boundary; align dry and detector paths; and update host latency when the selected delay changes.
