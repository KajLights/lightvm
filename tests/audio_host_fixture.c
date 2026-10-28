/* Native reference for the browser's committed PCM corpus. Read s16le hops on
 * stdin and write both profiles' per-hop results (73 floats + 17 edge floats).
 * The conformance harness runs on little-endian development/CI hosts. */
#include "host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    KfAnalysisConfig c = kf_analysis_defaults();
    KfHost *profiles[2];
    for (unsigned i = 0; i < 2; i++) {
        profiles[i] = kf_host_new(i + 1, c.gain_db, c.floor_db, c.attack_ms,
            c.release_ms, c.band_count, c.onset_threshold, c.onset_refractory_ms);
        if (!profiles[i] || c.band_count != 16) return 2;
    }
    int16_t pcm[KF_HOP];
    unsigned hop = 0;
    while (fread(pcm, sizeof(*pcm), KF_HOP, stdin) == KF_HOP) {
        const double end = 1000000 + round(++hop * KF_HOP * 1e6 / KF_RATE);
        for (unsigned i = 0; i < 2; i++) {
            memcpy(kf_host_pcm(profiles[i]), pcm, sizeof(pcm));
            const int result = kf_host_push(profiles[i], end);
            if (result < 0) return 3;
            if (!result) continue;
            float row[90];
            memcpy(row, kf_host_output(profiles[i]), 73 * sizeof(float));
            for (unsigned edge = 0; edge <= c.band_count; edge++) row[73 + edge] = kf_host_edges(profiles[i])[edge];
            if (fwrite(row, sizeof(row), 1, stdout) != 1) return 4;
        }
    }
    for (unsigned i = 0; i < 2; i++) kf_host_destroy(profiles[i]);
    return ferror(stdin) ? 5 : 0;
}
