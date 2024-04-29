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

#include "AlbumSongLists.hpp"

#include "database/Artist.hpp"
#include "database/Cluster.hpp"
#include "database/Release.hpp"
#include "database/Session.hpp"
#include "database/Track.hpp"
#include "database/User.hpp"
#include "services/feedback/IFeedbackService.hpp"
#include "services/scrobbling/IScrobblingService.hpp"
#include "responses/Album.hpp"
#include "responses/Artist.hpp"
#include "responses/Song.hpp"
#include "core/Service.hpp"
#include "ParameterParsing.hpp"
#include "SubsonicId.hpp"

namespace lms::api::subsonic
{
    using namespace db;

    namespace
    {
        Response handleGetAlbumListRequestCommon(RequestContext& context, bool id3)
        {
            // Mandatory params
            const std::string type{ getMandatoryParameterAs<std::string>(context.parameters, "type") };

            // Optional params
            const MediaLibraryId mediaLibraryId{ getParameterAs<MediaLibraryId>(context.parameters, "musicFolderId").value_or(MediaLibraryId{}) };
            const std::size_t size{ getParameterAs<std::size_t>(context.parameters, "size").value_or(10) };
            const std::size_t offset{ getParameterAs<std::size_t>(context.parameters, "offset").value_or(0) };
            if (size > defaultMaxCountSize)
                throw ParameterValueTooHighGenericError{ "size", defaultMaxCountSize };

            const Range range{ offset, size };

            RangeResults<ReleaseId> releases;
            scrobbling::IScrobblingService& scrobblingService{ *core::Service<scrobbling::IScrobblingService>::get() };
            feedback::IFeedbackService& feedbackService{ *core::Service<feedback::IFeedbackService>::get() };

            auto transaction{ context.dbSession.createReadTransaction() };

            if (type == "alphabeticalByName")
            {
                Release::FindParameters params;
                params.setSortMethod(ReleaseSortMethod::Name);
                params.setRange(range);
                params.setMediaLibrary(mediaLibraryId);

                releases = Release::findIds(context.dbSession, params);
            }
            else if (type == "alphabeticalByArtist")
            {
                Release::FindParameters params;
                params.setSortMethod(ReleaseSortMethod::ArtistNameThenName);
                params.setRange(range);
                params.setMediaLibrary(mediaLibraryId);

                releases = Release::findIds(context.dbSession, params);
            }
            else if (type == "byGenre")
            {
                // Mandatory param
                const std::string genre{ getMandatoryParameterAs<std::string>(context.parameters, "genre") };

                if (const ClusterType::pointer clusterType{ ClusterType::find(context.dbSession, "GENRE") })
                {
                    if (const Cluster::pointer cluster{ clusterType->getCluster(genre) })
                    {
                        Release::FindParameters params;
                        params.setClusters(std::initializer_list<ClusterId>{ cluster->getId() });
                        params.setSortMethod(ReleaseSortMethod::Name);
                        params.setRange(range);
                        params.setMediaLibrary(mediaLibraryId);

                        releases = Release::findIds(context.dbSession, params);
                    }
                }
            }
            else if (type == "byYear")
            {
                const int fromYear{ getMandatoryParameterAs<int>(context.parameters, "fromYear") };
                const int toYear{ getMandatoryParameterAs<int>(context.parameters, "toYear") };

                Release::FindParameters params;
                params.setSortMethod(ReleaseSortMethod::Date);
                params.setRange(range);
                params.setDateRange(DateRange::fromYearRange(fromYear, toYear));
                params.setMediaLibrary(mediaLibraryId);

                releases = Release::findIds(context.dbSession, params);
            }
            else if (type == "frequent")
            {
                scrobbling::IScrobblingService::FindParameters params;
                params.setUser(context.user->getId());
                params.setRange(range);
                params.setMediaLibrary(mediaLibraryId);

                releases = scrobblingService.getTopReleases(params);
            }
            else if (type == "newest")
            {
                Release::FindParameters params;
                params.setSortMethod(ReleaseSortMethod::LastWritten);
                params.setRange(range);
                params.setMediaLibrary(mediaLibraryId);

                releases = Release::findIds(context.dbSession, params);
            }
            else if (type == "random")
            {
                // Random results are paginated, but there is no acceptable way to handle the pagination params without repeating some albums
                // (no seed provided by subsonic, ot it would require to store some kind of context for each user/client when iterating over the random albums)
                Release::FindParameters params;
                params.setSortMethod(ReleaseSortMethod::Random);
                params.setRange(Range{ 0, size });
                params.setMediaLibrary(mediaLibraryId);

                releases = Release::findIds(context.dbSession, params);
            }
            else if (type == "recent")
            {
                scrobbling::IScrobblingService::FindParameters params;
                params.setUser(context.user->getId());
                params.setRange(range);
                params.setMediaLibrary(mediaLibraryId);

                releases = scrobblingService.getRecentReleases(params);
            }
            else if (type == "starred")
            {
                feedback::IFeedbackService::FindParameters params;
                params.setUser(context.user->getId());
                params.setRange(range);
                params.setMediaLibrary(mediaLibraryId);

                releases = feedbackService.findStarredReleases(params);
            }
            else
            {
                throw NotImplementedGenericError{};
            }

            Response response{ Response::createOkResponse(context.serverProtocolVersion) };
            Response::Node& albumListNode{ response.createNode(id3 ? Response::Node::Key{ "albumList2" } : Response::Node::Key{ "albumList" }) };

            for (const ReleaseId releaseId : releases.results)
            {
                const Release::pointer release{ Release::find(context.dbSession, releaseId) };
                albumListNode.addArrayChild("album", createAlbumNode(context, release, context.user, id3));
            }

            return response;
        }

