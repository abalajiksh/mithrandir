#include "cue_parser.h"
#include "discid.h"
#include "splitter.h"
#include "tagger.h"
#include "types.h"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static void print_usage(const char* progname) {
    std::cerr << R"(
Usage: )" << progname << R"( [OPTIONS] <input>

  <input>   A .cue file, or a .flac/.wv file with an embedded CUE sheet.

Options:
  -o, --output-dir DIR     Output directory (default: same as input)
  -f, --format FMT         Output format: flac (default), wav
  -c, --compression N      FLAC compression level 0-8 (default: 8)
  -p, --pattern PAT        Naming pattern (default: "{number} - {title}")
                            Tokens: {number} {title} {artist} {album}
                                    {date} {genre}
  --pregap MODE            Pregap handling: discard (default), prepend, append
  --dry-run                Show what would be done without writing files
  --no-tag                 Skip tagging step
  --no-discid              Skip MusicBrainz Disc ID calculation
  -v, --verbose            Verbose output
  -h, --help               Show this help
)";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    mithrandir::SplitOptions opts;
    std::string input_path_str;
    bool do_tag    = true;
    bool do_discid = true;

    // ── Argument parsing ────────────────────────────────────────
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "-o" || arg == "--output-dir") {
            if (++i >= argc) { std::cerr << "Missing value for " << arg << "\n"; return 1; }
            opts.output_dir = argv[i];
        }
        else if (arg == "-f" || arg == "--format") {
            if (++i >= argc) { std::cerr << "Missing value for " << arg << "\n"; return 1; }
            opts.output_format = argv[i];
        }
        else if (arg == "-c" || arg == "--compression") {
            if (++i >= argc) { std::cerr << "Missing value for " << arg << "\n"; return 1; }
            opts.flac_compression = std::stoi(argv[i]);
        }
        else if (arg == "-p" || arg == "--pattern") {
            if (++i >= argc) { std::cerr << "Missing value for " << arg << "\n"; return 1; }
            opts.naming_pattern = argv[i];
        }
        else if (arg == "--pregap") {
            if (++i >= argc) { std::cerr << "Missing value for " << arg << "\n"; return 1; }
            std::string mode = argv[i];
            if      (mode == "discard") opts.pregap_mode = mithrandir::SplitOptions::PregapMode::Discard;
            else if (mode == "prepend") opts.pregap_mode = mithrandir::SplitOptions::PregapMode::Prepend;
            else if (mode == "append")  opts.pregap_mode = mithrandir::SplitOptions::PregapMode::AppendPrev;
            else { std::cerr << "Unknown pregap mode: " << mode << "\n"; return 1; }
        }
        else if (arg == "--dry-run")    opts.dry_run = true;
        else if (arg == "--no-tag")     do_tag = false;
        else if (arg == "--no-discid")  do_discid = false;
        else if (arg == "-v" || arg == "--verbose") opts.verbose = true;
        else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
        else {
            input_path_str = arg;
        }
    }

    if (input_path_str.empty()) {
        std::cerr << "Error: no input file specified.\n";
        return 1;
    }

    fs::path input_path = fs::absolute(input_path_str);
    if (!fs::exists(input_path)) {
        std::cerr << "Error: file not found: " << input_path << "\n";
        return 1;
    }

    fs::path base_dir = input_path.parent_path();
    if (opts.output_dir.empty())
        opts.output_dir = base_dir;

    // ── Parse CUE ───────────────────────────────────────────────
    mithrandir::CueSheet sheet;
    std::string input_ext = input_path.extension().string();
    for (auto& c : input_ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    try {
        if (input_ext == ".cue") {
            sheet = mithrandir::parse_cue_file(input_path);
        } else if (input_ext == ".flac" || input_ext == ".wv") {
            // Try to extract embedded CUE.
            auto embedded = mithrandir::extract_embedded_cue(input_path);
            if (!embedded) {
                // Also check for a .cue file alongside.
                fs::path sidecar = input_path;
                sidecar.replace_extension(".cue");
                if (fs::exists(sidecar)) {
                    sheet = mithrandir::parse_cue_file(sidecar);
                } else {
                    std::cerr << "Error: no embedded CUE sheet found in "
                              << input_path.filename()
                              << ", and no sidecar .cue file exists.\n";
                    return 1;
                }
            } else {
                sheet = mithrandir::parse_cue_string(*embedded, base_dir);
                // If the embedded CUE has no FILE directive, inject one
                // pointing at the input file itself.
                if (sheet.files.empty()) {
                    std::cerr << "Error: embedded CUE has no FILE directive and no tracks.\n";
                    return 1;
                }
                // Override the FILE path to point to the actual input.
                for (auto& f : sheet.files)
                    f.filename = input_path.filename().string();
            }
        } else {
            std::cerr << "Error: unsupported input type '" << input_ext
                      << "'. Expected .cue, .flac, or .wv.\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing CUE: " << e.what() << "\n";
        return 1;
    }

    // ── Summary ─────────────────────────────────────────────────
    std::cout << "Album:    " << sheet.title << "\n"
              << "Artist:   " << sheet.performer << "\n";
    if (!sheet.date.empty())
        std::cout << "Date:     " << sheet.date << "\n";
    if (!sheet.genre.empty())
        std::cout << "Genre:    " << sheet.genre << "\n";
    if (sheet.disc_number > 0)
        std::cout << "Disc:     " << sheet.disc_number
                  << (sheet.total_discs > 0
                      ? " / " + std::to_string(sheet.total_discs) : "")
                  << "\n";
    std::cout << "Tracks:   " << sheet.total_tracks() << "\n"
              << "Files:    " << sheet.files.size() << "\n"
              << "Format:   " << opts.output_format;
    if (opts.output_format == "flac")
        std::cout << " (compression " << opts.flac_compression << ")";
    std::cout << "\n"
              << "Output:   " << opts.output_dir.string() << "\n";
    if (opts.dry_run)
        std::cout << "          *** DRY RUN ***\n";
    std::cout << "\n";

    // ── MusicBrainz Disc ID ─────────────────────────────────────
    if (do_discid && !sheet.files.empty()) {
        // We need the source file duration for lead-out calculation.
        fs::path src = base_dir / sheet.files[0].filename;
        if (fs::exists(src)) {
            double dur = mithrandir::probe_duration(src);
            if (dur > 0.0) {
                auto discid = mithrandir::compute_mb_discid(sheet, dur);
                if (!discid.empty()) {
                    std::cout << "MusicBrainz Disc ID: " << discid << "\n";
                    std::cout << "  Lookup: " << mithrandir::mb_lookup_url(discid) << "\n";
                    auto toc_url = mithrandir::mb_toc_lookup_url(sheet, dur);
                    if (!toc_url.empty())
                        std::cout << "  TOC:    " << toc_url << "\n";
                    std::cout << "\n";
                }
            }
        }
    }

    // ── Split ───────────────────────────────────────────────────
    std::cout << "Splitting tracks...\n";
    auto results = mithrandir::split_tracks(sheet, base_dir, opts,
        [&](int current, int total) {
            if (opts.verbose)
                std::cout << "[" << current << "/" << total << "] ";
        });

    // ── Tag ─────────────────────────────────────────────────────
    if (do_tag && !opts.dry_run) {
        std::cout << "Tagging...\n";

        // Build a flat list of tracks for lookup by number.
        int idx = 0;
        for (auto& file : sheet.files) {
            for (auto& track : file.tracks) {
                if (idx < static_cast<int>(results.size()) &&
                    results[idx].success) {
                    bool ok = mithrandir::tag_track(results[idx].output_path,
                                                  sheet, track);
                    if (!ok)
                        std::cerr << "  Warning: tagging failed for track "
                                  << track.number << "\n";
                    else if (opts.verbose)
                        std::cout << "  Tagged: " << results[idx].output_path.filename()
                                  << "\n";
                }
                ++idx;
            }
        }
    }

    // ── Report ──────────────────────────────────────────────────
    std::cout << "\nResults:\n";
    int ok_count = 0, fail_count = 0;
    for (auto& r : results) {
        if (r.success) {
            ++ok_count;
            std::cout << "  ✓ " << r.output_path.filename().string();
            if (!r.error_msg.empty()) std::cout << "  " << r.error_msg;
            std::cout << "\n";
        } else {
            ++fail_count;
            std::cout << "  ✗ Track " << r.track_number
                      << ": " << r.error_msg << "\n";
        }
    }
    std::cout << "\n" << ok_count << " succeeded";
    if (fail_count > 0) std::cout << ", " << fail_count << " failed";
    std::cout << ".\n";

    return (fail_count > 0) ? 1 : 0;
}
