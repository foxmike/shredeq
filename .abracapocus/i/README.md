# i

## Goal

Analyzer data pipeline: percentile cloud, headroom histogram, resonance persistence map, stereo coherence. Reuses the shared SpectralAnalysisEngine from phase G.

Percentile Cloud (Source/UI/PercentileCloud.h/.cpp): ring buffer percentile computation from SpectralAnalysisEngine frames. 10th/50th/90th percentiles. Dynamic Range Map D(f) = Q_90 - Q_10. History: 2s-30s, frozen, scan.

Headroom Histogram (Source/UI/HeadroomHistogram.h/.cpp): output amplitude distribution, L/R/mono, peak/RMS/crest.

Resonance Persistence Map (Source/UI/ResonancePersistenceMap.h/.cpp): scrolling freq×time, resonance excess intensity.

Stereo coherence + note ruler data. Analyzer quality modes (Off/Live/Studio/Analysis). All computation on background/message thread.

## Notes

- Async: Wave 1: i.1 (PercentileCloud) and i.2 (HeadroomHistogram) are independent — both consume from SpectralAnalysisEngine. Wave 2: i.3 (ResonancePersistenceMap + stereo coherence + note ruler + CMakeLists) depends on SpectralAnalysisEngine existing. Wave 3: i.4 (tests) depends on all.
- Risk: Reuses SpectralAnalysisEngine from phase G — no new FFT engine. Processor modified only for analyzer tap data push.