        Response handleGetStarredRequestCommon(RequestContext& context, bool id3)
        {
            // Optional parameters
            const MediaLibraryId mediaLibrary{ getParameterAs<MediaLibraryId>(context.parameters, "musicFolderId").value_or(MediaLibraryId{}) };

            auto transaction{ context.dbSession.createReadTransaction() };

            Response response{ Response::createOkResponse(context.serverProtocolVersion) };
            Response::Node& starredNode{ response.createNode(id3 ? Response::Node::Key{ "starred2" } : Response::Node::Key{ "starred" }) };

            feedback::IFeedbackService& feedbackService{ *core::Service<feedback::IFeedbackService>::get() };

            {
                feedback::IFeedbackService::ArtistFindParameters artistFindParams;
                artistFindParams.setUser(context.user->getId());
                artistFindParams.setSortMethod(ArtistSortMethod::SortName);
                for (const ArtistId artistId : feedbackService.findStarredArtists(artistFindParams).results)
                {
                    if (auto artist{ Artist::find(context.dbSession, artistId) })
                        starredNode.addArrayChild("artist", createArtistNode(context, artist, context.user, id3));
                }
            }

            feedback::IFeedbackService::FindParameters findParameters;
            findParameters.setUser(context.user->getId());
            findParameters.setMediaLibrary(mediaLibrary);

            for (const ReleaseId releaseId : feedbackService.findStarredReleases(findParameters).results)
            {
                if (auto release{ Release::find(context.dbSession, releaseId) })
                    starredNode.addArrayChild("album", createAlbumNode(context, release, context.user, id3));
            }

            for (const TrackId trackId : feedbackService.findStarredTracks(findParameters).results)
            {
                if (auto track{ Track::find(context.dbSession, trackId) })
                    starredNode.addArrayChild("song", createSongNode(context, track, context.user));
            }

            return response;
        }
    } // namespace

    Response handleGetAlbumListRequest(RequestContext& context)
    {
        return handleGetAlbumListRequestCommon(context, false /* no id3 */);
    }

    Response handleGetAlbumList2Request(RequestContext& context)
    {
        return handleGetAlbumListRequestCommon(context, true /* id3 */);
    }

