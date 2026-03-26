#pragma once

#include "types.h"
#include <filesystem>
#include <string>

namespace mithrandir {

/// Apply CUE metadata to a split track file using TagLib.
/// Writes standard tags (title, artist, album, year, genre, track number)
/// and extended Xiph comments (DISCNUMBER, TOTALDISCS, TOTALTRACKS, ISRC,
/// ALBUMARTIST).
/// Returns true on success.
bool tag_track(const std::filesystem::path& file_path,
               const CueSheet& sheet,
               const CueTrack& track);

} // namespace mithrandir
