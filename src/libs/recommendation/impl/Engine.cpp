/*
 * Copyright (C) 2019 Emeric Poupon
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

#include "Engine.hpp"

#include <unordered_map>
#include <vector>

#include "ClustersClassifierCreator.hpp"
#include "FeaturesEngineCreator.hpp"

#include "database/Db.hpp"
#include "database/Session.hpp"
#include "database/ScanSettings.hpp"
#include "utils/Exception.hpp"
#include "utils/Logger.hpp"

namespace Recommendation {


static
std::unique_ptr<IClassifier>
createClassifier(ClassifierType type)
{
	switch (type)
	{
		case ClassifierType::Clusters:
			return createClustersClassifier();
			break;

		case ClassifierType::Features:
			return createFeaturesEngine();
			break;
	}

	return {};
}

std::unique_ptr<IEngine>
createEngine(Database::Db& db)
{
	return std::make_unique<Engine>(db);
}

Engine::Engine(Database::Db& db)
: _db {db}
{
}

Engine::TrackContainer
Engine::getSimilarTracksFromTrackList(Database::Session& session, Database::TrackListId trackListId, std::size_t maxCount)
{
	TrackContainer res;

	std::shared_lock lock {_classifiersMutex};
	for (const auto& classifierName : _classifierPriorities)
	{
		auto itClassifier {_classifiers.find(classifierName)};
		if (itClassifier == std::cend(_classifiers))
			continue;

		res = itClassifier->second->getSimilarTracksFromTrackList(session, trackListId, maxCount);
		if (!res.empty())
			break;
	}

	return res;
}

Engine::TrackContainer
Engine::getSimilarTracks(Database::Session& dbSession, const std::vector<Database::TrackId>& trackIds, std::size_t maxCount)
{
	TrackContainer res;

	std::shared_lock lock {_classifiersMutex};
	for (ClassifierType classifierType : _classifierPriorities)
	{
		auto itClassifier {_classifiers.find(classifierType)};
		if (itClassifier == std::cend(_classifiers))
			continue;

		const IClassifier& classifier {*itClassifier->second};
		res = classifier.getSimilarTracks(dbSession, trackIds, maxCount);
		if (!res.empty())
		{
			LMS_LOG(RECOMMENDATION, DEBUG) << "Got " << res.size() << " similar tracks using classifier '" << classifier.getName() << "'";
			break;
		}
	}

	return res;
}

Engine::ReleaseContainer
Engine::getSimilarReleases(Database::Session& dbSession, Database::ReleaseId releaseId, std::size_t maxCount)
{
	ReleaseContainer res;

	std::shared_lock lock {_classifiersMutex};
	for (ClassifierType classifierType : _classifierPriorities)
	{
		auto itClassifier {_classifiers.find(classifierType)};
		if (itClassifier == std::cend(_classifiers))
			continue;

		const IClassifier& classifier {*itClassifier->second};
		res = classifier.getSimilarReleases(dbSession, releaseId, maxCount);
		if (!res.empty())
		{
			LMS_LOG(RECOMMENDATION, DEBUG) << "Got " << res.size() << " similar releases using classifier '" << classifier.getName() << "'";
			break;
		}
	}

	return res;
}

Engine::ArtistContainer
Engine::getSimilarArtists(Database::Session& dbSession, Database::ArtistId artistId, EnumSet<Database::TrackArtistLinkType> linkTypes, std::size_t maxCount)
{
	ArtistContainer res;

	std::shared_lock lock {_classifiersMutex};
	for (ClassifierType classifierType : _classifierPriorities)
	{
		auto itClassifier {_classifiers.find(classifierType)};
		if (itClassifier == std::cend(_classifiers))
			continue;

		const IClassifier& classifier {*itClassifier->second};
		res = classifier.getSimilarArtists(dbSession, artistId, linkTypes, maxCount);
		if (!res.empty())
		{
			LMS_LOG(RECOMMENDATION, DEBUG) << "Got " << res.size() << " similar artists using classifier '" << classifier.getName() << "'";
			return res;
		}
	}

	return res;
}

static
Database::ScanSettings::RecommendationEngineType
getRecommendationEngineType(Database::Session& session)
{
	auto transaction {session.createSharedTransaction()};

	return Database::ScanSettings::get(session)->getRecommendationEngineType();
}

void
Engine::load(bool forceReload, const ProgressCallback& progressCallback)
{
	using namespace Database;

	LMS_LOG(RECOMMENDATION, INFO) << "Reloading recommendation engines...";
	struct ClassifierWithType
	{
		ClassifierType type;
		std::unique_ptr<IClassifier> classifier;
	};

	std::vector<ClassifierWithType> classifiers;
	auto addClassifier {[&](ClassifierType type)
	{
		classifiers.emplace_back(ClassifierWithType {type, createClassifier(type)});
	}};

	switch (getRecommendationEngineType(_db.getTLSSession()))
	{
		case ScanSettings::RecommendationEngineType::Clusters:
			setClassifierPriorities({ClassifierType::Clusters});
			addClassifier(ClassifierType::Clusters);
			break;
		case ScanSettings::RecommendationEngineType::Features:
			setClassifierPriorities({ClassifierType::Features, ClassifierType::Clusters});
			// not same order since clusters is faster to load
			addClassifier(ClassifierType::Clusters);
			addClassifier(ClassifierType::Features);
			break;
	}

	assert(_pendingClassifiers.empty());
	clearClassifiers();

	{
		std::scoped_lock lock {_controlMutex};

		std::transform(std::cbegin(classifiers), std::cend(classifiers), std::inserter(_pendingClassifiers, std::end(_pendingClassifiers)),
				[](auto& classifier) { return classifier.classifier.get(); });
	}

	for (ClassifierWithType& classifier : classifiers)
		loadClassifier(std::move(classifier.classifier), classifier.type, forceReload, progressCallback);

	LMS_LOG(RECOMMENDATION, INFO) << "Recommendation engines loaded!";
}

void
Engine::setClassifierPriorities(const std::vector<ClassifierType>& classifierPriorities)
{
	std::unique_lock<std::shared_mutex> lock {_classifiersMutex};

	_classifierPriorities = classifierPriorities;
}

void
Engine::clearClassifiers()
{
	std::unique_lock lock {_classifiersMutex};

	_classifiers.clear();
}

void
Engine::loadClassifier(std::unique_ptr<IClassifier> classifier,
		ClassifierType classifierType,
		bool forceReload,
		const ProgressCallback& progressCallback)
{
	IClassifier* rawClassifier {classifier.get()};

	bool res {};
	if (!_loadCancelled)
	{
		LMS_LOG(RECOMMENDATION, INFO) << "Initializing classifier '" << classifier->getName() << "'...";

		auto progress {[&](IClassifier::Progress progress)
		{
			progressCallback(Progress {progress.processedElems, progress.totalElems});
		}};

		res = classifier->load(_db.getTLSSession(), forceReload, progressCallback ? progress : IClassifier::ProgressCallback {});

		LMS_LOG(RECOMMENDATION, INFO) << "Initializing classifier '" << classifier->getName() << "': " << (res ? "SUCCESS" : "FAILURE");
	}

	if (res)
	{
		std::unique_lock lock {_classifiersMutex};

		_classifiers.emplace(classifierType, std::move(classifier));
	}

	{
		std::scoped_lock lock {_controlMutex};

		_pendingClassifiers.erase(rawClassifier);
	}

	_pendingClassifiersCondvar.notify_one();
}

void
Engine::cancelLoad()
{
	LMS_LOG(RECOMMENDATION, DEBUG) << "Cancelling loading...";

	std::unique_lock lock {_controlMutex};

	LMS_LOG(RECOMMENDATION, DEBUG) << "Still " <<  _pendingClassifiers.size() << " pending classifiers!";

	_loadCancelled = true;

	for (IClassifier* classifier : _pendingClassifiers)
		classifier->requestCancelLoad();

    _pendingClassifiersCondvar.wait(lock, [this] {return _pendingClassifiers.empty();});
	_loadCancelled = false;

	LMS_LOG(RECOMMENDATION, DEBUG) << "Cancelling loading DONE";
}

} // ns Similarity