    Response handleGetRandomSongsRequest(RequestContext& context)
    {
        // Optional params
        const MediaLibraryId mediaLibraryId{ getParameterAs<MediaLibraryId>(context.parameters, "musicFolderId").value_or(MediaLibraryId{}) };
        std::size_t size{ getParameterAs<std::size_t>(context.parameters, "size").value_or(50) };
        if (size > defaultMaxCountSize)
            throw ParameterValueTooHighGenericError{ "size", defaultMaxCountSize };

        Response response{ Response::createOkResponse(context.serverProtocolVersion) };
        Response::Node& randomSongsNode{ response.createNode("randomSongs") };

        auto transaction{ context.dbSession.createReadTransaction() };

        Track::FindParameters params;
        params.setSortMethod(TrackSortMethod::Random);
        params.setRange(Range{ 0, size });
        params.setMediaLibrary(mediaLibraryId);

        Track::find(context.dbSession, params, [&](const Track::pointer& track)
            {
                randomSongsNode.addArrayChild("song", createSongNode(context, track, context.user));
            });

        return response;
    }

  ClusterId GetCluster(std::string value, std::string name, RequestContext& context)
    {
        auto clusterType{ ClusterType::find(context.dbSession, name) };
        if (!clusterType)
          throw RequestedDataNotFoundError{};
        auto cluster{clusterType->getCluster(value)};
        if (!cluster)
          throw RequestedDataNotFoundError{};
        return cluster->getId();
    }

    Response handleGetSongsByGenreRequest(RequestContext& context)
    {
        // Mandatory params
        std::string genre{ getMandatoryParameterAs<std::string>(context.parameters, "genre") };

        // Optional params
        std::optional<std::string> year {getParameterAs<std::string>(context.parameters, "year")};
        std::optional<std::string> length {getParameterAs<std::string>(context.parameters, "length")};
        const MediaLibraryId mediaLibrary{ getParameterAs<MediaLibraryId>(context.parameters, "musicFolderId").value_or(MediaLibraryId{}) };
        std::size_t count{ getParameterAs<std::size_t>(context.parameters, "count").value_or(10) };
        std::size_t ratingMin{ getParameterAs<std::size_t>(context.parameters, "ratingMin").value_or(0) };
        std::size_t ratingMax{ getParameterAs<std::size_t>(context.parameters, "ratingMax").value_or(5) };
        if (count > defaultMaxCountSize)
            throw ParameterValueTooHighGenericError{"count", defaultMaxCountSize};

        std::size_t offset{ getParameterAs<std::size_t>(context.parameters, "offset").value_or(0) };

        auto transaction{ context.dbSession.createReadTransaction() };

        auto clusterType{ ClusterType::find(context.dbSession, "GENRE") };
        if (!clusterType)
            throw RequestedDataNotFoundError{};

        auto cluster{ clusterType->getCluster(genre) };
        if (!cluster)
            throw RequestedDataNotFoundError{};

        Response response{ Response::createOkResponse(context.serverProtocolVersion) };
        Response::Node& songsByGenreNode{ response.createNode("songsByGenre") };

        Track::FindParameters params;
//        params.setClusters(std::initializer_list<ClusterId>{ cluster->getId() });
        std::vector<ClusterId> clusters =  {cluster->getId()};
        if (year.has_value())
        {
          clusters.push_back(GetCluster(year.value(), "YEAR", context));
        }
        if (length.has_value())
        {
          clusters.push_back(GetCluster(length.value(), "LENGTH", context));
        }
        params.setClusters(clusters);
        params.setRange(Range{ offset, count });
        params.setMediaLibrary(mediaLibrary);


        Track::find(context.dbSession, params, [&](const Track::pointer& track)
            {
                if(
                    track->getRating().value_or(0) >= ratingMin &&
                    track->getRating().value_or(0)  <= ratingMax)
                songsByGenreNode.addArrayChild("song", createSongNode(context, track, context.user));
            });

        return response;
    }

