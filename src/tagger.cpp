#include "tagger.h"

#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/xiphcomment.h>

#include <iostream>
#include <string>

namespace mithrandir {

bool tag_track(const std::filesystem::path& file_path,
               const CueSheet& sheet,
               const CueTrack& track) {
    namespace fs = std::filesystem;

    std::string ext = file_path.extension().string();
    // Lowercase for comparison.
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // ── FLAC path (preferred — gives us Xiph comment access) ────
    if (ext == ".flac") {
        TagLib::FLAC::File f(file_path.c_str());
        if (!f.isValid()) {
            std::cerr << "  [tag] Failed to open: " << file_path << "\n";
            return false;
        }

        auto* tag = f.tag();
        tag->setTitle(TagLib::String(track.title, TagLib::String::UTF8));

        // Track artist falls back to album artist.
        std::string artist = track.performer.empty()
                                 ? sheet.performer : track.performer;
        tag->setArtist(TagLib::String(artist, TagLib::String::UTF8));
        tag->setAlbum(TagLib::String(sheet.title, TagLib::String::UTF8));
        tag->setTrack(static_cast<unsigned int>(track.number));

        if (!sheet.genre.empty())
            tag->setGenre(TagLib::String(sheet.genre, TagLib::String::UTF8));
        if (!sheet.date.empty()) {
            try { tag->setYear(static_cast<unsigned int>(std::stoi(sheet.date))); }
            catch (...) {}
        }

        // Extended fields via Xiph comments.
        auto* xiph = f.xiphComment(true);
        if (xiph) {
            // ALBUMARTIST — always set if we have an album performer.
            if (!sheet.performer.empty())
                xiph->addField("ALBUMARTIST",
                    TagLib::String(sheet.performer, TagLib::String::UTF8), true);

            if (sheet.disc_number > 0)
                xiph->addField("DISCNUMBER",
                    TagLib::String(std::to_string(sheet.disc_number),
                                   TagLib::String::UTF8), true);
            if (sheet.total_discs > 0)
                xiph->addField("TOTALDISCS",
                    TagLib::String(std::to_string(sheet.total_discs),
                                   TagLib::String::UTF8), true);

            int total = sheet.total_tracks();
            if (total > 0)
                xiph->addField("TOTALTRACKS",
                    TagLib::String(std::to_string(total),
                                   TagLib::String::UTF8), true);

            if (!track.isrc.empty())
                xiph->addField("ISRC",
                    TagLib::String(track.isrc, TagLib::String::UTF8), true);

            if (!sheet.catalog.empty())
                xiph->addField("BARCODE",
                    TagLib::String(sheet.catalog, TagLib::String::UTF8), true);

            if (!sheet.date.empty())
                xiph->addField("DATE",
                    TagLib::String(sheet.date, TagLib::String::UTF8), true);
        }

        f.save();
        return true;
    }

    // ── Generic path (WAV, WV, etc.) ────────────────────────────
    TagLib::FileRef f(file_path.c_str());
    if (f.isNull() || !f.tag()) {
        std::cerr << "  [tag] Failed to open: " << file_path << "\n";
        return false;
    }

    auto* tag = f.tag();
    tag->setTitle(TagLib::String(track.title, TagLib::String::UTF8));

    std::string artist = track.performer.empty()
                             ? sheet.performer : track.performer;
    tag->setArtist(TagLib::String(artist, TagLib::String::UTF8));
    tag->setAlbum(TagLib::String(sheet.title, TagLib::String::UTF8));
    tag->setTrack(static_cast<unsigned int>(track.number));

    if (!sheet.genre.empty())
        tag->setGenre(TagLib::String(sheet.genre, TagLib::String::UTF8));
    if (!sheet.date.empty()) {
        try { tag->setYear(static_cast<unsigned int>(std::stoi(sheet.date))); }
        catch (...) {}
    }

    f.save();
    return true;
}

} // namespace mithrandir
