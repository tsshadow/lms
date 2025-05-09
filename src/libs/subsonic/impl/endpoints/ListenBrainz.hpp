#ifndef LMS_LISTENBRAINZ_HPP
#define LMS_LISTENBRAINZ_HPP

#include "RequestContext.hpp"
#include "SubsonicResponse.hpp"

namespace lms::api::subsonic
{
    using namespace db;
    Response handleImportListenBrainzRequest(RequestContext& context);
} // namespace lms::api::subsonic

#endif // LMS_LISTENBRAINZ_HPP
