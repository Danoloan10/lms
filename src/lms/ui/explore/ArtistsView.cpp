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

#include "ArtistsView.hpp"

#include <optional>

#include <Wt/WMenu.h>

#include "common/ValueStringModel.hpp"
#include "database/Artist.hpp"
#include "database/Session.hpp"
#include "database/User.hpp"
#include "database/TrackArtistLink.hpp"
#include "database/TrackList.hpp"
#include "scrobbling/IScrobbling.hpp"
#include "utils/Logger.hpp"

#include "common/LoadingIndicator.hpp"
#include "ArtistListHelpers.hpp"
#include "LmsApplication.hpp"
#include "Filters.hpp"

using namespace Database;

namespace UserInterface {

using ArtistLinkModel = ValueStringModel<std::optional<TrackArtistLinkType>>;

Artists::Artists(Filters* filters)
: Wt::WTemplate {Wt::WString::tr("Lms.Explore.Artists.template")},
  _filters {filters}
{
	addFunction("tr", &Wt::WTemplate::Functions::tr);

	{
		auto* menu {bindNew<Wt::WMenu>("mode")};

		auto addItem = [this](Wt::WMenu& menu,const Wt::WString& str, Mode mode)
		{
			auto* item {menu.addItem(str)};
			item->clicked().connect([this, mode] { refreshView(mode); });

			if (mode == defaultMode)
				item->renderSelected(true);
		};

		addItem(*menu, Wt::WString::tr("Lms.Explore.random"), Mode::Random);
		addItem(*menu, Wt::WString::tr("Lms.Explore.starred"), Mode::Starred);
		addItem(*menu, Wt::WString::tr("Lms.Explore.recently-played"), Mode::RecentlyPlayed);
		addItem(*menu, Wt::WString::tr("Lms.Explore.most-played"), Mode::MostPlayed);
		addItem(*menu, Wt::WString::tr("Lms.Explore.recently-added"), Mode::RecentlyAdded);
		addItem(*menu, Wt::WString::tr("Lms.Explore.all"), Mode::All);
	}

	_linkType = bindNew<Wt::WComboBox>("link-type");
	_linkType->setModel(std::make_shared<ArtistLinkModel>());
	_linkType->changed().connect([this] { refreshView(); });
	refreshArtistLinkTypes();

	LmsApp->getScannerEvents().scanComplete.connect(this, [this](const Scanner::ScanStats& stats)
	{
		if (stats.nbChanges())
			refreshArtistLinkTypes();
	});

	_container = bindNew<Wt::WContainerWidget>("artists");
	hideLoadingIndicator();

	refreshView();

	filters->updated().connect([this] { refreshView(); });
}

void
Artists::refreshView()
{
	_container->clear();
	_randomArtists.clear();
	addSome();
}

void
Artists::refreshView(Mode mode)
{
	_mode = mode;
	refreshView();
}

void
Artists::refreshArtistLinkTypes()
{
	std::shared_ptr<ArtistLinkModel> linkTypeModel {std::static_pointer_cast<ArtistLinkModel>(_linkType->model())};

	EnumSet<Database::TrackArtistLinkType> usedLinkTypes;
	{
		auto transaction {LmsApp->getDbSession().createSharedTransaction()};
		usedLinkTypes = Database::TrackArtistLink::getUsedTypes(LmsApp->getDbSession());
	}

	auto addTypeIfUsed {[&](Database::TrackArtistLinkType linkType, std::string_view stringKey)
	{
		if (!usedLinkTypes.contains(linkType))
			return;

		linkTypeModel->add(Wt::WString::tr(std::string {stringKey}), linkType);
	}};

	linkTypeModel->clear();

	linkTypeModel->add(Wt::WString::tr("Lms.Explore.Artists.linktype-all"), {});
	addTypeIfUsed(TrackArtistLinkType::Artist, "Lms.Explore.Artists.linktype-artist");
	addTypeIfUsed(TrackArtistLinkType::ReleaseArtist, "Lms.Explore.Artists.linktype-releaseartist");
	addTypeIfUsed(TrackArtistLinkType::Composer, "Lms.Explore.Artists.linktype-composer");
	addTypeIfUsed(TrackArtistLinkType::Lyricist, "Lms.Explore.Artists.linktype-lyricist");
	addTypeIfUsed(TrackArtistLinkType::Mixer, "Lms.Explore.Artists.linktype-mixer");
	addTypeIfUsed(TrackArtistLinkType::Producer, "Lms.Explore.Artists.linktype-producer");
	addTypeIfUsed(TrackArtistLinkType::Remixer, "Lms.Explore.Artists.linktype-remixer");
}

void
Artists::displayLoadingIndicator()
{
	_loadingIndicator = bindWidget<Wt::WTemplate>("loading-indicator", createLoadingIndicator());
	_loadingIndicator->scrollVisibilityChanged().connect([this](bool visible)
	{
		if (!visible)
			return;

		addSome();
	});
}

void
Artists::hideLoadingIndicator()
{
	_loadingIndicator = nullptr;
	bindEmpty("loading-indicator");
}

std::vector<Artist::pointer>
Artists::getRandomArtists(std::optional<Range> range, bool& moreResults)
{
	std::vector<Artist::pointer> artists;

	const std::optional<TrackArtistLinkType> linkType {static_cast<ArtistLinkModel*>(_linkType->model().get())->getValue(_linkType->currentIndex())};

	if (_randomArtists.empty())
		_randomArtists = Artist::getAllIdsRandom(LmsApp->getDbSession(), _filters->getClusterIds(), linkType, maxItemsPerMode[Mode::Random]);

	{
		auto itBegin {std::cbegin(_randomArtists) + std::min(range ? range->offset : 0, _randomArtists.size())};
		auto itEnd {std::cbegin(_randomArtists) + std::min(range ? range->offset + range->limit : _randomArtists.size(), _randomArtists.size())};

		for (auto it {itBegin}; it != itEnd; ++it)
		{
			Artist::pointer artist {Artist::getById(LmsApp->getDbSession(), *it)};
			if (artist)
				artists.push_back(artist);
		}

		moreResults = (itEnd != std::cend(_randomArtists));
	}

	return artists;
}

std::vector<Artist::pointer>
Artists::getArtists(std::optional<Range> range, bool& moreResults)
{
	std::vector<Artist::pointer> artists;

	const std::optional<TrackArtistLinkType> linkType {static_cast<ArtistLinkModel*>(_linkType->model().get())->getValue(_linkType->currentIndex())};

	const std::optional<std::size_t> modeLimit{maxItemsPerMode[_mode]};
	if (modeLimit)
	{
		if (range)
			range->limit = std::min(*modeLimit - range->offset, range->limit);
		else
			range = Range {0, *modeLimit};
	}

	switch (_mode)
	{
		case Mode::Random:
			artists = getRandomArtists(range, moreResults);
			break;

		case Mode::Starred:
			artists = Artist::getStarred(LmsApp->getDbSession(),
						LmsApp->getUser(),
						_filters->getClusterIds(),
						linkType,
						Artist::SortMethod::BySortName,
						range, moreResults);
			break;

		case Mode::RecentlyPlayed:
			artists = Service<Scrobbling::IScrobbling>::get()->getRecentArtists(LmsApp->getDbSession(), LmsApp->getUser(),
						_filters->getClusterIds(),
						linkType,
						range, moreResults);
			break;

		case Mode::MostPlayed:
			artists = Service<Scrobbling::IScrobbling>::get()->getTopArtists(LmsApp->getDbSession(), LmsApp->getUser(),
						_filters->getClusterIds(),
						linkType,
						range, moreResults);
			break;

		case Mode::RecentlyAdded:
			artists = Artist::getLastWritten(LmsApp->getDbSession(),
						std::nullopt,
						_filters->getClusterIds(),
						linkType,
						range, moreResults);
			break;

		case Mode::All:
			artists = Artist::getByFilter(LmsApp->getDbSession(),
						_filters->getClusterIds(),
						{},
						linkType,
						Artist::SortMethod::BySortName,
						range, moreResults);
			break;

		default:
			break;
	}

	if (range && modeLimit && (range->offset + range->limit == *modeLimit))
		moreResults = false;

	return artists;
}

void
Artists::addSome()
{
	auto transaction {LmsApp->getDbSession().createSharedTransaction()};

	bool moreResults {};
	for (const Artist::pointer& artist : getArtists(Range {static_cast<std::size_t>(_container->count()), batchSize}, moreResults))
	{
		_container->addWidget(ArtistListHelpers::createEntry(artist));
	}

	if (moreResults)
		displayLoadingIndicator();
	else
		hideLoadingIndicator();
}

} // namespace UserInterface

