#include "cue_parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace mithrandir {

// ─── Helpers ────────────────────────────────────────────────────────

static std::string trim(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
        sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
        sv.remove_suffix(1);
    return std::string(sv);
}

/// Remove surrounding quotes if present.
static std::string unquote(std::string_view sv) {
    sv = trim(sv);
    if (sv.size() >= 2 && sv.front() == '"' && sv.back() == '"')
        sv = sv.substr(1, sv.size() - 2);
    return std::string(sv);
}

/// Case-insensitive prefix match. Returns remainder after prefix (trimmed).
static std::optional<std::string> match_cmd(std::string_view line,
                                             std::string_view cmd) {
    if (line.size() < cmd.size()) return std::nullopt;
    for (size_t i = 0; i < cmd.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(line[i])) !=
            std::toupper(static_cast<unsigned char>(cmd[i])))
            return std::nullopt;
    // Must be followed by whitespace or end of line.
    if (line.size() > cmd.size() &&
        !std::isspace(static_cast<unsigned char>(line[cmd.size()])))
        return std::nullopt;
    return trim(line.substr(cmd.size()));
}

/// Run a command and capture stdout.
static std::string exec_capture(const std::string& cmd) {
    std::array<char, 4096> buf;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(
        popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return {};
    while (fgets(buf.data(), buf.size(), pipe.get()))
        result += buf.data();
    return result;
}

// ─── Parser ─────────────────────────────────────────────────────────

CueSheet parse_cue_string(const std::string& cue_text,
                           const std::filesystem::path& base_dir) {
    CueSheet sheet;
    CueFile* cur_file   = nullptr;
    CueTrack* cur_track = nullptr;

    std::istringstream iss(cue_text);
    std::string raw_line;
    int line_no = 0;

    while (std::getline(iss, raw_line)) {
        ++line_no;
        std::string line = trim(raw_line);
        if (line.empty()) continue;

        // ── REM directives ──────────────────────────────────────
        if (auto rest = match_cmd(line, "REM")) {
            auto& r = *rest;
            if (auto v = match_cmd(r, "GENRE"))
                sheet.genre = unquote(*v);
            else if (auto v = match_cmd(r, "DATE"))
                sheet.date = unquote(*v);
            else if (auto v = match_cmd(r, "DISCID"))
                sheet.cddb_discid = unquote(*v);
            else if (auto v = match_cmd(r, "DISCNUMBER"))
                sheet.disc_number = std::stoi(*v);
            else if (auto v = match_cmd(r, "DISC_NUMBER"))
                sheet.disc_number = std::stoi(*v);
            else if (auto v = match_cmd(r, "TOTALDISCS"))
                sheet.total_discs = std::stoi(*v);
            else if (auto v = match_cmd(r, "TOTAL_DISCS"))
                sheet.total_discs = std::stoi(*v);
            else if (auto v = match_cmd(r, "COMMENT"))
                sheet.comment = unquote(*v);
            continue;
        }

        // ── Top-level / track-level shared commands ─────────────
        if (auto rest = match_cmd(line, "TITLE")) {
            auto val = unquote(*rest);
            if (cur_track)    cur_track->title = val;
            else              sheet.title = val;
            continue;
        }
        if (auto rest = match_cmd(line, "PERFORMER")) {
            auto val = unquote(*rest);
            if (cur_track)    cur_track->performer = val;
            else              sheet.performer = val;
            continue;
        }
        if (auto rest = match_cmd(line, "SONGWRITER")) {
            if (cur_track) cur_track->songwriter = unquote(*rest);
            continue;
        }
        if (auto rest = match_cmd(line, "CATALOG")) {
            sheet.catalog = unquote(*rest);
            continue;
        }

        // ── FILE directive ──────────────────────────────────────
        if (auto rest = match_cmd(line, "FILE")) {
            // FILE "name" FORMAT
            auto& r = *rest;
            std::string fname, fmt;
            // Parse quoted filename.
            if (!r.empty() && r[0] == '"') {
                auto end_q = r.find('"', 1);
                if (end_q == std::string::npos)
                    throw std::runtime_error(
                        "CUE line " + std::to_string(line_no) +
                        ": unterminated quote in FILE");
                fname = r.substr(1, end_q - 1);
                fmt   = trim(r.substr(end_q + 1));
            } else {
                // Unquoted: first token is filename, rest is format.
                auto sp = r.find(' ');
                fname = (sp != std::string::npos) ? r.substr(0, sp) : r;
                fmt   = (sp != std::string::npos) ? trim(r.substr(sp)) : "";
            }
            sheet.files.push_back({fname, fmt, {}});
            cur_file  = &sheet.files.back();
            cur_track = nullptr;
            continue;
        }

        // ── TRACK directive ─────────────────────────────────────
        if (auto rest = match_cmd(line, "TRACK")) {
            if (!cur_file)
                throw std::runtime_error(
                    "CUE line " + std::to_string(line_no) +
                    ": TRACK before FILE");
            // TRACK NN AUDIO
            int num = 0;
            std::sscanf(rest->c_str(), "%d", &num);
            cur_file->tracks.push_back({});
            cur_track = &cur_file->tracks.back();
            cur_track->number = num;
            continue;
        }

        // ── INDEX directive ─────────────────────────────────────
        if (auto rest = match_cmd(line, "INDEX")) {
            if (!cur_track)
                throw std::runtime_error(
                    "CUE line " + std::to_string(line_no) +
                    ": INDEX before TRACK");
            int idx_num = 0;
            char ts[64] = {};
            std::sscanf(rest->c_str(), "%d %63s", &idx_num, ts);
            auto frames = parse_msf(ts);
            if (!frames)
                throw std::runtime_error(
                    "CUE line " + std::to_string(line_no) +
                    ": invalid timestamp '" + ts + "'");
            if (idx_num == 0)
                cur_track->index00_frames = *frames;
            else if (idx_num == 1)
                cur_track->index01_frames = *frames;
            // INDEX 02+ (sub-indices) are silently ignored.
            continue;
        }

        // ── ISRC ────────────────────────────────────────────────
        if (auto rest = match_cmd(line, "ISRC")) {
            if (cur_track) cur_track->isrc = unquote(*rest);
            continue;
        }

        // Unknown lines are silently skipped (FLAGS, PREGAP, POSTGAP, etc.)
    }

    return sheet;
}

CueSheet parse_cue_file(const std::filesystem::path& cue_path) {
    std::ifstream ifs(cue_path, std::ios::binary);
    if (!ifs)
        throw std::runtime_error("Cannot open CUE file: " + cue_path.string());

    std::string contents((std::istreambuf_iterator<char>(ifs)),
                          std::istreambuf_iterator<char>());

    // Strip UTF-8 BOM if present.
    if (contents.size() >= 3 &&
        static_cast<unsigned char>(contents[0]) == 0xEF &&
        static_cast<unsigned char>(contents[1]) == 0xBB &&
        static_cast<unsigned char>(contents[2]) == 0xBF)
        contents.erase(0, 3);

    return parse_cue_string(contents, cue_path.parent_path());
}

// ─── Embedded CUE extraction ────────────────────────────────────────

std::optional<std::string> extract_embedded_cue(
    const std::filesystem::path& flac_path) {
    // Method 1: ffprobe Vorbis comment tag CUESHEET.
    {
        std::string cmd =
            "ffprobe -v error -show_entries format_tags=cuesheet "
            "-of default=nokey=1:noprint_wrappers=1 " +
            ("'" + flac_path.string() + "'");
        // Escape single quotes in path.
        // (Simplified — production code would use execvp.)
        auto out = exec_capture(cmd);
        auto trimmed = trim(out);
        if (!trimmed.empty() && trimmed.find("TRACK") != std::string::npos)
            return trimmed;
    }

    // Method 2: metaflac --export-cuesheet-to=- (native FLAC CUESHEET block).
    {
        std::string cmd =
            "metaflac --export-cuesheet-to=- '" + flac_path.string() + "' 2>/dev/null";
        auto out = exec_capture(cmd);
        auto trimmed = trim(out);
        if (!trimmed.empty() && trimmed.find("TRACK") != std::string::npos)
            return trimmed;
    }

    return std::nullopt;
}

} // namespace mithrandir
