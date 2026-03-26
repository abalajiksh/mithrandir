#pragma once

#include "types.h"
#include <filesystem>
#include <string>

namespace mithrandir {

/// Parse a .cue file from disk.
/// Throws std::runtime_error on failure.
CueSheet parse_cue_file(const std::filesystem::path& cue_path);

/// Parse CUE sheet from an in-memory string.
CueSheet parse_cue_string(const std::string& cue_text,
                           const std::filesystem::path& base_dir = ".");

/// Attempt to extract an embedded CUE sheet from a FLAC file.
/// Uses ffprobe to read the CUESHEET Vorbis comment.
/// Returns nullopt if no embedded CUE is found.
std::optional<std::string> extract_embedded_cue(const std::filesystem::path& flac_path);

} // namespace mithrandir