    Response handleGetSongsByYearRequest(RequestContext& context)
    {
        // Mandatory params
        std::string year{ getMandatoryParameterAs<std::string>(context.parameters, "year") };

        // Optional params
        const MediaLibraryId mediaLibrary{ getParameterAs<MediaLibraryId>(context.parameters, "musicFolderId").value_or(MediaLibraryId{}) };
        std::size_t count{ getParameterAs<std::size_t>(context.parameters, "count").value_or(10) };
        if (count > defaultMaxCountSize)
            throw ParameterValueTooHighGenericError{"count", defaultMaxCountSize};

        std::size_t offset{ getParameterAs<std::size_t>(context.parameters, "offset").value_or(0) };

        auto transaction{ context.dbSession.createReadTransaction() };

        auto clusterType{ ClusterType::find(context.dbSession, "YEAR") };
        if (!clusterType)
            throw RequestedDataNotFoundError{};

        auto cluster{ clusterType->getCluster(year) };
        if (!cluster)
            throw RequestedDataNotFoundError{};

        User::pointer user{ User::find(context.dbSession, context.userId) };
        if (!user)
            throw UserNotAuthorizedError{};

        Response response{ Response::createOkResponse(context.serverProtocolVersion) };
        Response::Node& songsByYearNode{ response.createNode("songsByYear") };

        Track::FindParameters params;
        params.setClusters({ cluster->getId() });
        params.setRange(Range{ offset, count });
        params.setMediaLibrary(mediaLibrary);

        Track::find(context.dbSession, params, [&](const Track::pointer& track)
        {
            songsByYearNode.addArrayChild("song", createSongNode(context, track, user));
        });

        return response;
    }

    Response handleGetSongsByMoodRequest(RequestContext& context)
    {
        // Mandatory params
        std::string Mood{ getMandatoryParameterAs<std::string>(context.parameters, "mood") };

        // Optional params
        std::optional<std::string> year {getParameterAs<std::string>(context.parameters, "year")};
        std::optional<std::string> length {getParameterAs<std::string>(context.parameters, "length")};
        std::size_t count{ getParameterAs<std::size_t>(context.parameters, "count").value_or(10) };
        std::size_t ratingMin{ getParameterAs<std::size_t>(context.parameters, "ratingMin").value_or(0) };
        std::size_t ratingMax{ getParameterAs<std::size_t>(context.parameters, "ratingMax").value_or(5) };
        if (count > defaultMaxCountSize)
            throw ParameterValueTooHighGenericError{"count", defaultMaxCountSize};

        std::size_t offset{ getParameterAs<std::size_t>(context.parameters, "offset").value_or(0) };

        auto transaction{ context.dbSession.createReadTransaction() };

        auto clusterType{ ClusterType::find(context.dbSession, "MOOD") };
        if (!clusterType)
            throw RequestedDataNotFoundError{};

        auto cluster{ clusterType->getCluster(Mood) };
        if (!cluster)
            throw RequestedDataNotFoundError{};

        User::pointer user{ User::find(context.dbSession, context.userId) };
        if (!user)
            throw UserNotAuthorizedError{};

        Response response{ Response::createOkResponse(context.serverProtocolVersion) };
        Response::Node& songsByMoodNode{ response.createNode("songsByMood") };

        Track::FindParameters params;
        std::vector<ClusterId> clusters =  {cluster->getId()};
        if (year.has_value())
        {
          clusters.push_back(GetCluster(year.value(), "YEAR", context));
        }
        if (length.has_value())
        {
          clusters.push_back(GetCluster(length.value(), "LENGTH", context));
        }
        params.setClusters(clusters);
        params.setRange(Range{ offset, count });

        Track::find(context.dbSession, params, [&](const Track::pointer& track)
        {
            if (track->getRating().value_or(0) >= ratingMin &&
                track->getRating().value_or(0)  <= ratingMax)
                songsByMoodNode.addArrayChild("song", createSongNode(context, track, user));
        });

        return response;
    }

    Response handleGetStarredRequest(RequestContext& context)
    {
        return handleGetStarredRequestCommon(context, false /* no id3 */);
    }

    Response handleGetStarred2Request(RequestContext& context)
    {
        return handleGetStarredRequestCommon(context, true /* id3 */);
    }

}
