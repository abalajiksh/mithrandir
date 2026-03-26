#pragma once

#include "types.h"
#include <string>

namespace mithrandir {

/// Compute a MusicBrainz Disc ID from a CUE sheet's track offsets.
///
/// The algorithm:
///   1. Build a TOC from INDEX 01 offsets (converted to CD sectors)
///      plus the standard 150-frame lead-in.
///   2. Format: first_track, last_track, lead-out offset, then
///      offsets for tracks 1–99 (zero-padded for unused slots).
///   3. SHA-1 hash → MusicBrainz-custom base64.
///
/// The resulting ID can be looked up directly at:
///   https://musicbrainz.org/cdtoc/attach?toc=<toc_string>&tracks=<N>
///
/// NOTE: This is an approximation from CUE data. A true disc ID comes from
/// the physical CD's TOC, but for single-FILE CUEs ripped from CD this
/// is typically identical.
///
/// cue_file_index: which FILE block in the CueSheet to compute for
///                 (default 0 — the first/only FILE).
/// total_duration_sec: duration of the source audio in seconds,
///                     needed to compute the lead-out offset.
///
/// Returns empty string on failure.
std::string compute_mb_discid(const CueSheet& sheet,
                               double total_duration_sec,
                               size_t cue_file_index = 0);

/// Format a MusicBrainz lookup URL from a disc ID + track count.
std::string mb_lookup_url(const std::string& discid);

/// Format a TOC-based MusicBrainz attach URL (works even without a
/// pre-existing disc ID in the database).
std::string mb_toc_lookup_url(const CueSheet& sheet,
                               double total_duration_sec,
                               size_t cue_file_index = 0);

} // namespace mithrandir
