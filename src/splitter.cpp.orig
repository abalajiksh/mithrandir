#include "splitter.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace mithrandir {

// ─── Helpers ────────────────────────────────────────────────────────

/// Sanitise a string for use as a filename component.
static std::string sanitise_filename(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        // Replace control bytes.
        if (c < 0x20) { out += '_'; continue; }
        switch (c) {
            case '/': case '\\': case ':': case '*':
            case '?': case '"':  case '\'': case '<':
            case '>': case '|':
                out += '_';
                break;
            default:
                out += static_cast<char>(c);
        }
    }
    // Trim trailing dots/spaces.
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

// ─── Process execution (no shell) ───────────────────────────────────

/// Run a command with arguments directly via fork/execvp.
/// No shell involved — paths with quotes, spaces, unicode are all safe.
/// Returns the exit code (0 on success).
static int run_exec(const std::vector<std::string>& argv, bool verbose) {
    if (argv.empty()) return -1;

    if (verbose) {
        std::cout << "  $";
        for (auto& a : argv) {
            bool needs_quote = (a.find(' ') != std::string::npos ||
                                a.find('\'') != std::string::npos ||
                                a.find('"') != std::string::npos);
            if (needs_quote)
                std::cout << " \"" << a << "\"";
            else
                std::cout << " " << a;
        }
        std::cout << "\n";
    }

    // Build C-style argv array for execvp.
    std::vector<const char*> c_argv;
    c_argv.reserve(argv.size() + 1);
    for (auto& s : argv) c_argv.push_back(s.c_str());
    c_argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        // Child — exec directly, no shell.
        execvp(c_argv[0], const_cast<char* const*>(c_argv.data()));
        perror("execvp");
        _exit(127);
    }

    // Parent — wait for child.
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

/// Run a command and capture its stdout via pipe + fork/execvp.
static std::string exec_capture(const std::vector<std::string>& argv) {
    if (argv.empty()) return {};

    int pipefd[2];
    if (pipe(pipefd) < 0) return {};

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return {}; }

    if (pid == 0) {
        // Child: stdout → pipe, stderr → /dev/null.
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }

        std::vector<const char*> c_argv;
        for (auto& s : argv) c_argv.push_back(s.c_str());
        c_argv.push_back(nullptr);
        execvp(c_argv[0], const_cast<char* const*>(c_argv.data()));
        _exit(127);
    }

    // Parent: read stdout from pipe.
    close(pipefd[1]);
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
        out.append(buf, static_cast<size_t>(n));
    close(pipefd[0]);

    waitpid(pid, nullptr, 0);
    return out;
}

// ─── Probing ────────────────────────────────────────────────────────

double probe_duration(const std::filesystem::path& audio_path) {
    auto out = exec_capture({
        "ffprobe", "-v", "error",
        "-show_entries", "format=duration",
        "-of", "default=nokey=1:noprint_wrappers=1",
        audio_path.string()
    });
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

    if (!opts.dry_run)
        fs::create_directories(opts.output_dir);

    int global_track_idx = 0;
    int total = sheet.total_tracks();

    for (size_t fi = 0; fi < sheet.files.size(); ++fi) {
        const auto& cue_file = sheet.files[fi];

        // Resolve the source audio path.
        fs::path src_path = base_dir / cue_file.filename;
        if (!fs::exists(src_path)) {
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

        double total_duration = probe_duration(src_path);

        for (size_t ti = 0; ti < cue_file.tracks.size(); ++ti) {
            const auto& track = cue_file.tracks[ti];
            ++global_track_idx;

            if (on_progress) on_progress(global_track_idx, total);

            // ── Compute start and end in seconds ────────────────
            double start_sec = frames_to_seconds(track.index01_frames);
            double end_sec   = 0.0;

            if (opts.pregap_mode == SplitOptions::PregapMode::Prepend &&
                track.index00_frames.has_value()) {
                start_sec = frames_to_seconds(*track.index00_frames);
            }

            bool is_last_in_file = (ti + 1 >= cue_file.tracks.size());
            if (is_last_in_file) {
                end_sec = total_duration;
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

            // ── Build ffmpeg argv (no shell involved) ───────────
            std::vector<std::string> argv = {
                "ffmpeg", "-y", "-v", "error",
                "-i", src_path.string(),
                "-ss", seconds_to_ffmpeg_ts(start_sec)
            };

            if (!is_last_in_file) {
                argv.push_back("-to");
                argv.push_back(seconds_to_ffmpeg_ts(end_sec));
            }

            if (opts.output_format == "flac") {
                argv.push_back("-c:a");
                argv.push_back("flac");
                argv.push_back("-compression_level");
                argv.push_back(std::to_string(opts.flac_compression));
            } else if (opts.output_format == "wav") {
                argv.push_back("-c:a");
                argv.push_back("pcm_s16le");
            }

            argv.push_back(out_path.string());

            int rc = run_exec(argv, opts.verbose);
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
