/*
 * Copyright (C) 2022 Emeric Poupon
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

#include "services/database/TrackListId.hpp"
#include "services/database/Types.hpp"

#include "common/Template.hpp"
#include "PlayQueueAction.hpp"

namespace UserInterface
{
	class Filters;
	class InfiniteScrollingContainer;

	class TrackList : public Template
	{
		public:
			TrackList(Filters& filters);

			PlayQueueActionTrackListSignal trackListAction;
			PlayQueueActionTrackSignal tracksAction;

			Wt::Signal<Database::TrackListId> trackListDeleted;

		private:
			void refreshView();
			void addSome();

			static constexpr std::size_t _batchSize {6};
			static constexpr std::size_t _maxCount {8000};

			Filters&					_filters;
			Database::TrackListId		_trackListId;
			InfiniteScrollingContainer* _container {};
	};
} // namespace UserInterface
