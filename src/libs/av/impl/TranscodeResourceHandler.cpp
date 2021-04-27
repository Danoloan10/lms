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

#include "TranscodeResourceHandler.hpp"

namespace Av
{

	std::unique_ptr<IResourceHandler>
	createTranscodeResourceHandler(const std::filesystem::path& trackPath, const TranscodeParameters& parameters)
	{
		return std::make_unique<TranscodeResourceHandler>(trackPath, parameters);
	}

	// TODO set some nice HTTP return code

	TranscodeResourceHandler::TranscodeResourceHandler(const std::filesystem::path& trackPath, const TranscodeParameters& parameters)
		: _transcoder {trackPath, parameters}
	{
	}

	Wt::Http::ResponseContinuation*
	TranscodeResourceHandler::processRequest(const Wt::Http::Request& /*request*/, Wt::Http::Response& response)
	{
		response.setMimeType(_transcoder.getOutputMimeType());

		if (_nbBytesReady > 0)
		{
			response.out().write(reinterpret_cast<const char *>(&_buffer[0]), _nbBytesReady);
			_nbBytesReady = 0;
		}

		if (!_transcoder.finished())
		{
			Wt::Http::ResponseContinuation *continuation {response.createContinuation()};
			continuation->waitForMoreData();
			_transcoder.asyncRead(_buffer.data(), _buffer.size(), [=](std::size_t nbBytesRead)
			{
				assert(_nbBytesReady == 0);
				_nbBytesReady = nbBytesRead;
				continuation->haveMoreData();
			});

			return continuation;
		}

		return {};
	}
}

