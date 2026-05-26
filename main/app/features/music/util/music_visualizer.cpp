#include "music_visualizer.h"

namespace {

constexpr uint8_t  kEnvelopeRange = 18u;
constexpr uint32_t kAttackMs      = 60u;
constexpr uint32_t kBasePeriodMs  = 350u;
constexpr uint32_t kPeriodPerLeftSlot = 8u;
constexpr uint32_t kPerBarPhaseStep   = 173u;
// Pseudo-random hashing constants (Knuth multiplicative + a coprime).
constexpr uint32_t kHashIndexMul = 2654435761u;
constexpr uint32_t kHashCycleMul = 1597463007u;
constexpr uint8_t  kMaxAmpModulo = 13u;
constexpr uint8_t  kMinHeight = 4u;
constexpr uint8_t  kMaxHeight = 30u;

} // namespace

uint8_t musicVisualizerBarHeight(uint8_t index, uint8_t count, uint32_t progress_permille,
                                  bool playing, uint32_t phase_ms)
{
    if (count == 0u) {
        return 0u;
    }

    if (progress_permille > 1000u) {
        progress_permille = 1000u;
    }

    // Static envelope: bars near the centre are taller, bars at the ends shorter.
    // Independent of progress_permille and phase_ms — that's intentional, the
    // colour-opacity logic in VisualizerWidget conveys progress, not height.
    const uint8_t center   = static_cast<uint8_t>(count / 2u);
    const uint8_t distance = (index > center) ? static_cast<uint8_t>(index - center)
                                              : static_cast<uint8_t>(center - index);
    const uint8_t envelope = (distance > kEnvelopeRange) ? 0u
                                                          : static_cast<uint8_t>(kEnvelopeRange - distance);
    const uint8_t base     = static_cast<uint8_t>(kMinHeight + (envelope * 3u) / 4u);

    if (!playing) {
        return base;
    }

    // Per-bar period: shorter (faster) on the right side of the array so
    // treble bars feel more energetic than bass.
    const uint8_t  left_slot = (index < count) ? static_cast<uint8_t>(count - index) : 1u;
    const uint32_t period_ms = kBasePeriodMs + static_cast<uint32_t>(left_slot) * kPeriodPerLeftSlot;

    // Per-bar phase offset spreads the array across the cycle so bars don't
    // all peak at the same moment, even at phase_ms = 0.
    const uint32_t phase_with_offset = phase_ms + static_cast<uint32_t>(index) * kPerBarPhaseStep;
    const uint32_t cycle    = phase_with_offset / period_ms;
    const uint32_t in_cycle = phase_with_offset % period_ms;

    // Pseudo-random target amplitude for this (bar, cycle).
    const uint32_t hash = (static_cast<uint32_t>(index) * kHashIndexMul) ^ (cycle * kHashCycleMul);
    const uint8_t target_amp = static_cast<uint8_t>((hash >> 24) % kMaxAmpModulo);

    // Bouncy-peak envelope: linear attack to target, linear decay back to 0.
    uint32_t bouncy_amp = 0u;
    if (in_cycle < kAttackMs) {
        bouncy_amp = (static_cast<uint32_t>(target_amp) * in_cycle) / kAttackMs;
    } else {
        const uint32_t decay_ms       = period_ms - kAttackMs;
        const uint32_t decay_progress = in_cycle - kAttackMs;
        bouncy_amp = (static_cast<uint32_t>(target_amp) * (decay_ms - decay_progress)) / decay_ms;
    }

    uint32_t height = static_cast<uint32_t>(base) + bouncy_amp;
    if (height > kMaxHeight) {
        height = kMaxHeight;
    }
    return static_cast<uint8_t>(height);
}
