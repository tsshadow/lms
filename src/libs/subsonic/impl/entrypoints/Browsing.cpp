/*
 * Copyright (C) 2023 Emeric Poupon
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

#include "Browsing.hpp"

#include "database/Artist.hpp"
#include "database/Cluster.hpp"
#include "database/MediaLibrary.hpp"
#include "database/Session.hpp"
#include "database/Release.hpp"
#include "database/Track.hpp"
#include "database/User.hpp"
#include "services/recommendation/IRecommendationService.hpp"
#include "services/scrobbling/IScrobblingService.hpp"
#include "core/ILogger.hpp"
#include "core/Random.hpp"
#include "core/Service.hpp"
#include "responses/Album.hpp"
#include "responses/Artist.hpp"
#include "responses/Genre.hpp"
#include "responses/Song.hpp"
#include "ParameterParsing.hpp"
#include "SubsonicId.hpp"
#include "Utils.hpp"
#include "RequestCacher.h"

static auto reqCacher = RequestCacher::getInstance();
namespace lms::api::subsonic
{
    using namespace db;

    static const unsigned long long	reportedDummyDateULong{ 946684800000ULL }; // 2000-01-01T00:00:00 UTC

    namespace
    {
        Response handleGetArtistInfoRequestCommon(RequestContext& context, bool id3)
        {
            // Mandatory params
            ArtistId id{ getMandatoryParameterAs<ArtistId>(context.parameters, "id") };

            // Optional params
            std::size_t count{ getParameterAs<std::size_t>(context.parameters, "count").value_or(20) };

            Response response{ Response::createOkResponse(context.serverProtocolVersion) };
            Response::Node& artistInfoNode{ response.createNode(id3 ? Response::Node::Key{ "artistInfo2" } : Response::Node::Key{ "artistInfo" }) };

            {
                auto transaction{ context.dbSession.createReadTransaction() };

                const Artist::pointer artist{ Artist::find(context.dbSession, id) };
                if (!artist)
                    throw RequestedDataNotFoundError{};

                std::optional<core::UUID> artistMBID{ artist->getMBID() };
                if (artistMBID)
                    artistInfoNode.createChild("musicBrainzId").setValue(artistMBID->getAsString());
            }

            auto similarArtistsId{ core::Service<recommendation::IRecommendationService>::get()->getSimilarArtists(id, {TrackArtistLinkType::Artist, TrackArtistLinkType::ReleaseArtist}, count) };

            {
                auto transaction{ context.dbSession.createReadTransaction() };

                for (const ArtistId similarArtistId : similarArtistsId)
                {
                    const Artist::pointer similarArtist{ Artist::find(context.dbSession, similarArtistId) };
                    if (similarArtist)
                        artistInfoNode.addArrayChild("similarArtist", createArtistNode(context, similarArtist, context.user, id3));
                }
            }

            return response;
        }

        Response handleGetArtistsRequestCommon(RequestContext& context, bool id3)
        {
            // Optional params
            const MediaLibraryId mediaLibrary{ getParameterAs<MediaLibraryId>(context.parameters, "musicFolderId").value_or(MediaLibraryId{}) };

            Response response{ Response::createOkResponse(context.serverProtocolVersion) };

            Response::Node& artistsNode{ response.createNode(id3 ? "artists" : "indexes") };
            artistsNode.setAttribute("ignoredArticles", "");
            artistsNode.setAttribute("lastModified", reportedDummyDateULong); // TODO report last file write?

            Artist::FindParameters parameters;
            {
                auto transaction{ context.dbSession.createReadTransaction() };

                parameters.setSortMethod(ArtistSortMethod::SortName);
                switch (context.user->getSubsonicArtistListMode())
                {
                    case SubsonicArtistListMode::AllArtists:
                        break;
                    case SubsonicArtistListMode::ReleaseArtists:
                        parameters.setLinkType(TrackArtistLinkType::ReleaseArtist);
                        break;
                    case SubsonicArtistListMode::TrackArtists:
                        parameters.setLinkType(TrackArtistLinkType::Artist);
                        break;
                }
            }
            parameters.setMediaLibrary(mediaLibrary);

            // This endpoint does not scale: make sort lived transactions in order not to block the whole application

            // first pass: dispatch the artists by first letter
            LMS_LOG(API_SUBSONIC, DEBUG, "GetArtists: fetching all artists...");
            std::map<char, std::vector<ArtistId>> artistsSortedByFirstChar;
            std::size_t currentArtistOffset{ 0 };
            constexpr std::size_t batchSize{ 100 };
//            bool hasMoreArtists{ true };
//            while (hasMoreArtists)
//            {
//                auto transaction{ context.dbSession.createReadTransaction() };
//
//                parameters.setRange(Range{ currentArtistOffset, batchSize });
//                const auto artists{ Artist::find(context.dbSession, parameters) };
//                for (const Artist::pointer& artist : artists.results)
//                {
//                    std::string_view sortName{ artist->getSortName() };
//
//                    char sortChar;
//                    if (sortName.empty() || !std::isalpha(sortName[0]))
//                        sortChar = '?';
//                    else
//                        sortChar = std::toupper(sortName[0]);
//
//                    artistsSortedByFirstChar[sortChar].push_back(artist->getId());
//                }
//
//                hasMoreArtists = artists.moreResults;
//                currentArtistOffset += artists.results.size();
//            }

            // second pass: add each artist
            LMS_LOG(API_SUBSONIC, DEBUG, "GetArtists: constructing response...");
            for (const auto& [sortChar, artistIds] : artistsSortedByFirstChar)
            {
                Response::Node& indexNode{ artistsNode.createArrayChild("index") };
                indexNode.setAttribute("name", std::string{ sortChar });

                for (const ArtistId artistId : artistIds)
                {
                    auto transaction{ context.dbSession.createReadTransaction() };

                    if (const Artist::pointer artist{ Artist::find(context.dbSession, artistId) })
                        indexNode.addArrayChild("artist", createArtistNode(context, artist, context.user, id3));
                }
            }

            return response;
        }

        std::vector<TrackId> findSimilarSongs(RequestContext& context, ArtistId artistId, std::size_t count)
        {
            // API says: "Returns a random collection of songs from the given artist and similar artists"
            const std::size_t similarArtistCount{ count / 5 };
            std::vector<ArtistId> artistIds{ core::Service<recommendation::IRecommendationService>::get()->getSimilarArtists(artistId, {TrackArtistLinkType::Artist, TrackArtistLinkType::ReleaseArtist}, similarArtistCount) };
            artistIds.push_back(artistId);

            const std::size_t meanTrackCountPerArtist{ (count / artistIds.size()) + 1 };

            auto transaction{ context.dbSession.createReadTransaction() };

            std::vector<TrackId> tracks;
            tracks.reserve(count);

            for (const ArtistId id : artistIds)
            {
                Track::FindParameters params;
                params.setArtist(id);
                params.setRange(Range{ 0, meanTrackCountPerArtist });
                params.setSortMethod(TrackSortMethod::Random);

                const auto artistTracks{ Track::findIds(context.dbSession, params) };
                tracks.insert(std::end(tracks),
                              std::begin(artistTracks.results),
                              std::end(artistTracks.results));
            }

            return tracks;
        }

        std::vector<TrackId> findSimilarSongs(RequestContext& context, ReleaseId releaseId, std::size_t count)
        {
            // API says: "Returns a random collection of songs from the given artist and similar artists"
            // so let's extend this for release
            const std::size_t similarReleaseCount{ count / 5 };
            std::vector<ReleaseId> releaseIds{ core::Service<recommendation::IRecommendationService>::get()->getSimilarReleases(releaseId, similarReleaseCount) };
            releaseIds.push_back(releaseId);

            const std::size_t meanTrackCountPerRelease{ (count / releaseIds.size()) + 1 };

            auto transaction{ context.dbSession.createReadTransaction() };

            std::vector<TrackId> tracks;
            tracks.reserve(count);

            for (const ReleaseId id : releaseIds)
            {
                Track::FindParameters params;
                params.setRelease(id);
                params.setRange(Range{ 0, meanTrackCountPerRelease });
                params.setSortMethod(TrackSortMethod::Random);

                const auto releaseTracks{ Track::findIds(context.dbSession, params) };
                tracks.insert(std::end(tracks),
                              std::begin(releaseTracks.results),
                              std::end(releaseTracks.results));
            }

            return tracks;
        }

        std::vector<TrackId> findSimilarSongs(RequestContext&, TrackId trackId, std::size_t count)
        {
            return core::Service<recommendation::IRecommendationService>::get()->findSimilarTracks({ trackId }, count);
        }

        Response handleGetSimilarSongsRequestCommon(RequestContext& context, bool id3)
        {
            // Optional params
            std::size_t count{ getParameterAs<std::size_t>(context.parameters, "count").value_or(50) };
            if (count > defaultMaxCountSize)
                throw ParameterValueTooHighGenericError{ "count", defaultMaxCountSize };

            std::vector<TrackId> tracks;

            if (const auto artistId{ getParameterAs<ArtistId>(context.parameters, "id") })
                tracks = findSimilarSongs(context, *artistId, count);
            else if (const auto releaseId{ getParameterAs<ReleaseId>(context.parameters, "id") })
                tracks = findSimilarSongs(context, *releaseId, count);
            else if (const auto trackId{ getParameterAs<TrackId>(context.parameters, "id") })
                tracks = findSimilarSongs(context, *trackId, count);
            else
                throw BadParameterGenericError{ "id" };

            core::random::shuffleContainer(tracks);

            auto transaction{ context.dbSession.createReadTransaction() };

            Response response{ Response::createOkResponse(context.serverProtocolVersion) };
            Response::Node& similarSongsNode{ response.createNode(id3 ? Response::Node::Key{ "similarSongs2" } : Response::Node::Key{ "similarSongs" }) };
            for (const TrackId trackId : tracks)
            {
                const Track::pointer track{ Track::find(context.dbSession, trackId) };
                similarSongsNode.addArrayChild("song", createSongNode(context, track, context.user));
            }

            return response;
        }

    }

    Response handleGetMusicFoldersRequest(RequestContext& context)
    {
        Response response{ Response::createOkResponse(context.serverProtocolVersion) };
        Response::Node& musicFoldersNode{ response.createNode("musicFolders") };

        auto transaction{ context.dbSession.createReadTransaction() };
        MediaLibrary::find(context.dbSession, [&](const MediaLibrary::pointer& library)
        {
            Response::Node& musicFolderNode{ musicFoldersNode.createArrayChild("musicFolder") };

            musicFolderNode.setAttribute("id", idToString(library->getId()));
            musicFolderNode.setAttribute("name", library->getName());
        });

        return response;
    }

    Response handleGetIndexesRequest(RequestContext& context)
    {
        return handleGetArtistsRequestCommon(context, false /* no id3 */);
    }

    Response handleGetMusicDirectoryRequest(RequestContext& context)
    {
        // Mandatory params
        const auto artistId{ getParameterAs<ArtistId>(context.parameters, "id") };
        const auto releaseId{ getParameterAs<ReleaseId>(context.parameters, "id") };
        const auto root{ getParameterAs<RootId>(context.parameters, "id") };

        if (!root && !artistId && !releaseId)
            throw BadParameterGenericError{ "id" };

        Response response{ Response::createOkResponse(context.serverProtocolVersion) };
        Response::Node& directoryNode{ response.createNode("directory") };

        auto transaction{ context.dbSession.createReadTransaction() };

        if (root)
        {
            directoryNode.setAttribute("id", idToString(RootId{}));
            directoryNode.setAttribute("name", "Music");

            // TODO: this does not scale when a lot of artists are present
            Artist::find(context.dbSession, Artist::FindParameters{}.setSortMethod(ArtistSortMethod::SortName), [&](const Artist::pointer& artist)
            {
                directoryNode.addArrayChild("child", createArtistNode(context, artist, context.user, false /* no id3 */));
            });
        }
        else if (artistId)
        {
            directoryNode.setAttribute("id", idToString(*artistId));

            auto artist{ Artist::find(context.dbSession, *artistId) };
            if (!artist)
                throw RequestedDataNotFoundError{};

            directoryNode.setAttribute("name", utils::makeNameFilesystemCompatible(artist->getName()));

            Release::find(context.dbSession, Release::FindParameters{}.setArtist(*artistId), [&](const Release::pointer& release)
            {
                directoryNode.addArrayChild("child", createAlbumNode(context, release, context.user, false /* no id3 */));
            });
        }
        else if (releaseId)
        {
            directoryNode.setAttribute("id", idToString(*releaseId));

            auto release{ Release::find(context.dbSession, *releaseId) };
            if (!release)
                throw RequestedDataNotFoundError{};

            directoryNode.setAttribute("name", utils::makeNameFilesystemCompatible(release->getName()));

            Track::find(context.dbSession, Track::FindParameters{}.setRelease(*releaseId).setSortMethod(TrackSortMethod::Release), [&](const Track::pointer& track)
            {
                directoryNode.addArrayChild("child", createSongNode(context, track, context.user));
            });
        }
        else
            throw BadParameterGenericError{ "id" };

        return response;
    }

    ClusterId BGetCluster(std::string value, std::string name, RequestContext& context)
    {
        auto clusterType{ ClusterType::find(context.dbSession, name) };
        if (!clusterType)
            throw RequestedDataNotFoundError{};
        auto cluster{clusterType->getCluster(value)};
        if (!cluster)
            throw RequestedDataNotFoundError{};
        return cluster->getId();
    }

    Response handleGetGenresRequest(RequestContext& context)
    {
        Response response{ Response::createOkResponse(context.serverProtocolVersion) };
        std::optional<std::string> year {getParameterAs<std::string>(context.parameters, "year")};
        std::optional<std::string> length {getParameterAs<std::string>(context.parameters, "length")};
        std::string key = "genre";

        if ((year.has_value() && year.value() != "-1") || (length.has_value() && !length.value().empty())) {
            if (year.has_value()) {
                key += year.value();
            }
            if (length.has_value()) {
                key += length.value();
            }
        }

        if (reqCacher->hasCache(key)) {
            return reqCacher->getCache(key, "genre", std::move(response));
        }
        Response::Node& genresNode{ response.createNode("genres") };

        auto transaction{ context.dbSession.createReadTransaction() };

        const ClusterType::pointer clusterType{ ClusterType::find(context.dbSession, "GENRE") };
        if (clusterType)
        {
            const auto clusters{ clusterType->getClusters() };
            for (const Cluster::pointer& cluster : clusters) {
                Track::FindParameters params;
                std::vector<ClusterId> searchClusters = {cluster->getId()};
                if ((year.has_value() && year.value() != "-1") || (length.has_value() && !length.value().empty())) {
                    if (year.has_value()){
                        searchClusters.push_back(BGetCluster(year.value(), "YEAR", context));
                    }
                    if (length.has_value()) {
                        searchClusters.push_back(BGetCluster(length.value(), "LENGTH", context));
                    }
                    params.setClusters(searchClusters);
                    auto  results = Track::count(context.dbSession, params);
                    if (results) {
                        genresNode.addArrayChild("genre", createGenreNode(cluster, results));
                        reqCacher->addToCache(key, cluster, results);
                    }
                }
                else
                {
                    genresNode.addArrayChild("genre", createGenreNode(cluster));
                    reqCacher->addToCache(key, cluster, cluster->getTrackCount());
                }
            }
        }

        return response;
    }

    Response handleGetMoodRequest(RequestContext& context)
    {
        Response response{ Response::createOkResponse(context.serverProtocolVersion) };
        std::optional<std::string> year {getParameterAs<std::string>(context.parameters, "year")};
        std::optional<std::string> length {getParameterAs<std::string>(context.parameters, "length")};
        std::string key = "mood";

        if ((year.has_value() && year.value() != "-1") || (length.has_value() && !length.value().empty())) {
            if (year.has_value()) {
                key += year.value();
            }
            if (length.has_value()) {
                key += length.value();
            }
        }

        if (reqCacher->hasCache(key)) {
            return reqCacher->getCache(key, "mood", std::move(response));
        }

        Response::Node& moodNode{ response.createNode("mood") };

        auto transaction{ context.dbSession.createReadTransaction() };

        const ClusterType::pointer clusterType{ ClusterType::find(context.dbSession, "MOOD") };
        if (clusterType)
        {
            const auto clusters{ clusterType->getClusters() };
            for (const Cluster::pointer& cluster : clusters) {
                Track::FindParameters params;
                std::vector<ClusterId> searchClusters = {cluster->getId()};
                if ((year.has_value() && year.value() != "-1") || (length.has_value() && !length.value().empty())) {
                    if (year.has_value()){
                        searchClusters.push_back(BGetCluster(year.value(), "YEAR", context));
                    }
                    if (length.has_value()) {
                        searchClusters.push_back(BGetCluster(length.value(), "LENGTH", context));
                    }
                    params.setClusters(searchClusters);
                    auto  results = Track::count(context.dbSession, params);
                    if (results) {
                        moodNode.addArrayChild("mood", createGenreNode(cluster, results));
                        reqCacher->addToCache(key, cluster, results);
                    }
                }
                else
                {
                    moodNode.addArrayChild("mood", createGenreNode(cluster));
                    reqCacher->addToCache(key, cluster, cluster->getTrackCount());
                }
            }
        }

        return response;
    }

    Response handleGetYearsRequest(RequestContext& context)
    {
        Response response {Response::createOkResponse(context.serverProtocolVersion)};

        Response::Node& yearsNode {response.createNode("years")};

        auto transaction{ context.dbSession.createReadTransaction() };

//    const std::vector<int> years = Track::getAllYears(context.dbSession)};
        std::vector<int> years = {
                1992,
                1993,
                1994,
                1995,
                1996,
                1997,
                1998,
                1999,
                2000,
                2001,
                2002,
                2003,
                2004,
                2005,
                2006,
                2007,
                2008,
                2009,
                2010,
                2011,
                2012,
                2013,
                2014,
                2015,
                2016,
                2017,
                2018,
                2019,
                2020,
                2021,
                2022,
                2023,
                2024
        };

        for (const int year : years)
        {
            Response::Node yearNode;

            yearNode.setValue(year);
            yearsNode.addArrayChild("year", Response::Node(yearNode));
        }
        return response;
    }

    Response handleGetArtistsRequest(RequestContext& context)
    {
        return handleGetArtistsRequestCommon(context, true /* id3 */);
    }

    Response handleGetArtistRequest(RequestContext& context)
    {
        // Mandatory params
        ArtistId id{ getMandatoryParameterAs<ArtistId>(context.parameters, "id") };

        auto transaction{ context.dbSession.createReadTransaction() };

        const Artist::pointer artist{ Artist::find(context.dbSession, id) };
        if (!artist)
            throw RequestedDataNotFoundError{};

        Response response{ Response::createOkResponse(context.serverProtocolVersion) };
        Response::Node artistNode{ createArtistNode(context, artist, context.user, true /* id3 */) };

        const auto releases{ Release::find(context.dbSession, Release::FindParameters {}.setArtist(artist->getId())) };
        for (const Release::pointer& release : releases.results)
            artistNode.addArrayChild("album", createAlbumNode(context, release, context.user, true /* id3 */));

        response.addNode("artist", std::move(artistNode));

        return response;
    }

    Response handleGetAlbumRequest(RequestContext& context)
    {
        // Mandatory params
        ReleaseId id{ getMandatoryParameterAs<ReleaseId>(context.parameters, "id") };

        auto transaction{ context.dbSession.createReadTransaction() };

        Release::pointer release{ Release::find(context.dbSession, id) };
        if (!release)
            throw RequestedDataNotFoundError{};

        Response response{ Response::createOkResponse(context.serverProtocolVersion) };
        Response::Node albumNode{ createAlbumNode(context, release, context.user, true /* id3 */) };

        const auto tracks{ Track::find(context.dbSession, Track::FindParameters{}.setRelease(id).setSortMethod(TrackSortMethod::Release)) };
        for (const Track::pointer& track : tracks.results)
            albumNode.addArrayChild("song", createSongNode(context, track, context.user));

        response.addNode("album", std::move(albumNode));

        return response;
    }

    Response handleGetSongRequest(RequestContext& context)
    {
        // Mandatory params
        TrackId id{ getMandatoryParameterAs<TrackId>(context.parameters, "id") };

        auto transaction{ context.dbSession.createReadTransaction() };

        const Track::pointer track{ Track::find(context.dbSession, id) };
        if (!track)
            throw RequestedDataNotFoundError{};

        Response response{ Response::createOkResponse(context.serverProtocolVersion) };
        response.addNode("song", createSongNode(context, track, context.user));

        return response;
    }

    Response handleGetArtistInfoRequest(RequestContext& context)
    {
        return handleGetArtistInfoRequestCommon(context, false /* no id3 */);
    }

    Response handleGetArtistInfo2Request(RequestContext& context)
    {
        return handleGetArtistInfoRequestCommon(context, true /* id3 */);
    }

    Response handleGetSimilarSongsRequest(RequestContext& context)
    {
        return handleGetSimilarSongsRequestCommon(context, false /* no id3 */);
    }

    Response handleGetSimilarSongs2Request(RequestContext& context)
    {
        return handleGetSimilarSongsRequestCommon(context, true /* id3 */);
    }

    Response handleGetTopSongs(RequestContext& context)
    {
        // Mandatory params
        std::string_view artistName{ getMandatoryParameterAs<std::string_view>(context.parameters, "artist") };
        std::size_t count{ getParameterAs<std::size_t>(context.parameters, "count").value_or(50) };
        if (count > defaultMaxCountSize)
            throw ParameterValueTooHighGenericError{ "count", defaultMaxCountSize };

        auto transaction{ context.dbSession.createReadTransaction() };

        const auto artists{ Artist::find(context.dbSession, artistName) };
        if (artists.size() != 1)
            throw RequestedDataNotFoundError{};

        Response response{ Response::createOkResponse(context.serverProtocolVersion) };
        Response::Node& topSongs{ response.createNode("topSongs") };

        scrobbling::IScrobblingService::FindParameters params;
        params.setUser(context.user->getId());
        params.setRange(db::Range{ 0, count });
        params.setArtist(artists.front()->getId());

        const auto trackIds{ core::Service<scrobbling::IScrobblingService>::get()->getTopTracks(params) };
        for (const TrackId trackId : trackIds.results)
        {
            if (Track::pointer track{ Track::find(context.dbSession, trackId) })
                topSongs.addArrayChild("song", createSongNode(context, track, context.user));
        }

        return response;
    }
}
