#include "types.h"

#include <charconv>
#include <cstdio>
#include <stdexcept>

namespace mithrandir {

std::optional<int64_t> parse_msf(const std::string& msf) {
    // Expected: MM:SS:FF  where FF is CD frames (0-74)
    int mm = 0, ss = 0, ff = 0;
    if (std::sscanf(msf.c_str(), "%d:%d:%d", &mm, &ss, &ff) != 3)
        return std::nullopt;
    if (ss < 0 || ss > 59 || ff < 0 || ff > 99)
        return std::nullopt;
    return static_cast<int64_t>(mm) * 60 * CD_FRAMES_PER_SECOND
         + static_cast<int64_t>(ss) * CD_FRAMES_PER_SECOND
         + ff;
}

std::string seconds_to_ffmpeg_ts(double seconds) {
    int h  = static_cast<int>(seconds) / 3600;
    int m  = (static_cast<int>(seconds) % 3600) / 60;
    double s = seconds - h * 3600 - m * 60;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%09.6f", h, m, s);
    return buf;
}

int CueSheet::total_tracks() const {
    int n = 0;
    for (auto& f : files) n += static_cast<int>(f.tracks.size());
    return n;
}

} // namespace mithrandir
