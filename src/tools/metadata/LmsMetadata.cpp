/*
 * Copyright (C) 2019 Emeric Poupon
 *
 * This file is part of LMS.
 *
 * LMS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LMS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <chrono>
#include <iostream>
#include <iomanip>
#include <optional>
#include <stdexcept>
#include <stdlib.h>

#include <Wt/WDate.h>

#include "metadata/IParser.hpp"
#include "utils/StreamLogger.hpp"

namespace
{
    std::ostream& operator<<(std::ostream& os, const MetaData::Artist& artist)
    {
        os << artist.name;

        if (artist.mbid)
            os << " (" << artist.mbid->getAsString() << ")";

        if (artist.sortName)
            os << " '" << *artist.sortName << "'";

        return os;
    }

    std::ostream& operator<<(std::ostream& os, const MetaData::Release& release)
    {
        os << release.name;

        if (release.mbid)
            os << " (" << release.mbid->getAsString() << ")" << std::endl;

        if (release.mediumCount)
            std::cout << "\tMediumCount: " << *release.mediumCount << std::endl;

        if (!release.artistDisplayName.empty())
            std::cout << "\tDisplay artist: " << release.artistDisplayName << std::endl;

        for (const MetaData::Artist& artist : release.artists)
            std::cout << "\tRelease artist: " << artist << std::endl;

        std::cout << "Release types:" << std::endl;
        for (std::string_view releaseType : release.releaseTypes)
            std::cout << "\t" << releaseType << std::endl;

        return os;
    }

    std::ostream& operator<<(std::ostream& os, const MetaData::Medium& medium)
    {
        if (!medium.name.empty())
            os << medium.name;
        os << std::endl;

        if (medium.position)
            os << "\tPosition: " << *medium.position << std::endl;

        if (!medium.type.empty())
            os << "\tType: " << medium.type << std::endl;

        if (medium.trackCount)
            std::cout << "\tTrackCount: " << *medium.trackCount << std::endl;

        if (medium.replayGain)
            std::cout << "\tReplay gain: " << *medium.replayGain << std::endl;

        if (medium.release)
            std::cout << "Release: " << *medium.release << std::endl;

        return os;
    }

    void parse(MetaData::IParser& parser, const std::filesystem::path& file)
    {
        using namespace MetaData;

        parser.setUserExtraTags({ "MOOD", "ALBUMGROUPING", "GENRE", "LANGUAGE" });

        const auto start{ std::chrono::steady_clock::now() };
        std::optional<Track> track{ parser.parse(file, true) };
        if (!track)
        {
            std::cerr << "Parsing failed" << std::endl;
            return;
        }
        const auto end{ std::chrono::steady_clock::now() };

        std::cout << "Parsing time: " << std::fixed << std::setprecision(2) << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000. << "ms" << std::endl;

        std::cout << "Parsed metadata:" << std::endl;

        if (!track->artistDisplayName.empty())
            std::cout << "Display artist: " << track->artistDisplayName << std::endl;

        for (const Artist& artist : track->artists)
            std::cout << "Artist: " << artist << std::endl;

        for (const Artist& artist : track->conductorArtists)
            std::cout << "Conductor: " << artist << std::endl;

        for (const Artist& artist : track->composerArtists)
            std::cout << "Composer: " << artist << std::endl;

        for (const Artist& artist : track->lyricistArtists)
            std::cout << "Lyricist: " << artist << std::endl;

        for (const Artist& artist : track->mixerArtists)
            std::cout << "Mixer: " << artist << std::endl;

        for (const auto& [role, artists] : track->performerArtists)
        {
            std::cout << "Performer";
            if (!role.empty())
                std::cout << " (" << role << ")";
            std::cout << ":" << std::endl;
            for (const Artist& artist : artists)
                std::cout << "\t" << artist << std::endl;
        }

        for (const Artist& artist : track->producerArtists)
            std::cout << "Producer: " << artist << std::endl;

        for (const Artist& artist : track->remixerArtists)
            std::cout << "Remixer: " << artist << std::endl;

        if (track->medium)
            std::cout << "Medium: " << *track->medium;

        std::cout << "Title: " << track->title << std::endl;

        if (track->mbid)
            std::cout << "Track MBID = " << track->mbid->getAsString() << std::endl;

        if (track->recordingMBID)
            std::cout << "Recording MBID = " << track->recordingMBID->getAsString() << std::endl;

        for (const auto& [tag, values] : track->userExtraTags)
        {
            std::cout << "Tag: " << tag << std::endl;
            for (const auto& value : values)
            {
                std::cout << "\t" << value << std::endl;
            }
        }

        std::cout << "Duration: " << std::fixed << std::setprecision(2) << track->duration.count() / 1000. << "s" << std::endl;
        std::cout << "Bitrate: " << track->bitrate << " bps" << std::endl;

        if (track->position)
            std::cout << "Position: " << *track->position << std::endl;

        if (track->date.isValid())
            std::cout << "Date: " << track->date.toString("yyyy-MM-dd") << std::endl;

        if (track->originalDate.isValid())
            std::cout << "Original date: " << track->originalDate.toString("yyyy-MM-dd") << std::endl;

        std::cout << "HasCover = " << std::boolalpha << track->hasCover << std::endl;

        if (track->replayGain)
            std::cout << "Track replay gain: " << *track->replayGain << std::endl;

        if (track->acoustID)
            std::cout << "AcoustID: " << track->acoustID->getAsString() << std::endl;

        if (!track->copyright.empty())
            std::cout << "Copyright: " << track->copyright << std::endl;

        if (!track->copyrightURL.empty())
            std::cout << "CopyrightURL: " << track->copyrightURL << std::endl;

        std::cout << std::endl;
    }
}

int main(int argc, char* argv[])
{
    if (argc == 1)
    {
        std::cerr << "Usage: <file> [<file> ...]" << std::endl;
        return EXIT_FAILURE;
    }

    try
    {
        // log to stdout
        Service<ILogger> logger{ std::make_unique<StreamLogger>(std::cout) };

        for (std::size_t i{}; i < static_cast<std::size_t>(argc - 1); ++i)
        {
            std::filesystem::path file{ argv[i + 1] };

            std::cout << "Parsing file '" << file << "'" << std::endl;

            {
                std::cout << "Using av:" << std::endl;
                auto parser{ MetaData::createParser(MetaData::ParserType::AvFormat, MetaData::ParserReadStyle::Accurate) };
                parse(*parser, file);
            }

            {
                std::cout << "Using TagLib:" << std::endl;
                auto parser{ MetaData::createParser(MetaData::ParserType::TagLib, MetaData::ParserReadStyle::Accurate) };
                parse(*parser, file);
            }
        }
    }
    catch (std::exception& e)
    {
        std::cerr << "Caught exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
