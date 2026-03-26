# mithrandir

*The Grey Pilgrim speaks many tongues.* A C++17 CUE sheet splitter with
tagging and MusicBrainz Disc ID computation — the proper successor to the
`gandalf` bash function.

Reads a CUE sheet (or extracts an embedded one from FLAC), splits the source
audio into individual tracks using ffmpeg, tags them via TagLib, and optionally
computes a MusicBrainz Disc ID for easy release lookup.

## Dependencies

- **ffmpeg / ffprobe** — audio splitting and duration probing
- **TagLib** (≥ 1.12) — metadata writing (FLAC Xiph comments, generic tags)
- **OpenSSL** (≥ 1.1) — SHA-1 for MusicBrainz Disc ID
- **metaflac** (optional) — fallback for embedded CUE extraction
- **CMake** ≥ 3.16, a C++17 compiler

### Fedora

```sh
sudo dnf install cmake gcc-c++ taglib-devel openssl-devel ffmpeg metaflac
```

### Debian / Ubuntu

```sh
sudo apt install cmake g++ libtag1-dev libssl-dev ffmpeg flac
```

## Build

```sh
mkdir build && cd build
cmake ..
make -j$(nproc)
```

The binary lands at `build/mithrandir`.

## Usage

### Basic — split a CUE file

```sh
./mithrandir album.cue
```

Outputs FLAC files (compression 8) alongside the source, named
`01 - Track Title.flac`, etc. Tags are written automatically.

### From a FLAC with embedded CUE

```sh
./mithrandir album.flac
```

Extracts the embedded CUE sheet, then splits and tags as above.

### Custom output directory and naming

```sh
./mithrandir -o ~/Music/split -p "{number}. {artist} - {title}" album.cue
```

Available tokens: `{number}`, `{title}`, `{artist}`, `{album}`, `{date}`,
`{genre}`.

### Output as WAV

```sh
./mithrandir -f wav album.cue
```

### Dry run

```sh
./mithrandir --dry-run -v album.cue
```

Shows what would be done (timestamps, filenames) without writing anything.

### Pregap handling

```sh
./mithrandir --pregap prepend album.cue   # include pregap in each track
./mithrandir --pregap append album.cue    # append pregap to previous track
./mithrandir --pregap discard album.cue   # default — start at INDEX 01
```

## MusicBrainz Disc ID

When splitting from a single-FILE CUE (typical for CD rips), mithrandir
computes a MusicBrainz Disc ID from the TOC and prints a lookup URL:

```
MusicBrainz Disc ID: dX5mFkM7h3lKsNBIbcfN4gJfEOo-
  Lookup: https://musicbrainz.org/cdtoc/dX5mFkM7h3lKsNBIbcfN4gJfEOo-
  TOC:    https://musicbrainz.org/cdtoc/attach?toc=1+12+...&tracks=12
```

The TOC URL works even if the Disc ID isn't in the MusicBrainz database yet —
it searches by track offsets, which often matches the correct release.

## Architecture

```
main.cpp          CLI argument parsing, orchestration
types.h / .cpp    Shared data model (CueSheet, CueTrack, SplitOptions)
cue_parser        CUE parsing (file + string), embedded CUE extraction
splitter          ffmpeg-based track extraction with pregap handling
tagger            TagLib-based metadata writer (Xiph comments for FLAC)
discid            MusicBrainz Disc ID computation (SHA-1 + custom base64)
```

### Design decisions

- **TagLib over metaflac**: TagLib gives native C++ access to Xiph comments
  (ALBUMARTIST, DISCNUMBER, TOTALTRACKS, ISRC, BARCODE) without shelling out.
  It also handles WAV/WV/OGG if you ever need those.

- **ffmpeg for splitting**: Sample-accurate seeking for audio, handles FLAC,
  WAV, WavPack (including 32-bit float), APE, and anything else ffmpeg
  supports. `-ss` after `-i` ensures precise timestamps.

- **Custom CUE parser**: CUE is simple enough that a custom parser gives more
  control over edge cases (multi-FILE, REM extensions, encoding quirks) than
  libcue, with zero extra dependencies.

- **Compression level 8**: Matches your `gimli` convention. Maximum FLAC
  compression for archival; negligible speed difference on modern hardware.

## Limitations / future work

- **Character encoding**: Currently assumes UTF-8 (or Latin-1 subset). CUE
  files from Japanese CDs (Shift-JIS) need manual conversion via `iconv` first.
  A future version could auto-detect via ICU or uchardet.

- **Cover art**: Not handled. Use MusicBrainz Picard or `metaflac
  --import-picture-from` post-split.

- **ReplayGain**: Not computed. Run `metaflac --add-replay-gain *.flac`
  on the output directory after splitting.

- **Parallel splitting**: Tracks are split sequentially. For large albums on
  NVMe, parallel extraction via `std::async` would reduce wall time.

## License

MIT
