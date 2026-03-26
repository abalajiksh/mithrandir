#include "splitter.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>

namespace mithrandir {

// ─── Helpers ────────────────────────────────────────────────────────

/// Shell-escape a path for use in system().
static std::string shell_escape(const std::filesystem::path& p) {
    std::string s = p.string();
    // Wrap in single quotes, escaping embedded single quotes.
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    out += "'";
    return out;
}

/// Sanitise a string for use as a filename component.
static std::string sanitise_filename(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '/': case '\\': case ':': case '*':
            case '?': case '"':  case '<': case '>':
            case '|':
                out += '_';
                break;
            default:
                out += c;
        }
    }
    // Trim trailing dots/spaces (Windows compat, also just good hygiene).
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    return out;
}

/// Expand a naming pattern like "{number} - {title}" with track metadata.
static std::string expand_pattern(const std::string& pattern,
                                   const CueTrack& track,
                                   const CueSheet& sheet) {
    std::string result = pattern;

    auto replace_all = [&](const std::string& token, const std::string& value) {
        std::string::size_type pos = 0;
        while ((pos = result.find(token, pos)) != std::string::npos) {
            result.replace(pos, token.size(), value);
            pos += value.size();
        }
    };

    char num_buf[8];
    std::snprintf(num_buf, sizeof(num_buf), "%02d", track.number);

    replace_all("{number}",    num_buf);
    replace_all("{title}",     track.title);
    replace_all("{artist}",    track.performer.empty() ? sheet.performer : track.performer);
    replace_all("{album}",     sheet.title);
    replace_all("{date}",      sheet.date);
    replace_all("{genre}",     sheet.genre);

    return sanitise_filename(result);
}

double probe_duration(const std::filesystem::path& audio_path) {
    std::string cmd =
        "ffprobe -v error -show_entries format=duration "
        "-of default=nokey=1:noprint_wrappers=1 " +
        shell_escape(audio_path);

    std::array<char, 256> buf;
    std::string out;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(
        popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return 0.0;
    while (fgets(buf.data(), buf.size(), pipe.get()))
        out += buf.data();
    try { return std::stod(out); }
    catch (...) { return 0.0; }
}

// ─── Core splitter ──────────────────────────────────────────────────

std::vector<SplitResult> split_tracks(
    const CueSheet& sheet,
    const std::filesystem::path& base_dir,
    const SplitOptions& opts,
    ProgressCallback on_progress) {

    namespace fs = std::filesystem;
    std::vector<SplitResult> results;

    // Ensure output directory exists.
    if (!opts.dry_run)
        fs::create_directories(opts.output_dir);

    int global_track_idx = 0;
    int total = sheet.total_tracks();

    for (size_t fi = 0; fi < sheet.files.size(); ++fi) {
        const auto& cue_file = sheet.files[fi];

        // Resolve the source audio path.
        fs::path src_path = base_dir / cue_file.filename;
        if (!fs::exists(src_path)) {
            // Try common alternate extensions.
            for (auto ext : {".flac", ".wav", ".wv", ".ape", ".mp3"}) {
                auto alt = src_path;
                alt.replace_extension(ext);
                if (fs::exists(alt)) { src_path = alt; break; }
            }
        }
        if (!fs::exists(src_path)) {
            for (auto& t : cue_file.tracks) {
                results.push_back({t.number, {}, false,
                    "Source file not found: " + src_path.string()});
                ++global_track_idx;
            }
            continue;
        }

        // Probe total duration for calculating the last track's end.
        double total_duration = probe_duration(src_path);

        for (size_t ti = 0; ti < cue_file.tracks.size(); ++ti) {
            const auto& track = cue_file.tracks[ti];
            ++global_track_idx;

            if (on_progress) on_progress(global_track_idx, total);

            // ── Compute start and end in seconds ────────────────
            double start_sec = frames_to_seconds(track.index01_frames);
            double end_sec   = 0.0;

            // Pregap handling: if Prepend mode, start from INDEX 00.
            if (opts.pregap_mode == SplitOptions::PregapMode::Prepend &&
                track.index00_frames.has_value()) {
                start_sec = frames_to_seconds(*track.index00_frames);
            }

            // End time = next track's start (or INDEX 00 if AppendPrev).
            bool is_last_in_file = (ti + 1 >= cue_file.tracks.size());
            if (is_last_in_file) {
                end_sec = total_duration;  // to EOF
            } else {
                const auto& next = cue_file.tracks[ti + 1];
                if (opts.pregap_mode == SplitOptions::PregapMode::AppendPrev &&
                    next.index00_frames.has_value()) {
                    end_sec = frames_to_seconds(*next.index00_frames);
                } else {
                    end_sec = frames_to_seconds(next.index01_frames);
                }
            }

            // ── Build output path ───────────────────────────────
            std::string stem = expand_pattern(opts.naming_pattern, track, sheet);
            std::string ext  = "." + opts.output_format;
            fs::path out_path = opts.output_dir / (stem + ext);

            if (opts.dry_run) {
                results.push_back({track.number, out_path, true, "[dry run]"});
                if (opts.verbose) {
                    std::cout << "[dry run] Track " << track.number
                              << ": " << seconds_to_ffmpeg_ts(start_sec)
                              << " → " << (is_last_in_file ? "EOF"
                                          : seconds_to_ffmpeg_ts(end_sec))
                              << "  →  " << out_path.filename().string()
                              << "\n";
                }
                continue;
            }

            // ── Build ffmpeg command ────────────────────────────
            std::ostringstream cmd;
            cmd << "ffmpeg -y -v error"
                << " -i " << shell_escape(src_path)
                << " -ss " << seconds_to_ffmpeg_ts(start_sec);

            if (!is_last_in_file)
                cmd << " -to " << seconds_to_ffmpeg_ts(end_sec);

            if (opts.output_format == "flac") {
                cmd << " -c:a flac"
                    << " -compression_level " << opts.flac_compression;
            } else if (opts.output_format == "wav") {
                cmd << " -c:a pcm_s16le";
            } else {
                // Fallback: let ffmpeg choose codec from extension.
                // (Shouldn't happen with validated options.)
            }

            cmd << " " << shell_escape(out_path);

            if (opts.verbose)
                std::cout << "  $ " << cmd.str() << "\n";

            int rc = std::system(cmd.str().c_str());
            if (rc != 0) {
                results.push_back({track.number, out_path, false,
                    "ffmpeg exited with code " + std::to_string(rc)});
            } else {
                results.push_back({track.number, out_path, true, {}});
            }
        }
    }

    return results;
}

} // namespace mithrandir
