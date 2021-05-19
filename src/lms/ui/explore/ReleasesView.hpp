/*
 * Copyright (C) 2018 Emeric Poupon
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

#include <unordered_map>

#include <Wt/WContainerWidget.h>
#include <Wt/WTemplate.h>

#include "database/Types.hpp"
#include "PlayQueueAction.hpp"
#include "ReleaseCollector.hpp"

namespace UserInterface {

	class Filters;
	class InfiniteScrollingContainer;

	class Releases : public Wt::WTemplate
	{
		public:
			Releases(Filters& filters);

			PlayQueueActionSignal releasesAction;

		private:

			void refreshView();
			void refreshView(ReleaseCollector::Mode mode);

			void addSome();
			std::vector<Database::IdType> getAllReleases();

			static constexpr std::size_t _maxItemsPerLine {6};
			static constexpr std::size_t _batchSize {_maxItemsPerLine * 3};
			static inline const std::unordered_map<ReleaseCollector::Mode, std::size_t> _maxItemsPerMode
			{
				{ReleaseCollector::Mode::All, _batchSize * 30},
				{ReleaseCollector::Mode::MostPlayed, _batchSize * 10},
				{ReleaseCollector::Mode::Random, _batchSize * 10},
				{ReleaseCollector::Mode::RecentlyAdded, _batchSize * 10},
				{ReleaseCollector::Mode::RecentlyPlayed, _batchSize * 10},
				{ReleaseCollector::Mode::Starred, _batchSize * 30},
			};

			InfiniteScrollingContainer* _container {};
			ReleaseCollector			_releaseCollector;
			static constexpr ReleaseCollector::Mode _defaultMode {ReleaseCollector::Mode::Random};
	};

} // namespace UserInterface

