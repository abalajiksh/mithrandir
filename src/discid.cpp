#include "discid.h"

#include <openssl/sha.h>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <vector>

namespace mithrandir {

// MusicBrainz uses a modified base64 alphabet:
//   Standard base64:  +/=
//   MB variant:       ._-
static const char MB_BASE64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._";

static std::string mb_base64_encode(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);

        out += MB_BASE64[(n >> 18) & 0x3F];
        out += MB_BASE64[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? MB_BASE64[(n >> 6) & 0x3F] : '-';
        out += (i + 2 < len) ? MB_BASE64[n & 0x3F]        : '-';
    }
    return out;
}

std::string compute_mb_discid(const CueSheet& sheet,
                               double total_duration_sec,
                               size_t cue_file_index) {
    if (cue_file_index >= sheet.files.size()) return {};
    const auto& file = sheet.files[cue_file_index];
    if (file.tracks.empty()) return {};

    int first_track = file.tracks.front().number;
    int last_track  = file.tracks.back().number;

    // Lead-out offset in CD sectors (frames):
    //   total_duration * 75 + 150 (lead-in)
    int64_t leadout = static_cast<int64_t>(total_duration_sec * CD_FRAMES_PER_SECOND)
                    + CD_LEAD_IN_FRAMES;

    // Build the SHA-1 input string.
    // Format: "%02X%02X%08X" for first, last, leadout,
    //         then "%08X" for offsets of tracks 1–99 (0 for unused).
    char buf[2048];
    int pos = 0;
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "%02X", first_track);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "%02X", last_track);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "%08X",
                         static_cast<unsigned int>(leadout));

    // Offsets for tracks 1–99.
    for (int i = 0; i < 99; ++i) {
        int track_num = i + 1;
        int64_t offset = 0;
        // Find this track in the CUE file.
        for (const auto& t : file.tracks) {
            if (t.number == track_num) {
                offset = t.index01_frames + CD_LEAD_IN_FRAMES;
                break;
            }
        }
        pos += std::snprintf(buf + pos, sizeof(buf) - pos, "%08X",
                             static_cast<unsigned int>(offset));
    }

    // SHA-1.
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(buf),
         static_cast<size_t>(pos), hash);

    return mb_base64_encode(hash, SHA_DIGEST_LENGTH);
}

std::string mb_lookup_url(const std::string& discid) {
    if (discid.empty()) return {};
    return "https://musicbrainz.org/cdtoc/" + discid;
}

std::string mb_toc_lookup_url(const CueSheet& sheet,
                               double total_duration_sec,
                               size_t cue_file_index) {
    if (cue_file_index >= sheet.files.size()) return {};
    const auto& file = sheet.files[cue_file_index];
    if (file.tracks.empty()) return {};

    int first = file.tracks.front().number;
    int last  = file.tracks.back().number;
    int64_t leadout = static_cast<int64_t>(total_duration_sec * CD_FRAMES_PER_SECOND)
                    + CD_LEAD_IN_FRAMES;

    // TOC string: "1 N leadout offset1 offset2 ..."
    std::ostringstream toc;
    toc << first << " " << last << " " << leadout;
    for (const auto& t : file.tracks)
        toc << " " << (t.index01_frames + CD_LEAD_IN_FRAMES);

    // URL-encode spaces as +.
    std::string toc_str = toc.str();
    std::string encoded;
    for (char c : toc_str) {
        if (c == ' ') encoded += '+';
        else          encoded += c;
    }

    return "https://musicbrainz.org/cdtoc/attach?toc=" + encoded
         + "&tracks=" + std::to_string(last - first + 1);
}

} // namespace mithrandir
