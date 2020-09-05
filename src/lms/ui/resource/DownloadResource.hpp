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

#pragma once

#include <memory>
#include <Wt/WResource.h>

#include "database/Types.hpp"
#include "utils/Zipper.hpp"

namespace UserInterface {

class DownloadResource : public Wt::WResource
{
	public:
		static constexpr std::size_t bufferSize {32768};

		~DownloadResource();

	private:

		void handleRequest(const Wt::Http::Request& request, Wt::Http::Response& response) override;
		virtual std::unique_ptr<Zip::Zipper> createZipper() = 0;
};

class DownloadArtistResource : public DownloadResource
{
	public:
		DownloadArtistResource(Database::IdType artistId);

	private:
		std::unique_ptr<Zip::Zipper> createZipper() override;
		Database::IdType _artistId;
};

class DownloadReleaseResource : public DownloadResource
{
	public:
		DownloadReleaseResource(Database::IdType releaseId);

	private:
		std::unique_ptr<Zip::Zipper> createZipper() override;
		Database::IdType _releaseId;
};

class DownloadTrackResource : public DownloadResource
{
	public:
		DownloadTrackResource(Database::IdType trackId);

	private:
		std::unique_ptr<Zip::Zipper> createZipper() override;
		Database::IdType _trackId;
};

} // namespace UserInterface

