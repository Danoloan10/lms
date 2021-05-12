/*
 * Copyright (C) 2021 Emeric Poupon
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

#include "DatabaseCollectorBase.hpp"

namespace UserInterface
{
	DatabaseCollectorBase::DatabaseCollectorBase(Filters& filters, Mode defaultMode)
		: _filters {filters}
		, _mode {defaultMode}
	{
	}

	std::optional<DatabaseCollectorBase::Range>
	DatabaseCollectorBase::getActualRange(std::optional<Range> range) const
	{
		if (std::optional<std::size_t> maxCount {getMaxCount()})
		{
			if (range)
				range->limit = std::min(*maxCount - range->offset, range->limit);
			else
				range = Range {0, *maxCount};
		}

		return range;
	}

	std::optional<std::size_t>
	DatabaseCollectorBase::getMaxCount() const
	{
		std::optional<std::size_t> res;
		if (auto itMaxCount {_maxItemCountPerMode.find(_mode)}; itMaxCount != std::cend(_maxItemCountPerMode))
		{
			res = itMaxCount->second;
		}

		return res;
	}

} // ns UserInterface

