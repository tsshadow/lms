#include "Browsing.hpp"

#include "core/ILogger.hpp"
#include "core/Random.hpp"
#include "database/Artist.hpp"
#include "database/ArtistInfo.hpp"
#include "database/Session.hpp"
#include "database/Track.hpp"
#include "database/User.hpp"

#include "ParameterParsing.hpp"

namespace lms::api::subsonic
{
    using namespace db;

    Response handleImportListenBrainzRequest(RequestContext& context)
    {
        std::string username = getMandatoryParameterAs<std::string>(context.parameters, "username");

        auto transaction = context.dbSession.createWriteTransaction();

        try
        {
            Track::importFromListenBrainz(context.dbSession, context.user->getId(), username);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("Failed to import from ListenBrainz: " + std::string(e.what()));
        }

        return Response::createOkResponse(context.serverProtocolVersion);
    }
} // namespace lms::api::subsonic
