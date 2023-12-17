/*
 * Copyright (C) 2020 Emeric Poupon
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

#include "Migration.hpp"

#include <Wt/Dbo/WtSqlTraits.h>

#include "database/Db.hpp"
#include "database/ScanSettings.hpp"
#include "database/Session.hpp"
#include "database/User.hpp"
#include "utils/Exception.hpp"
#include "utils/ILogger.hpp"

namespace Database
{
    VersionInfo::pointer VersionInfo::getOrCreate(Session& session)
    {
        session.checkWriteTransaction();

        pointer versionInfo{ session.getDboSession().find<VersionInfo>() };
        if (!versionInfo)
            return session.getDboSession().add(std::make_unique<VersionInfo>());

        return versionInfo;
    }

    VersionInfo::pointer VersionInfo::get(Session& session)
    {
        session.checkReadTransaction();

        return session.getDboSession().find<VersionInfo>();
    }
}

namespace Database::Migration
{
    class ScopedNoForeignKeys
    {
    public:
        ScopedNoForeignKeys(Db& db) : _db{ db }
        {
            _db.executeSql("PRAGMA foreign_keys=OFF");
        }
        ~ScopedNoForeignKeys()
        {
            _db.executeSql("PRAGMA foreign_keys=ON");
        }

        ScopedNoForeignKeys(const ScopedNoForeignKeys&) = delete;
        ScopedNoForeignKeys(ScopedNoForeignKeys&&) = delete;
        ScopedNoForeignKeys& operator=(const ScopedNoForeignKeys&) = delete;
        ScopedNoForeignKeys& operator=(ScopedNoForeignKeys&&) = delete;

    private:
        Db& _db;
    };

    static void migrateFromV32(Session& session)
    {
        ScanSettings::get(session).modify()->addAudioFileExtension(".wv");
    }

    static void migrateFromV33(Session& session)
    {
        // remove name from track_artist_link
        // Drop Auth mode
        session.getDboSession().execute(R"(
CREATE TABLE IF NOT EXISTS "track_artist_link_backup" (
  "id" integer primary key autoincrement,
  "version" integer not null,
  "type" integer not null,
  "track_id" bigint,
  "artist_id" bigint,
  constraint "fk_track_artist_link_track" foreign key ("track_id") references "track" ("id") on delete cascade deferrable initially deferred,
  constraint "fk_track_artist_link_artist" foreign key ("artist_id") references "artist" ("id") on delete cascade deferrable initially deferred
);
))");
        session.getDboSession().execute("INSERT INTO track_artist_link_backup SELECT id, version, type, track_id, artist_id FROM track_artist_link");
        session.getDboSession().execute("DROP TABLE track_artist_link");
        session.getDboSession().execute("ALTER TABLE track_artist_link_backup RENAME TO track_artist_link");
    }

    static void migrateFromV34(Session& session)
    {
        // Add scrobbling state
        // By default, everything needs to be sent
        session.getDboSession().execute("ALTER TABLE starred_artist ADD scrobbling_state INTEGER NOT NULL DEFAULT(" + std::to_string(static_cast<int>(/*ScrobblingState::PendingAdd*/0)) + ")");
        session.getDboSession().execute("ALTER TABLE starred_release ADD scrobbling_state INTEGER NOT NULL DEFAULT(" + std::to_string(static_cast<int>(/*ScrobblingState::PendingAdd*/0)) + ")");
        session.getDboSession().execute("ALTER TABLE starred_track ADD scrobbling_state INTEGER NOT NULL DEFAULT(" + std::to_string(static_cast<int>(/*ScrobblingState::PendingAdd*/0)) + ")");
    }

    static void migrateFromV35(Session& session)
    {
        // Add creattion/last modif date time for tracklists
        session.getDboSession().execute("ALTER TABLE tracklist ADD creation_date_time TEXT");
        session.getDboSession().execute("ALTER TABLE tracklist ADD last_modified_date_time TEXT");
    }

    static void migrateFromV36(Session& session)
    {
        // Increased precision for track durations (now in milliseconds instead of secodns)
        // Just increment the scan version of the settings to make the next scheduled scan rescan everything
        ScanSettings::get(session).modify()->incScanVersion();
    }

    static void migrateFromV37(Session& session)
    {
        // Support Performer tags (via subtypes)
        session.getDboSession().execute("ALTER TABLE track_artist_link ADD subtype TEXT");

        // Just increment the scan version of the settings to make the next scheduled scan rescan everything
        ScanSettings::get(session).modify()->incScanVersion();
    }

    static void migrateFromV38(Session& session)
    {
        // migrate release-specific tags from Track to Release
        session.getDboSession().execute("ALTER TABLE release ADD total_disc INTEGER");

        session.getDboSession().execute(R"(
CREATE TABLE IF NOT EXISTS "track_backup" (
  "id" integer primary key autoincrement,
  "version" integer not null,
  "scan_version" integer not null,
  "rating" integer,
  "track_number" integer,
  "disc_number" integer,
  "total_track" integer,
  "disc_subtitle" text not null,
  "name" text not null,
  "duration" integer,
  "date" text,
  "original_date" text,
  "file_path" text not null,
  "file_last_write" text,
  "file_added" text,
  "has_cover" boolean not null,
  "mbid" text not null,
  "recording_mbid" text not null,
  "copyright" text not null,
  "copyright_url" text not null,
  "track_replay_gain" real,
  "release_replay_gain" real,
  "release_id" bigint,
  constraint "fk_track_release" foreign key ("release_id") references "release" ("id") on delete cascade deferrable initially deferred
);
))");
        session.getDboSession().execute("INSERT INTO track_backup SELECT id, version, scan_version,  track_number, disc_number, total_track, disc_subtitle, name, duration, date, original_date, file_path, file_last_write, file_added, has_cover, mbid, recording_mbid, copyright, copyright_url, track_replay_gain, release_replay_gain, release_id FROM track");
        session.getDboSession().execute("DROP TABLE track");
        session.getDboSession().execute("ALTER TABLE track_backup RENAME TO track");

        // Just increment the scan version of the settings to make the next scheduled scan rescan everything
        ScanSettings::get(session).modify()->incScanVersion();
    }

    static void migrateFromV39(Session& session)
    {
        // add release type
        session.getDboSession().execute("ALTER TABLE release ADD primary_type INTEGER");
        session.getDboSession().execute("ALTER TABLE release ADD secondary_types INTEGER");

        // Just increment the scan version of the settings to make the next scheduled scan rescan everything
        ScanSettings::get(session).modify()->incScanVersion();
    }

    static void migrateFromV40(Session& session)
    {
        // add artist_display_name in Release and Track
        session.getDboSession().execute("ALTER TABLE release ADD artist_display_name TEXT");
        session.getDboSession().execute("ALTER TABLE track ADD artist_display_name TEXT");

        // Just increment the scan version of the settings to make the next scheduled scan rescan everything
        ScanSettings::get(session).modify()->incScanVersion();
    }

    static void migrateFromV41(Session& session)
    {
        // add artist_display_name in Release and Track
        session.getDboSession().execute("ALTER TABLE user RENAME COLUMN subsonic_transcode_format TO subsonic_default_transcode_format");
        session.getDboSession().execute("ALTER TABLE user RENAME COLUMN subsonic_transcode_bitrate TO subsonic_default_transcode_bitrate");
        session.getDboSession().execute("ALTER TABLE user DROP COLUMN subsonic_transcode_enable");
    }

    static void migrateFromV42(Session& session)
    {
        session.getDboSession().execute("DROP INDEX IF EXISTS listen_scrobbler_idx");
        session.getDboSession().execute("DROP INDEX IF EXISTS listen_user_scrobbler_idx");
        session.getDboSession().execute("DROP INDEX IF EXISTS listen_user_track_scrobbler_date_time_idx");
        session.getDboSession().execute("DROP INDEX IF EXISTS starred_artist_user_scrobbler_idx");
        session.getDboSession().execute("DROP INDEX IF EXISTS starred_artist_artist_user_scrobbler_idx");
        session.getDboSession().execute("DROP INDEX IF EXISTS starred_release_user_scrobbler_idx");
        session.getDboSession().execute("DROP INDEX IF EXISTS starred_release_release_user_scrobbler_idx");
        session.getDboSession().execute("DROP INDEX IF EXISTS starred_track_user_scrobbler_idx");
        session.getDboSession().execute("DROP INDEX IF EXISTS starred_track_track_user_scrobbler_idx");

        // New feedback service that now handles the star/unstar stuff (that was previously handled by the scrobbling service)
        session.getDboSession().execute("ALTER TABLE user RENAME COLUMN scrobbler TO scrobbling_backend");
        session.getDboSession().execute("ALTER TABLE user ADD feedback_backend INTEGER");
        session.getDboSession().execute("ALTER TABLE listen RENAME COLUMN scrobbler TO backend");
        session.getDboSession().execute("ALTER TABLE listen RENAME COLUMN scrobbling_state TO sync_state");
        session.getDboSession().execute("ALTER TABLE starred_artist RENAME COLUMN scrobbler TO backend");
        session.getDboSession().execute("ALTER TABLE starred_artist RENAME COLUMN scrobbling_state TO sync_state");
        session.getDboSession().execute("ALTER TABLE starred_release RENAME COLUMN scrobbler TO backend");
        session.getDboSession().execute("ALTER TABLE starred_release RENAME COLUMN scrobbling_state TO sync_state");
        session.getDboSession().execute("ALTER TABLE starred_track RENAME COLUMN scrobbler TO backend");
        session.getDboSession().execute("ALTER TABLE starred_track RENAME COLUMN scrobbling_state TO sync_state");
        
        session.getDboSession().execute("UPDATE user SET feedback_backend = scrobbling_backend");
    }

    static void migrateFromV43(Session& session)
    {
        // add counts in genre table
        session.getDboSession().execute("ALTER TABLE cluster ADD track_count INTEGER");
        session.getDboSession().execute("ALTER TABLE cluster ADD release_count INTEGER");

        // Just increment the scan version of the settings to make the next scheduled scan rescan everything
        ScanSettings::get(session).modify()->incScanVersion();
    }

    static void migrateFromV44(Session& session)
    {
        // add bitrate
        session.getDboSession().execute("ALTER TABLE track ADD bitrate INTEGER");

        // Just increment the scan version of the settings to make the next scheduled scan rescan everything
        ScanSettings::get(session).modify()->incScanVersion();
    }

    void migrateFromV45(Session& session)
    {
        // add subsonic_enable_transcoding_by_default, default is disabled
        session.getDboSession().execute("ALTER TABLE user ADD subsonic_enable_transcoding_by_default INTEGER NOT NULL DEFAULT(" + std::to_string(static_cast<int>(/*User::defaultSubsonicEnableTranscodingByDefault*/0)) + ")");
    }

    void migrateFromV46(Session& session)
    {
        // add extra tags to parse
        session.getDboSession().execute(R"(CREATE TABLE IF NOT EXISTS "cluster_type_backup" (
  "id" integer primary key autoincrement,
  "version" integer not null,
  "name" text not null
);)");
        session.getDboSession().execute("INSERT INTO cluster_type_backup SELECT id, version, name FROM cluster_type");
        session.getDboSession().execute("DROP TABLE cluster_type");
        session.getDboSession().execute("ALTER TABLE cluster_type_backup RENAME TO cluster_type");

        session.getDboSession().execute("ALTER TABLE scan_settings ADD COLUMN extra_tags_to_scan TEXT");

        // Just increment the scan version of the settings to make the next scheduled scan rescan everything
        ScanSettings::get(session).modify()->incScanVersion();
    }

    void doDbMigration(Session& session)
    {
        static const std::string outdatedMsg{ "Outdated database, please rebuild it (delete the .db file and restart)" };

        ScopedNoForeignKeys noPragmaKeys{ session.getDb() };

        using MigrationFunction = std::function<void(Session&)>;

        const std::map<unsigned, MigrationFunction> migrationFunctions
        {
            {32, migrateFromV32},
            {33, migrateFromV33},
            {34, migrateFromV34},
            {35, migrateFromV35},
            {36, migrateFromV36},
            {37, migrateFromV37},
            {38, migrateFromV38},
            {39, migrateFromV39},
            {40, migrateFromV40},
            {41, migrateFromV41},
            {42, migrateFromV42},
            {43, migrateFromV43},
            {44, migrateFromV44},
            {45, migrateFromV45},
            {46, migrateFromV46},
        };

        {
            auto transaction{ session.createWriteTransaction() };

            Version version;
            try
            {
                version = VersionInfo::getOrCreate(session)->getVersion();
                LMS_LOG(DB, INFO, "Database version = " << version << ", LMS binary version = " << LMS_DATABASE_VERSION);
            }
            catch (std::exception& e)
            {
                LMS_LOG(DB, ERROR, "Cannot get database version info: " << e.what());
                throw LmsException{ outdatedMsg };
            }

            if (version > LMS_DATABASE_VERSION)
                throw LmsException{ "Server binary outdated, please upgrade it to handle this database" };

            if (version < migrationFunctions.begin()->first)
                throw LmsException{ outdatedMsg };

            while (version < LMS_DATABASE_VERSION)
            {
                LMS_LOG(DB, INFO, "Migrating database from version " << version << " to " << version + 1 << "...");

                auto itMigrationFunc{ migrationFunctions.find(version) };
                assert(itMigrationFunc != std::cend(migrationFunctions));
                itMigrationFunc->second(session);

                VersionInfo::get(session).modify()->setVersion(++version);

                LMS_LOG(DB, INFO, "Migration complete to version " << version);
            }
        }
    }
}
