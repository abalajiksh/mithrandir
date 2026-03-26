#pragma once

#include "types.h"
#include <filesystem>
#include <functional>
#include <vector>

namespace mithrandir {

struct SplitResult {
    int track_number;
    std::filesystem::path output_path;
    bool success;
    std::string error_msg;
};

using ProgressCallback = std::function<void(int track_number, int total)>;

/// Given a parsed CUE sheet, split each source file into individual tracks.
/// The base_dir is used to resolve relative FILE paths in the CUE.
/// Returns one SplitResult per track.
std::vector<SplitResult> split_tracks(
    const CueSheet& sheet,
    const std::filesystem::path& base_dir,
    const SplitOptions& opts,
    ProgressCallback on_progress = nullptr);

/// Probe the duration (in seconds) of an audio file via ffprobe.
/// Returns 0.0 on failure.
double probe_duration(const std::filesystem::path& audio_path);

} // namespace mithrandir
