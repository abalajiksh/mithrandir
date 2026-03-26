#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mithrandir {

// CD audio: 75 frames per second
inline constexpr int CD_FRAMES_PER_SECOND = 75;
// Standard 2-second lead-in (150 frames) for disc ID calculation
inline constexpr int CD_LEAD_IN_FRAMES = 150;

/// Convert MM:SS:FF timestamp string to total CD frames.
/// Returns nullopt on parse failure.
std::optional<int64_t> parse_msf(const std::string& msf);

/// Convert CD frames to fractional seconds.
inline double frames_to_seconds(int64_t frames) {
    return static_cast<double>(frames) / CD_FRAMES_PER_SECOND;
}

/// Format seconds as HH:MM:SS.mmm for ffmpeg -ss / -to.
std::string seconds_to_ffmpeg_ts(double seconds);

// ─── CUE data model ────────────────────────────────────────────────

struct CueTrack {
    int number = 0;
    std::string title;
    std::string performer;
    std::string songwriter;
    std::string isrc;

    // INDEX 00 = pregap start, INDEX 01 = track audio start.
    // Stored as CD frames (75 fps).
    std::optional<int64_t> index00_frames;
    int64_t index01_frames = 0;
};

struct CueFile {
    std::string filename;       // as written in the CUE (may be relative)
    std::string format;         // WAVE, FLAC, AIFF, MP3, BINARY, ...
    std::vector<CueTrack> tracks;
};

struct CueSheet {
    std::string title;          // TITLE (album)
    std::string performer;      // PERFORMER (album artist)
    std::string genre;          // REM GENRE
    std::string date;           // REM DATE
    std::string cddb_discid;    // REM DISCID
    std::string catalog;        // CATALOG (UPC/EAN barcode)
    int disc_number  = 0;       // REM DISC_NUMBER or DISCNUMBER
    int total_discs  = 0;       // REM TOTAL_DISCS or TOTALDISCS
    std::string comment;        // REM COMMENT
    std::vector<CueFile> files;

    /// Total number of tracks across all FILE blocks.
    int total_tracks() const;
};

// ─── Options ────────────────────────────────────────────────────────

struct SplitOptions {
    std::filesystem::path output_dir;
    std::string output_format  = "flac";     // flac | wav
    int flac_compression       = 8;
    std::string naming_pattern = "{number} - {title}";
    bool dry_run               = false;
    bool verbose               = false;

    enum class PregapMode { Discard, Prepend, AppendPrev };
    PregapMode pregap_mode = PregapMode::Discard;
};

} // namespace mithrandir
