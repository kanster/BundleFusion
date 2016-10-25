
#include "stdafx.h"
#include "SIFTImageManager.h"
#include "SiftGPU/SiftMatch.h"
#include "SiftGPU/SiftMatchFilter.h"
#include "SiftGPU/MatrixConversion.h"
#include "SiftVisualization.h"
#include "ImageHelper.h"
#include "GlobalAppState.h"
#include "SiftGPU/SiftCameraParams.h"
#include "SBA.h"

#include "testMatching.h"

extern "C" void updateConstantSiftCameraParams(const SiftCameraParams& params);

template<>
struct std::hash<vec2ui> : public std::unary_function < vec2ui, size_t > {
	size_t operator()(const vec2ui& v) const {
		//TODO larger prime number (64 bit) to match size_t
		const size_t p0 = 73856093;
		const size_t p1 = 19349669;
		//const size_t p2 = 83492791;
		const size_t res = ((size_t)v.x * p0) ^ ((size_t)v.y * p1);// ^ ((size_t)v.z * p2);
		return res;
	}
};

TestMatching::TestMatching()
{
	unsigned maxNumImages = GlobalBundlingState::get().s_maxNumImages;
	m_siftManager = new SIFTImageManager(GlobalBundlingState::get().s_submapSize, maxNumImages, GlobalBundlingState::get().s_maxNumKeysPerImage);

	d_numMatchesPerImagePair = NULL;
	d_matchDistancesPerImagePair = NULL;
	d_matchKeyIndicesPerImagePair = NULL;
	d_filtNumMatchesPerImagePair = NULL;
	d_filtMatchDistancesPerImagePair = NULL;
	d_filtMatchKeyIndicesPerImagePair = NULL;
	m_bAllMatchingAllocated = false;

	d_cachedFrames = NULL;

	m_stage = NONE;
}

TestMatching::~TestMatching()
{
	SAFE_DELETE(m_siftManager);

	MLIB_CUDA_SAFE_FREE(d_numMatchesPerImagePair);
	MLIB_CUDA_SAFE_FREE(d_matchDistancesPerImagePair);
	MLIB_CUDA_SAFE_FREE(d_matchKeyIndicesPerImagePair);

	MLIB_CUDA_SAFE_FREE(d_filtNumMatchesPerImagePair);
	MLIB_CUDA_SAFE_FREE(d_filtMatchDistancesPerImagePair);
	MLIB_CUDA_SAFE_FREE(d_filtMatchKeyIndicesPerImagePair);

	freeCachedFrames();
}

//void TestMatching::match(const std::string& loadFile, const std::string& outDir, const std::string& sensorFile, const vec2ui& frames /*= vec2ui((unsigned int)-1)*/)
//{
//	loadFromSensor(sensorFile, "", 1, frames);
//	const unsigned int numFrames = (unsigned int)m_colorImages.size();
//
//	if (!loadFile.empty() && util::fileExists(loadFile)) {
//
//	}
//	else { // recomputing!
//		bool printColorImages = false;
//		if (printColorImages) {
//			const std::string colorDir = "debug/color/"; if (!util::directoryExists(colorDir)) util::makeDirectory(colorDir);
//			for (unsigned int i = 0; i < numFrames; i++)
//				FreeImageWrapper::saveImage(colorDir + std::to_string(i) + ".png", m_colorImages[i]);
//		}
//
//		// detect keypoints
//		detectKeys(m_colorImages, m_depthImages, m_siftManager);
//
//		bool printKeys = true;
//		if (printKeys) {
//			const std::string keysDir = "debug/keys/"; if (!util::directoryExists(keysDir)) util::makeDirectory(keysDir);
//			for (unsigned int i = 0; i < numFrames; i++)
//				SiftVisualization::printKey(keysDir + std::to_string(i) + ".png", m_colorImages[i], m_siftManager, i);
//		}
//
//		// match
//		matchAll(true, outDir);
//	}
//
//	bool filter = true;
//	if (filter) {
//		if (m_stage == HAS_MATCHES) {
//			std::vector<vec2ui> imagePairMatchesFiltered;
//			filterImagePairMatches(imagePairMatchesFiltered, true, "debug/matchesFilt/");
//		}
//		if (m_stage == FILTERED_IMPAIR_MATCHES) {
//			filterBySurfaceArea(false);//(true, "debug/matchesSA/");
//		}
//	}
//}

//void TestMatching::load(const std::string& filename, const std::string siftFile)
//{
//	if (!util::fileExists(siftFile)) throw MLIB_EXCEPTION(siftFile + " does not exist");
//	m_siftManager->loadFromFile(siftFile);
//
//	if (filename.empty() || !util::fileExists(filename)) {
//		std::cout << "warning: " << filename << " does not exist, need to re-compute" << std::endl;
//		return;
//	}
//	BinaryDataStreamFile s(filename, false);
//	unsigned int numImages;
//	s >> numImages;
//	s >> m_origMatches;
//	s.readData((BYTE*)&m_stage, sizeof(int));
//	std::cout << "load: stage " << m_stage << std::endl;
//	for (unsigned int i = 1; i < numImages; i++) { // no matches for image 0
//		if (m_stage >= HAS_MATCHES) {
//			const bool filtered = false;
//			std::vector<unsigned int> numMatchesPerImagePair;
//			std::vector<float> matchDistancesPerImagePair;
//			std::vector<vec2ui> matchKeyIndicesPerImagePair;
//			s >> numMatchesPerImagePair;
//			s >> matchDistancesPerImagePair;
//			s >> matchKeyIndicesPerImagePair;
//			MLIB_ASSERT(numMatchesPerImagePair.size() == i && matchDistancesPerImagePair.size() == i*MAX_MATCHES_PER_IMAGE_PAIR_RAW && matchKeyIndicesPerImagePair.size() == i*MAX_MATCHES_PER_IMAGE_PAIR_RAW);
//
//			MLIB_CUDA_SAFE_CALL(cudaMemcpy(getNumMatchesCUDA(i, filtered), numMatchesPerImagePair.data(), sizeof(int)*numMatchesPerImagePair.size(), cudaMemcpyHostToDevice));
//			MLIB_CUDA_SAFE_CALL(cudaMemcpy(getMatchDistsCUDA(i, filtered), matchDistancesPerImagePair.data(), sizeof(float)*matchDistancesPerImagePair.size(), cudaMemcpyHostToDevice));
//			MLIB_CUDA_SAFE_CALL(cudaMemcpy(getMatchKeyIndicesCUDA(i, filtered), matchKeyIndicesPerImagePair.data(), sizeof(uint2)*matchKeyIndicesPerImagePair.size(), cudaMemcpyHostToDevice));
//		}
//		if (m_stage >= FILTERED_IMPAIR_MATCHES) {
//			const bool filtered = true;
//			std::vector<unsigned int> numMatchesPerImagePair;
//			std::vector<float> matchDistancesPerImagePair;
//			std::vector<vec2ui> matchKeyIndicesPerImagePair;
//			s >> numMatchesPerImagePair;
//			s >> matchDistancesPerImagePair;
//			s >> matchKeyIndicesPerImagePair;
//			MLIB_ASSERT(numMatchesPerImagePair.size() == i && matchDistancesPerImagePair.size() == i*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED && matchKeyIndicesPerImagePair.size() == i*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
//
//			MLIB_CUDA_SAFE_CALL(cudaMemcpy(getNumMatchesCUDA(i, filtered), numMatchesPerImagePair.data(), sizeof(int)*numMatchesPerImagePair.size(), cudaMemcpyHostToDevice));
//			MLIB_CUDA_SAFE_CALL(cudaMemcpy(getMatchDistsCUDA(i, filtered), matchDistancesPerImagePair.data(), sizeof(float)*matchDistancesPerImagePair.size(), cudaMemcpyHostToDevice));
//			MLIB_CUDA_SAFE_CALL(cudaMemcpy(getMatchKeyIndicesCUDA(i, filtered), matchKeyIndicesPerImagePair.data(), sizeof(uint2)*matchKeyIndicesPerImagePair.size(), cudaMemcpyHostToDevice));
//		}
//	}
//	s.closeStream();
//}
//
//void TestMatching::save(const std::string& filename) const
//{
//	std::cout << "saving orig matches... ";
//	BinaryDataStreamFile s(filename, true);
//	unsigned int numImages = m_siftManager->getNumImages();
//	s << numImages;
//	s << m_origMatches;
//	s.writeData((const BYTE*)&m_stage, sizeof(int));
//	for (unsigned int i = 1; i < numImages; i++) { // no matches for image 0
//		if (m_stage >= HAS_MATCHES) {
//			const bool filtered = false;
//			std::vector<unsigned int> numMatchesPerImagePair(i);
//			std::vector<float> matchDistancesPerImagePair(i*MAX_MATCHES_PER_IMAGE_PAIR_RAW);
//			std::vector<vec2ui> matchKeyIndicesPerImagePair(i*MAX_MATCHES_PER_IMAGE_PAIR_RAW);
//
//			MLIB_CUDA_SAFE_CALL(cudaMemcpy(numMatchesPerImagePair.data(), getNumMatchesCUDA(i, filtered), sizeof(int)*numMatchesPerImagePair.size(), cudaMemcpyDeviceToHost));
//			MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchDistancesPerImagePair.data(), getMatchDistsCUDA(i, filtered), sizeof(float)*matchDistancesPerImagePair.size(), cudaMemcpyDeviceToHost));
//			MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchKeyIndicesPerImagePair.data(), getMatchKeyIndicesCUDA(i, filtered), sizeof(uint2)*matchKeyIndicesPerImagePair.size(), cudaMemcpyDeviceToHost));
//
//			s << numMatchesPerImagePair;
//			s << matchDistancesPerImagePair;
//			s << matchKeyIndicesPerImagePair;
//		}
//		if (m_stage >= FILTERED_IMPAIR_MATCHES) {
//			const bool filtered = true;
//			std::vector<unsigned int> numMatchesPerImagePair(i);
//			std::vector<float> matchDistancesPerImagePair(i*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
//			std::vector<vec2ui> matchKeyIndicesPerImagePair(i*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
//
//			MLIB_CUDA_SAFE_CALL(cudaMemcpy(numMatchesPerImagePair.data(), getNumMatchesCUDA(i, filtered), sizeof(int)*numMatchesPerImagePair.size(), cudaMemcpyDeviceToHost));
//			MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchDistancesPerImagePair.data(), getMatchDistsCUDA(i, filtered), sizeof(float)*matchDistancesPerImagePair.size(), cudaMemcpyDeviceToHost));
//			MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchKeyIndicesPerImagePair.data(), getMatchKeyIndicesCUDA(i, filtered), sizeof(uint2)*matchKeyIndicesPerImagePair.size(), cudaMemcpyDeviceToHost));
//
//			s << numMatchesPerImagePair;
//			s << matchDistancesPerImagePair;
//			s << matchKeyIndicesPerImagePair;
//		}
//	}
//	s.closeStream();
//	std::cout << "done!" << std::endl;
//}
//
//void TestMatching::test()
//{
//	if (m_stage < HAS_MATCHES) {
//		matchAll();
//		save("debug/matchAll.bin");
//	}
//	//!!!DEBUGGING
//	//std::vector<vec2ui> matches = { vec2ui(0, 2) };
//	//printMatches("debug/", matches, false);
//	//std::cout << "waiting..." << std::endl;
//	//getchar();
//	//!!!DEBUGGING
//
//	unsigned int numImages = m_siftManager->getNumImages();
//
//	std::vector<vec2ui> imagePairMatchesFiltered;
//	filterImagePairMatches(imagePairMatchesFiltered);
//
//	//!!!DEBUGGING
//	//std::vector<vec2ui> matches = { vec2ui(94, 98), vec2ui(118, 119), vec2ui(118, 121), vec2ui(118, 133), vec2ui(118, 134), vec2ui(152, 155), vec2ui(156, 157), vec2ui(161, 163), vec2ui(165, 171), vec2ui(165, 174) };
//	std::vector<vec2ui> matches = { vec2ui(104, 125) };
//	printMatches("debug/", matches, true);
//	//saveMatchToPLY("debug/", vec2ui(118, 135), true);
//	std::cout << "waiting..." << std::endl;
//	getchar();
//	//!!!DEBUGGING
//
//	// compare to reference
//	std::vector<vec2ui> falsePositives, falseNegatives;
//	checkFiltered(imagePairMatchesFiltered, falsePositives, falseNegatives);
//
//	std::cout << "#false positives = " << falsePositives.size() << std::endl;
//	std::cout << "#false negatives = " << falseNegatives.size() << std::endl;
//
//	// visualize
//	printMatches("debug/falsePositives/", falsePositives, true);
//	printMatches("debug/falseNegatives/", falseNegatives, false);
//}

void TestMatching::matchAll(bool print /*= false*/, const std::string outDir /*= ""*/)
{
	MLIB_ASSERT(m_stage == INITIALIZED);
	MLIB_ASSERT(!print || !outDir.empty());
	allocForMatchAll();

	SiftMatchGPU* siftMatcher = new SiftMatchGPU(GlobalBundlingState::get().s_maxNumKeysPerImage);
	siftMatcher->InitSiftMatch();
	const float ratioMax = GlobalBundlingState::get().s_siftMatchRatioMaxGlobal;
	const float matchThresh = GlobalBundlingState::get().s_siftMatchThresh;
	unsigned int numImages = m_siftManager->getNumImages();
	const bool filtered = false;
	const unsigned int minNumMatches = GlobalBundlingState::get().s_minNumMatchesGlobal;

	m_origMatches.clear();
	std::cout << "matching all... ";
	Timer t;

	for (unsigned int cur = 1; cur < numImages; cur++) {

		SIFTImageGPU& curImage = m_siftManager->getImageGPU(cur);
		int num2 = (int)m_siftManager->getNumKeyPointsPerImage(cur);
		if (num2 == 0) {
			MLIB_CUDA_SAFE_CALL(cudaMemset(m_siftManager->d_currNumMatchesPerImagePair, 0, sizeof(int)*cur));
			continue;
		}

		// match to all previous
		for (unsigned int prev = 0; prev < cur; prev++) {
			SIFTImageGPU& prevImage = m_siftManager->getImageGPU(prev);
			int num1 = (int)m_siftManager->getNumKeyPointsPerImage(prev);
			if (num1 == 0) {
				MLIB_CUDA_SAFE_CALL(cudaMemset(m_siftManager->d_currNumMatchesPerImagePair + prev, 0, sizeof(int)));
				continue;
			}

			uint2 keyPointOffset = make_uint2(0, 0);
			ImagePairMatch& imagePairMatch = m_siftManager->getImagePairMatchDEBUG(prev, cur, keyPointOffset);

			siftMatcher->SetDescriptors(0, num1, (unsigned char*)prevImage.d_keyPointDescs);
			siftMatcher->SetDescriptors(1, num2, (unsigned char*)curImage.d_keyPointDescs);
			siftMatcher->GetSiftMatch(num1, imagePairMatch, keyPointOffset, matchThresh, ratioMax);
		}  // prev frames

		// print matches
		if (print) SiftVisualization::printMatches(outDir, m_siftManager, m_colorImages, cur, false);

		// save
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(getNumMatchesCUDA(cur, filtered), m_siftManager->d_currNumMatchesPerImagePair, sizeof(int)*cur, cudaMemcpyDeviceToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(getMatchDistsCUDA(cur, filtered), m_siftManager->d_currMatchDistances, sizeof(float)*cur*MAX_MATCHES_PER_IMAGE_PAIR_RAW, cudaMemcpyDeviceToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(getMatchKeyIndicesCUDA(cur, filtered), m_siftManager->d_currMatchKeyPointIndices, sizeof(uint2)*cur*MAX_MATCHES_PER_IMAGE_PAIR_RAW, cudaMemcpyDeviceToDevice));

		std::vector<int> numMatches(cur);
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(numMatches.data(), getNumMatchesCUDA(cur, filtered), sizeof(int)*cur, cudaMemcpyDeviceToHost));
		for (unsigned int i = 0; i < numMatches.size(); i++) {
			if (numMatches[i] >(int)minNumMatches) {
				m_origMatches.push_back(vec2ui(i, cur));
			}
		}
	} // cur frames
	t.stop();
	std::cout << "done! (" << t.getElapsedTimeMS() << " ms)" << std::endl;
	std::cout << "#orig matches = " << m_origMatches.size() << std::endl;

	SAFE_DELETE(siftMatcher);

	m_stage = HAS_MATCHES;
}

void TestMatching::filterImagePairMatches(std::vector<vec2ui>& imagePairMatchesFiltered, bool print /*= false*/, const std::string& outDir /*= ""*/)
{
	MLIB_ASSERT(m_stage == HAS_MATCHES);
	MLIB_ASSERT(!print || !outDir.empty());

	unsigned int minNumMatches = GlobalBundlingState::get().s_minNumMatchesGlobal;
	const float maxResThresh2 = GlobalBundlingState::get().s_maxKabschResidual2;

	imagePairMatchesFiltered.clear();
	std::cout << "filtering... " << std::endl;
	Timer t;

	unsigned int numImages = m_siftManager->getNumImages();
	//const std::vector<int>& valid = m_siftManager->getValidImages();
	for (unsigned int cur = 1; cur < numImages; cur++) {
		std::cout << cur << " ";
		//if (valid[cur] == 0) continue;

		// copy respective matches
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(m_siftManager->d_currNumMatchesPerImagePair, getNumMatchesCUDA(cur, false), sizeof(int)*cur, cudaMemcpyDeviceToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(m_siftManager->d_currMatchDistances, getMatchDistsCUDA(cur, false), sizeof(float)*cur*MAX_MATCHES_PER_IMAGE_PAIR_RAW, cudaMemcpyDeviceToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(m_siftManager->d_currMatchKeyPointIndices, getMatchKeyIndicesCUDA(cur, false), sizeof(uint2)*cur*MAX_MATCHES_PER_IMAGE_PAIR_RAW, cudaMemcpyDeviceToDevice));

		//SIFTMatchFilter::filterKeyPointMatchesDEBUG(cur, m_siftManager, MatrixConversion::toCUDA(m_colorCalibration.m_IntrinsicInverse), minNumMatches, maxResThresh2, false);
		SIFTMatchFilter::ransacKeyPointMatchesDEBUG(cur, m_siftManager, MatrixConversion::toCUDA(m_colorCalibration.m_IntrinsicInverse), minNumMatches, maxResThresh2, false);

		// print matches
		if (print) SiftVisualization::printMatches(outDir, m_siftManager, m_colorImages, cur, true);

		std::vector<unsigned int> numMatches(cur);
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(numMatches.data(), m_siftManager->d_currNumFilteredMatchesPerImagePair, sizeof(int)*cur, cudaMemcpyDeviceToHost));
		for (unsigned int i = 0; i < cur; i++) {
			if (numMatches[i] > 0) imagePairMatchesFiltered.push_back(vec2ui(i, cur));
		}
		// to have for debug printing
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(getNumMatchesCUDA(cur, true), m_siftManager->d_currNumFilteredMatchesPerImagePair, sizeof(int)*cur, cudaMemcpyDeviceToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(getMatchDistsCUDA(cur, true), m_siftManager->d_currFilteredMatchDistances, sizeof(float)*cur*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED, cudaMemcpyDeviceToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(getMatchKeyIndicesCUDA(cur, true), m_siftManager->d_currFilteredMatchKeyPointIndices, sizeof(uint2)*cur*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED, cudaMemcpyDeviceToDevice));
	} // cur frames

	t.stop();
	std::cout << std::endl << "done! (" << t.getElapsedTimeMS() << " ms)" << std::endl;

	m_stage = FILTERED_IMPAIR_MATCHES;
}

void TestMatching::checkFiltered(const std::vector<vec2ui>& imagePairMatchesFiltered, std::vector<vec2ui>& falsePositives, std::vector<vec2ui>& falseNegatives) const
{
	const float maxResidualThres2 = 0.05 * 0.05;

	// get data
	std::vector<SIFTKeyPoint> keys;
	keys.resize(m_siftManager->m_numKeyPoints);
	MLIB_CUDA_SAFE_CALL(cudaMemcpy(keys.data(), m_siftManager->d_keyPoints, sizeof(SIFTKeyPoint) * keys.size(), cudaMemcpyDeviceToHost));

	unsigned int numImages = m_siftManager->getNumImages();
	unsigned int maxNumMatches = (numImages - 1)*numImages / 2;
	std::vector<int> numMatchesRaw(maxNumMatches); std::vector<float> matchDistsRaw; std::vector<uint2> matchKeyIndicesRaw;
	std::vector<int> numMatchesFilt(maxNumMatches); std::vector<float> matchDistsFilt; std::vector<uint2> matchKeyIndicesFilt;
	unsigned int offsetRaw = MAX_MATCHES_PER_IMAGE_PAIR_RAW, offsetFilt = MAX_MATCHES_PER_IMAGE_PAIR_FILTERED;
	// filtered
	MLIB_CUDA_SAFE_CALL(cudaMemcpy(numMatchesFilt.data(), d_filtNumMatchesPerImagePair, sizeof(int)*numMatchesFilt.size(), cudaMemcpyDeviceToHost));
	matchDistsFilt.resize(MAX_MATCHES_PER_IMAGE_PAIR_FILTERED * maxNumMatches);
	matchKeyIndicesFilt.resize(MAX_MATCHES_PER_IMAGE_PAIR_FILTERED * maxNumMatches);
	MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchDistsFilt.data(), d_filtMatchDistancesPerImagePair, sizeof(float)*matchDistsFilt.size(), cudaMemcpyDeviceToHost));
	MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchKeyIndicesFilt.data(), d_filtMatchKeyIndicesPerImagePair, sizeof(uint2)*matchKeyIndicesFilt.size(), cudaMemcpyDeviceToHost));
	// raw
	MLIB_CUDA_SAFE_CALL(cudaMemcpy(numMatchesRaw.data(), d_numMatchesPerImagePair, sizeof(int)*numMatchesRaw.size(), cudaMemcpyDeviceToHost));
	matchDistsRaw.resize(MAX_MATCHES_PER_IMAGE_PAIR_RAW * maxNumMatches);
	matchKeyIndicesRaw.resize(MAX_MATCHES_PER_IMAGE_PAIR_RAW * maxNumMatches);
	MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchDistsRaw.data(), d_matchDistancesPerImagePair, sizeof(float)*matchDistsRaw.size(), cudaMemcpyDeviceToHost));
	MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchKeyIndicesRaw.data(), d_matchKeyIndicesPerImagePair, sizeof(uint2)*matchKeyIndicesRaw.size(), cudaMemcpyDeviceToHost));

	// compare to reference trajectory
	std::unordered_set<vec2ui> filteredSet;
	for (unsigned int i = 0; i < imagePairMatchesFiltered.size(); i++) filteredSet.insert(imagePairMatchesFiltered[i]);

	for (unsigned int i = 0; i < m_origMatches.size(); i++) {
		const vec2ui& imageIndices = m_origMatches[i];
		MLIB_ASSERT(m_referenceTrajectory[imageIndices.x][0] != -std::numeric_limits<float>::infinity() &&
			m_referenceTrajectory[imageIndices.y][0] != -std::numeric_limits<float>::infinity());
		if (m_referenceTrajectory[imageIndices.x][0] == -std::numeric_limits<float>::infinity() ||
			m_referenceTrajectory[imageIndices.y][0] == -std::numeric_limits<float>::infinity()) {
			std::cout << "warning: no reference trajectory for " << imageIndices << std::endl;
		}
		const mat4f transform = m_referenceTrajectory[imageIndices.y].getInverse() * m_referenceTrajectory[imageIndices.x]; // src to tgt

		unsigned int idx = (imageIndices.y - 1) * imageIndices.y / 2 + imageIndices.x;
		std::vector<vec3f> srcPts(MAX_MATCHES_PER_IMAGE_PAIR_RAW), tgtPts(MAX_MATCHES_PER_IMAGE_PAIR_RAW);

		MLIB_ASSERT(numMatchesRaw[idx] > 0);
		if (filteredSet.find(imageIndices) != filteredSet.end()) { // classified good
			MLIB_ASSERT(numMatchesFilt[idx] > 0);
			getSrcAndTgtPts(keys.data(), matchKeyIndicesFilt.data() + idx*offsetFilt, numMatchesFilt[idx],
				(float3*)srcPts.data(), (float3*)tgtPts.data(), MatrixConversion::toCUDA(m_colorCalibration.m_IntrinsicInverse));
			float maxRes = 0.0f;
			for (int i = 0; i < numMatchesFilt[idx]; i++) {
				vec3f d = transform * srcPts[i] - tgtPts[i];
				float res = d | d;
				if (res > maxRes) maxRes = res;
			}
			if (maxRes > maxResidualThres2) { // bad
				falsePositives.push_back(imageIndices);
			}
		}
		else { // filtered out
			getSrcAndTgtPts(keys.data(), matchKeyIndicesRaw.data() + idx*offsetRaw, numMatchesRaw[idx],
				(float3*)srcPts.data(), (float3*)tgtPts.data(), MatrixConversion::toCUDA(m_colorCalibration.m_IntrinsicInverse));
			float maxRes = 0.0f; const unsigned int minNumMatches = GlobalBundlingState::get().s_minNumMatchesGlobal;
			for (unsigned int i = 0; i < minNumMatches; i++) {//numMatchesRaw[idx]; i++) {
				vec3f d = transform * srcPts[i] - tgtPts[i];
				float res = d | d;
				if (res > maxRes) maxRes = res;
			}
			if (maxRes <= maxResidualThres2) { // good
				falseNegatives.push_back(imageIndices);
			}
		}
	}
}

void TestMatching::printMatches(const std::string& outDir, const std::vector<vec2ui>& imagePairMatches, bool filtered) const
{
	if (!util::directoryExists(outDir)) util::makeDirectory(outDir);

	// get data
	std::vector<SIFTKeyPoint> keys;
	keys.resize(m_siftManager->m_numKeyPoints);
	MLIB_CUDA_SAFE_CALL(cudaMemcpy(keys.data(), m_siftManager->d_keyPoints, sizeof(SIFTKeyPoint) * keys.size(), cudaMemcpyDeviceToHost));

	const float distMax = GlobalBundlingState::get().s_siftMatchThresh;

	unsigned int numImages = m_siftManager->getNumImages();
	unsigned int maxNumMatches = (numImages - 1)*numImages / 2;
	std::vector<int> numMatches(maxNumMatches);
	std::vector<float> matchDists;
	std::vector<uint2> matchKeyIndices;
	unsigned int OFFSET;
	if (filtered) {
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(numMatches.data(), d_filtNumMatchesPerImagePair, sizeof(int)*numMatches.size(), cudaMemcpyDeviceToHost));
		matchDists.resize(MAX_MATCHES_PER_IMAGE_PAIR_FILTERED * maxNumMatches);
		matchKeyIndices.resize(MAX_MATCHES_PER_IMAGE_PAIR_FILTERED * maxNumMatches);
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchDists.data(), d_filtMatchDistancesPerImagePair, sizeof(float)*matchDists.size(), cudaMemcpyDeviceToHost));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchKeyIndices.data(), d_filtMatchKeyIndicesPerImagePair, sizeof(uint2)*matchKeyIndices.size(), cudaMemcpyDeviceToHost));
		OFFSET = MAX_MATCHES_PER_IMAGE_PAIR_FILTERED;
	}
	else {
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(numMatches.data(), d_numMatchesPerImagePair, sizeof(int)*numMatches.size(), cudaMemcpyDeviceToHost));
		matchDists.resize(MAX_MATCHES_PER_IMAGE_PAIR_RAW * maxNumMatches);
		matchKeyIndices.resize(MAX_MATCHES_PER_IMAGE_PAIR_RAW * maxNumMatches);
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchDists.data(), d_matchDistancesPerImagePair, sizeof(float)*matchDists.size(), cudaMemcpyDeviceToHost));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchKeyIndices.data(), d_matchKeyIndicesPerImagePair, sizeof(uint2)*matchKeyIndices.size(), cudaMemcpyDeviceToHost));
		OFFSET = MAX_MATCHES_PER_IMAGE_PAIR_RAW;
	}
	for (unsigned int i = 0; i < imagePairMatches.size(); i++) {
		const vec2ui& imageIndices = imagePairMatches[i];
		const ColorImageR8G8B8& image1 = m_colorImages[imageIndices.x];
		const ColorImageR8G8B8& image2 = m_colorImages[imageIndices.y];

		unsigned int idx = (imageIndices.y - 1) * imageIndices.y / 2 + imageIndices.x;
		if (numMatches[idx] == 0) return;

		const std::string filename = outDir + std::to_string(imageIndices.x) + "-" + std::to_string(imageIndices.y) + ".png";

		ColorImageR32G32B32 matchImage(image1.getWidth() * 2, image1.getHeight());
		ColorImageR32G32B32 im1(image1);
		ColorImageR32G32B32 im2(image2);
		matchImage.copyIntoImage(im1, 0, 0);
		matchImage.copyIntoImage(im2, image1.getWidth(), 0);

		const float scaleWidth = (float)m_colorImages[0].getWidth() / (float)GlobalBundlingState::get().s_widthSIFT;
		const float scaleHeight = (float)m_colorImages[0].getHeight() / (float)GlobalBundlingState::get().s_heightSIFT;

		float maxMatchDistance = 0.0f;
		RGBColor lowColor = ml::RGBColor::Blue;
		RGBColor highColor = ml::RGBColor::Red;
		//unsigned int numMatchesToPrint = filtered ? numMatches[idx] : std::min(GlobalBundlingState::get().s_minNumMatchesGlobal, (unsigned int)numMatches[idx]);
		unsigned int numMatchesToPrint = numMatches[idx];
		for (unsigned int i = 0; i < numMatchesToPrint; i++) {
			const SIFTKeyPoint& key1 = keys[matchKeyIndices[idx*OFFSET + i].x];
			const SIFTKeyPoint& key2 = keys[matchKeyIndices[idx*OFFSET + i].y];
			if (matchDists[idx*OFFSET + i] > maxMatchDistance) maxMatchDistance = matchDists[idx*OFFSET + i];

			vec2f pf0(key1.pos.x * scaleWidth, key1.pos.y * scaleHeight);
			vec2f pf1(key2.pos.x * scaleWidth, key2.pos.y * scaleHeight);

			//std::cout << "[" << i << "] (" << matchKeyIndices[idx*OFFSET + i].x << ", " << matchKeyIndices[idx*OFFSET + i].y << ") " << pf0 << " | " << pf1 << std::endl;

			RGBColor c = RGBColor::interpolate(lowColor, highColor, matchDists[idx*OFFSET + i] / distMax);
			vec3f color(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f);
			vec2i p0 = ml::math::round(ml::vec2f(pf0.x, pf0.y));
			vec2i p1 = ml::math::round(ml::vec2f(pf1.x + image1.getWidth(), pf1.y));
			ImageHelper::drawCircle(matchImage, p0, ml::math::round(key1.scale), color);
			ImageHelper::drawCircle(matchImage, p1, ml::math::round(key2.scale), color);
			ImageHelper::drawLine(matchImage, p0, p1, color);
		}
		//std::cout << "(" << imageIndices << "): max match distance = " << maxMatchDistance << std::endl;
		FreeImageWrapper::saveImage(filename, matchImage);
	}
}

void TestMatching::loadFromSensor(const std::string& sensorFile, const std::string& trajectoryFile, unsigned int skip, const vec2ui& frames /*= vec2ui((unsigned int)-1)*/, bool createCache /*= true*/)
{
	std::cout << "WARNING: Color needs to be modified to rgb" << std::endl; getchar();
	if (skip > 1) {
		std::cout << "WARNING: need to load keys for skip " << skip << std::endl;
		getchar();
	}
	throw MLIB_EXCEPTION(".sensor unsupported!");

	std::cout << "loading color images from sensor... ";
	CalibratedSensorData cs; std::vector<mat4f> refTrajectory;
	{
		BinaryDataStreamFile s(sensorFile, false);
		s >> cs;
		s.closeStream();
	}
	if (!trajectoryFile.empty()) {
		BinaryDataStreamFile s(trajectoryFile, false);
		s >> refTrajectory;
	}
	else {
		refTrajectory = cs.m_trajectory;
	}
	vec2ui startEnd = frames;
	if (frames.x == (unsigned int)-1) {
		startEnd.x = 0;
		startEnd.y = cs.m_ColorNumFrames;
	}
	MLIB_ASSERT(startEnd.y > startEnd.x);
	unsigned int numFrames = (startEnd.y - startEnd.x - 1) / skip + 1;

	m_colorImages.resize(numFrames);
	m_depthImages.resize(numFrames);
	m_referenceTrajectory.resize(numFrames);
	MLIB_ASSERT((startEnd.y - startEnd.x - 1) / skip == numFrames - 1);
	for (unsigned int i = startEnd.x; i < startEnd.y; i += skip) {
		const unsigned int newIndex = (i - startEnd.x) / skip;
		MLIB_ASSERT(newIndex < m_colorImages.size());
		//m_colorImages[newIndex] = ColorImageR8G8B8A8(cs.m_ColorImageWidth, cs.m_ColorImageHeight, cs.m_ColorImages[i]);
		m_depthImages[newIndex] = DepthImage32(cs.m_DepthImageWidth, cs.m_DepthImageHeight, cs.m_DepthImages[i]);
		m_referenceTrajectory[newIndex] = refTrajectory[i];
	}

	m_depthCalibration = cs.m_CalibrationDepth;
	m_colorCalibration = cs.m_CalibrationColor;

	std::cout << "done! (" << m_colorImages.size() << " of " << cs.m_ColorNumFrames << ")" << std::endl;

	initSiftParams(m_depthImages.front().getWidth(), m_depthImages.front().getHeight(),
		m_colorImages.front().getWidth(), m_colorImages.front().getHeight());

	m_stage = INITIALIZED;

	bool debugSave = (numFrames != cs.m_ColorNumFrames) || (skip > 1);
	if (debugSave) {
		std::cout << "saving debug sensor... ";
		if (startEnd.y < cs.m_ColorNumFrames) {
			cs.m_ColorImages.erase(cs.m_ColorImages.begin() + startEnd.y, cs.m_ColorImages.end());
			cs.m_DepthImages.erase(cs.m_DepthImages.begin() + startEnd.y, cs.m_DepthImages.end());
			cs.m_trajectory.erase(cs.m_trajectory.begin() + startEnd.y, cs.m_trajectory.end());
		}
		if (skip > 1) {
			unsigned int maxFrame = startEnd.y / skip;
			for (int i = (int)(maxFrame * skip); i >= (int)startEnd.x; i -= skip) {
				if (i + 1 < (int)cs.m_ColorNumFrames) {
					cs.m_ColorImages.erase(cs.m_ColorImages.begin() + i + 1, cs.m_ColorImages.begin() + std::min(i + skip, cs.m_ColorNumFrames));
					cs.m_DepthImages.erase(cs.m_DepthImages.begin() + i + 1, cs.m_DepthImages.begin() + std::min(i + skip, cs.m_ColorNumFrames));
					cs.m_trajectory.erase(cs.m_trajectory.begin() + i + 1, cs.m_trajectory.begin() + std::min(i + skip, cs.m_ColorNumFrames));
				}
			}
		}
		if (startEnd.x > 0) {
			cs.m_ColorImages.erase(cs.m_ColorImages.begin(), cs.m_ColorImages.begin() + startEnd.x);
			cs.m_DepthImages.erase(cs.m_DepthImages.begin(), cs.m_DepthImages.begin() + startEnd.x);
			cs.m_trajectory.erase(cs.m_trajectory.begin(), cs.m_trajectory.begin() + startEnd.y);
		}
		cs.m_ColorNumFrames = (unsigned int)cs.m_ColorImages.size();
		cs.m_DepthNumFrames = (unsigned int)cs.m_DepthImages.size();

		BinaryDataStreamFile s("debug/debug.sensor", true);
		s << cs;
		s.closeStream();
		std::cout << "done!" << std::endl;
	}

	if (createCache) createCachedFrames();
}

void TestMatching::saveToPointCloud(const std::string& filename, const std::vector<unsigned int>& frameIndices /*= std::vector<unsigned int>()*/) const
{
	std::cout << "computing point cloud..." << std::endl;
	std::vector<unsigned int> pointCloudFrameIndices;
	if (frameIndices.empty()) {
		for (unsigned int i = 0; i < m_colorImages.size(); i++) pointCloudFrameIndices.push_back(i);
	}
	else {
		pointCloudFrameIndices = frameIndices;
	}

	PointCloudf pc;
	for (unsigned int k = 0; k < pointCloudFrameIndices.size(); k++) {
		recordPointCloud(pc, pointCloudFrameIndices[k]);
	} // frames
	std::cout << "saving to file... ";
	PointCloudIOf::saveToFile(filename, pc);
	std::cout << "done!" << std::endl;
}

void TestMatching::recordPointCloud(PointCloudf& pc, unsigned int frame) const
{
	if (m_referenceTrajectory[frame][0] == -std::numeric_limits<float>::infinity()) {
		std::cout << "warning: invalid frame " << frame << std::endl;
		return;
	}
	unsigned int depthWidth = m_depthImages[0].getWidth();
	float scaleWidth = (float)m_colorImages[0].getWidth() / (float)m_depthImages[0].getWidth();
	float scaleHeight = (float)m_colorImages[0].getHeight() / (float)m_depthImages[0].getHeight();

	for (unsigned int p = 0; p < m_depthImages[frame].getNumPixels(); p++) {
		unsigned int x = p%depthWidth; unsigned int y = p / depthWidth;
		float depth = m_depthImages[frame](x, y);
		if (depth != -std::numeric_limits<float>::infinity()) {
			vec3f camPos = m_depthCalibration.m_IntrinsicInverse * (depth * vec3f((float)x, (float)y, 1.0f));
			pc.m_points.push_back(m_referenceTrajectory[frame] * camPos);
			unsigned int cx = (unsigned int)math::round(x * scaleWidth);
			unsigned int cy = (unsigned int)math::round(y * scaleHeight);
			vec3uc c = m_colorImages[frame](cx, cy);
			pc.m_colors.push_back(vec4f(vec3f(c) / 255.0f));

			vec3f wpos = m_referenceTrajectory[frame] * camPos;
			if (isnan(wpos.x) || isnan(wpos.y) || isnan(wpos.z))
				std::cout << "[recordPointCloud] ERROR nan in frame" << std::endl;
		} // valid depth
	} // depth pixels
}

//void TestMatching::saveMatchToPLY(const std::string& dir, const vec2ui& imageIndices, bool filtered) const
//{
//	std::cout << "saving match " << imageIndices << "... ";
//	// frames
//	PointCloudf pc0, pc1;
//	recordPointCloud(pc0, imageIndices.x);
//	recordPointCloud(pc1, imageIndices.y);
//	PointCloudIOf::saveToFile(dir + std::to_string(imageIndices.x) + ".ply", pc0);
//	PointCloudIOf::saveToFile(dir + std::to_string(imageIndices.y) + ".ply", pc1);
//
//	// keys
//	vec4f red(1.0f, 0.0f, 0.0f, 1.0f);
//	vec4f green(0.0f, 1.0f, 0.0f, 1.0f);
//	MeshDataf keys0, keys1;
//	recordKeysMeshData(keys0, keys1, imageIndices, filtered, red, green);
//	MeshIOf::saveToFile(dir + std::to_string(imageIndices.x) + "-keys.ply", keys0);
//	MeshIOf::saveToFile(dir + std::to_string(imageIndices.y) + "-keys.ply", keys1);
//	std::cout << "done!" << std::endl;
//}

void TestMatching::recordKeysMeshData(MeshDataf& keys0, MeshDataf& keys1, const vec2ui& imageIndices, bool filtered, const vec4f& color0, const vec4f& color1) const
{
	MLIB_ASSERT(m_referenceTrajectory[imageIndices.x][0] != -std::numeric_limits<float>::infinity() &&
		m_referenceTrajectory[imageIndices.y][0] != -std::numeric_limits<float>::infinity());
	const unsigned int matchIdx = ((imageIndices.y - 1) * imageIndices.y) / 2 + imageIndices.x;

	std::vector<SIFTKeyPoint> keys;
	m_siftManager->getSIFTKeyPointsDEBUG(keys);

	const unsigned int OFFSET = filtered ? MAX_MATCHES_PER_IMAGE_PAIR_FILTERED : MAX_MATCHES_PER_IMAGE_PAIR_RAW;
	unsigned int numMatches;
	std::vector<float> matchDists;
	std::vector<uint2> matchKeyIndices;
	if (filtered) {
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(&numMatches, d_filtNumMatchesPerImagePair + matchIdx, sizeof(int), cudaMemcpyDeviceToHost));
		MLIB_ASSERT(numMatches < MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
	}
	else {
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(&numMatches, d_numMatchesPerImagePair + matchIdx, sizeof(int), cudaMemcpyDeviceToHost));
		MLIB_ASSERT(numMatches < MAX_MATCHES_PER_IMAGE_PAIR_RAW);
	}
	if (numMatches == 0) {
		std::cout << "error: no matches for images " << imageIndices << std::endl;
		return;
	}
	matchDists.resize(numMatches); matchKeyIndices.resize(numMatches);
	if (filtered) {
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchDists.data(), d_filtMatchDistancesPerImagePair + matchIdx*OFFSET, sizeof(float)*numMatches, cudaMemcpyDeviceToHost));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchKeyIndices.data(), d_filtMatchKeyIndicesPerImagePair + matchIdx*OFFSET, sizeof(uint2)*numMatches, cudaMemcpyDeviceToHost));
	}
	else {
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchDists.data(), d_matchDistancesPerImagePair + matchIdx*OFFSET, sizeof(float)*numMatches, cudaMemcpyDeviceToHost));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(matchKeyIndices.data(), d_matchKeyIndicesPerImagePair + matchIdx*OFFSET, sizeof(uint2)*numMatches, cudaMemcpyDeviceToHost));
	}

	const float radius = 0.02f;
	std::vector<vec3f> srcPts(numMatches), tgtPts(numMatches);
	getSrcAndTgtPts(keys.data(), matchKeyIndices.data(), numMatches, (float3*)srcPts.data(), (float3*)tgtPts.data(), MatrixConversion::toCUDA(m_colorCalibration.m_IntrinsicInverse));
	for (unsigned int i = 0; i < numMatches; i++) {
		keys0.merge(Shapesf::sphere(radius, m_referenceTrajectory[imageIndices.x] * srcPts[i], 10, 10, color0).getMeshData());
		keys1.merge(Shapesf::sphere(radius, m_referenceTrajectory[imageIndices.y] * tgtPts[i], 10, 10, color1).getMeshData());
	}
}

void TestMatching::initSiftParams(unsigned int widthDepth, unsigned int heightDepth, unsigned int widthColor, unsigned int heightColor)
{
	m_widthSift = GlobalBundlingState::get().s_widthSIFT;
	m_heightSift = GlobalBundlingState::get().s_heightSIFT;
	m_widthDepth = widthDepth;
	m_heightDepth = heightDepth;
	if (widthColor != m_widthSift && heightColor != m_heightSift) {
		// adapt intrinsics
		const float scaleWidth = (float)m_widthSift / (float)widthColor;
		const float scaleHeight = (float)m_heightSift / (float)heightColor;

		m_colorCalibration.m_Intrinsic._m00 *= scaleWidth;  m_colorCalibration.m_Intrinsic._m02 *= scaleWidth;
		m_colorCalibration.m_Intrinsic._m11 *= scaleHeight; m_colorCalibration.m_Intrinsic._m12 *= scaleHeight;

		m_colorCalibration.m_IntrinsicInverse._m00 /= scaleWidth; m_colorCalibration.m_IntrinsicInverse._m11 /= scaleHeight;

		for (unsigned int i = 0; i < m_colorImages.size(); i++) {
			m_colorImages[i].resize(m_widthSift, m_heightSift);
		}
	}

	SiftCameraParams siftCameraParams;
	siftCameraParams.m_depthWidth = widthDepth;
	siftCameraParams.m_depthHeight = heightDepth;
	siftCameraParams.m_intensityWidth = m_widthSift;
	siftCameraParams.m_intensityHeight = m_heightSift;
	siftCameraParams.m_siftIntrinsics = MatrixConversion::toCUDA(m_colorCalibration.m_Intrinsic);
	siftCameraParams.m_siftIntrinsicsInv = MatrixConversion::toCUDA(m_colorCalibration.m_IntrinsicInverse);
	siftCameraParams.m_minKeyScale = GlobalBundlingState::get().s_minKeyScale;
	updateConstantSiftCameraParams(siftCameraParams);
}

void TestMatching::detectKeys(const std::vector<ColorImageR8G8B8> &colorImages, const std::vector<DepthImage32> &depthImages, SIFTImageManager *siftManager) const
{
	std::cout << "detecting keypoints... ";
	SiftGPU* sift = new SiftGPU;
	sift->SetParams(m_widthSift, m_heightSift, false, 150, GlobalAppState::get().s_sensorDepthMin, GlobalAppState::get().s_sensorDepthMax);
	sift->InitSiftGPU();
	const unsigned int numFrames = (unsigned int)colorImages.size();

	float *d_intensity = NULL, *d_depth = NULL;
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_intensity, sizeof(float)*m_widthSift*m_heightSift));
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_depth, sizeof(float)*m_widthDepth*m_heightDepth));
	for (unsigned int f = 0; f < numFrames; f++) {
		ColorImageR32 intensity = convertToGrayScale(colorImages[f]);
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_intensity, intensity.getData(), sizeof(float)*m_widthSift*m_heightSift, cudaMemcpyHostToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_depth, depthImages[f].getData(), sizeof(float)*m_widthDepth*m_heightDepth, cudaMemcpyHostToDevice));
		// run sift
		SIFTImageGPU& cur = siftManager->createSIFTImageGPU();
		int success = sift->RunSIFT(d_intensity, d_depth);
		if (!success) throw MLIB_EXCEPTION("Error running SIFT detection");
		unsigned int numKeypoints = sift->GetKeyPointsAndDescriptorsCUDA(cur, d_depth);
		siftManager->finalizeSIFTImageGPU(numKeypoints);
		std::cout << "\t" << f << ": " << numKeypoints << " keys" << std::endl;
	}

	MLIB_CUDA_SAFE_FREE(d_intensity);
	MLIB_CUDA_SAFE_FREE(d_depth);
	SAFE_DELETE(sift);
	std::cout << "done!" << std::endl;
}

void TestMatching::createCachedFrames()
{
	std::cout << "creating cached frames... ";
	if (m_colorImages.front().getWidth() != m_widthSift) {
		std::cout << "does not support different color/sift res!" << std::endl;
		while (1);
	}

	unsigned int width = GlobalBundlingState::get().s_downsampledWidth;
	unsigned int height = GlobalBundlingState::get().s_downsampledHeight;

	const float scaleWidth = (float)width / (float)m_widthDepth;
	const float scaleHeight = (float)height / (float)m_heightDepth;
	m_intrinsicsDownsampled = m_depthCalibration.m_Intrinsic;
	m_intrinsicsDownsampled._m00 *= scaleWidth;  m_intrinsicsDownsampled._m02 *= scaleWidth;
	m_intrinsicsDownsampled._m11 *= scaleHeight; m_intrinsicsDownsampled._m12 *= scaleHeight;

	allocCachedFrames((unsigned int)m_colorImages.size(), width, height);

	float* d_depth = NULL; float* d_depthErodeHelper = NULL; uchar4* d_color = NULL;
	float4* d_helperCamPos = NULL; float4* d_helperNormal = NULL; float* d_filterHelperDown = NULL;
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_depth, sizeof(float) * m_widthDepth * m_heightDepth));
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_color, sizeof(uchar4) * m_widthSift * m_heightSift));
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_depthErodeHelper, sizeof(float) * m_widthDepth * m_heightDepth));
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_helperCamPos, sizeof(float4) * m_widthDepth * m_heightDepth));
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_helperNormal, sizeof(float4) * m_widthDepth * m_heightDepth));
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_filterHelperDown, sizeof(float) * width * height));

	float intensityFilterSigma = GlobalBundlingState::get().s_colorDownSigma;
	float depthFilterSigmaD = GlobalBundlingState::get().s_depthDownSigmaD;
	float depthFilterSigmaR = GlobalBundlingState::get().s_depthDownSigmaR;
	//erode and smooth depth
	bool erode = GlobalBundlingState::get().s_erodeSIFTdepth;
	//bool smooth = GlobalBundlingState::get().s_depthFilter;
	for (unsigned int i = 0; i < m_colorImages.size(); i++) {
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_depth, m_depthImages[i].getData(), sizeof(float) * m_depthImages[i].getNumPixels(), cudaMemcpyHostToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_color, m_colorImages[i].getData(), sizeof(uchar4) * m_colorImages[i].getNumPixels(), cudaMemcpyHostToDevice));
		if (erode) {
			unsigned int numIter = 2; numIter = 2 * ((numIter + 1) / 2);
			for (unsigned int k = 0; k < numIter; k++) {
				if (k % 2 == 0) {
					CUDAImageUtil::erodeDepthMap(d_depthErodeHelper, d_depth, 3, m_widthDepth, m_heightDepth, 0.05f, 0.3f);
				}
				else {
					CUDAImageUtil::erodeDepthMap(d_depth, d_depthErodeHelper, 3, m_widthDepth, m_heightDepth, 0.05f, 0.3f);
				}
			}
		}
		//if (smooth) {
		//	CUDAImageUtil::gaussFilterDepthMap(d_depthErodeHelper, d_depth, GlobalBundlingState::get().s_depthSigmaD, GlobalBundlingState::get().s_depthSigmaR, m_widthDepth, m_heightDepth);
		//	std::swap(d_depth, d_depthErodeHelper);
		//}
		if (erode) MLIB_CUDA_SAFE_CALL(cudaMemcpy(m_depthImages[i].getData(), d_depth, sizeof(float) * m_depthImages[i].getNumPixels(), cudaMemcpyDeviceToHost)); //for vis only

		CUDACachedFrame& frame = m_cachedFrames[i];
		if (depthFilterSigmaD > 0.0f) {
			CUDAImageUtil::gaussFilterDepthMap(d_depthErodeHelper, d_depth, depthFilterSigmaD, depthFilterSigmaR, m_widthDepth, m_heightDepth);
			std::swap(d_depthErodeHelper, d_depth);
		}
		CUDAImageUtil::convertDepthFloatToCameraSpaceFloat4(d_helperCamPos, d_depth, MatrixConversion::toCUDA(m_intrinsicsDownsampled.getInverse()), m_widthDepth, m_heightDepth);
		CUDAImageUtil::resampleFloat4(frame.d_cameraposDownsampled, width, height, d_helperCamPos, m_widthDepth, m_heightDepth);

		CUDAImageUtil::computeNormals(d_helperNormal, d_helperCamPos, m_widthDepth, m_heightDepth);
		CUDAImageUtil::resampleFloat4(frame.d_normalsDownsampled, width, height, d_helperNormal, m_widthDepth, m_heightDepth);

		CUDAImageUtil::convertNormalsFloat4ToUCHAR4(frame.d_normalsDownsampledUCHAR4, frame.d_normalsDownsampled, width, height);

		CUDAImageUtil::resampleFloat(frame.d_depthDownsampled, width, height, d_depth, m_widthDepth, m_heightDepth);

		CUDAImageUtil::resampleToIntensity(d_filterHelperDown, width, height, d_color, m_widthSift, m_heightSift);
		if (intensityFilterSigma > 0.0f) CUDAImageUtil::gaussFilterIntensity(frame.d_intensityDownsampled, d_filterHelperDown, intensityFilterSigma, width, height);
		else std::swap(frame.d_intensityDownsampled, d_filterHelperDown);
		CUDAImageUtil::computeIntensityDerivatives(frame.d_intensityDerivsDownsampled, frame.d_intensityDownsampled, width, height);
	}

	MLIB_CUDA_SAFE_FREE(d_depth);
	MLIB_CUDA_SAFE_FREE(d_color);
	MLIB_CUDA_SAFE_FREE(d_depthErodeHelper);
	MLIB_CUDA_SAFE_FREE(d_helperCamPos);
	MLIB_CUDA_SAFE_FREE(d_helperNormal);
	MLIB_CUDA_SAFE_FREE(d_filterHelperDown);
	std::cout << "done!" << std::endl;
}

void TestMatching::filterBySurfaceArea(bool print /*= false*/, const std::string& outDir /*= ""*/)
{
	MLIB_ASSERT(m_stage == FILTERED_IMPAIR_MATCHES && !m_cachedFrames.empty());
	MLIB_ASSERT(!print || !outDir.empty());
	std::cout << "filtering by SA... " << std::endl;
	Timer t;

	unsigned int numImages = m_siftManager->getNumImages();
	for (unsigned int cur = 1; cur < numImages; cur++) {
		std::cout << cur << " ";

		// copy respective matches
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(m_siftManager->d_currNumFilteredMatchesPerImagePair, getNumMatchesCUDA(cur, true), sizeof(int)*cur, cudaMemcpyDeviceToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(m_siftManager->d_currFilteredMatchDistances, getMatchDistsCUDA(cur, true), sizeof(float)*cur*MAX_MATCHES_PER_IMAGE_PAIR_RAW, cudaMemcpyDeviceToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(m_siftManager->d_currFilteredMatchKeyPointIndices, getMatchKeyIndicesCUDA(cur, true), sizeof(uint2)*cur*MAX_MATCHES_PER_IMAGE_PAIR_RAW, cudaMemcpyDeviceToDevice));

		SIFTMatchFilter::filterBySurfaceArea(m_siftManager, m_cachedFrames, MatrixConversion::toCUDA(m_colorCalibration.m_IntrinsicInverse), GlobalBundlingState::get().s_minNumMatchesGlobal);

		// print matches
		if (print) SiftVisualization::printMatches(outDir, m_siftManager, m_colorImages, cur, true);

		// to have for debug printing
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(getNumMatchesCUDA(cur, true), m_siftManager->d_currNumFilteredMatchesPerImagePair, sizeof(int)*cur, cudaMemcpyDeviceToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(getMatchDistsCUDA(cur, true), m_siftManager->d_currFilteredMatchDistances, sizeof(float)*cur*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED, cudaMemcpyDeviceToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(getMatchKeyIndicesCUDA(cur, true), m_siftManager->d_currFilteredMatchKeyPointIndices, sizeof(uint2)*cur*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED, cudaMemcpyDeviceToDevice));
	} // cur frames

	t.stop();
	std::cout << std::endl << "done! (" << t.getElapsedTimeMS() << " ms)" << std::endl;

	m_stage = FILTERED_SA;
}

void TestMatching::filterByDenseVerify(bool print /*= false*/, const std::string& outDir /*= ""*/)
{
	MLIB_ASSERT(m_stage == FILTERED_SA && !m_cachedFrames.empty());
	MLIB_ASSERT(!print || !outDir.empty());
	std::cout << "filtering by DV... " << std::endl;
	Timer t;

	unsigned int numImages = m_siftManager->getNumImages();
	for (unsigned int cur = 1; cur < numImages; cur++) {
		std::cout << cur << " ";

		// copy respective matches
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(m_siftManager->d_currNumFilteredMatchesPerImagePair, getNumMatchesCUDA(cur, true), sizeof(int)*cur, cudaMemcpyDeviceToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(m_siftManager->d_currFilteredMatchDistances, getMatchDistsCUDA(cur, true), sizeof(float)*cur*MAX_MATCHES_PER_IMAGE_PAIR_RAW, cudaMemcpyDeviceToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(m_siftManager->d_currFilteredMatchKeyPointIndices, getMatchKeyIndicesCUDA(cur, true), sizeof(uint2)*cur*MAX_MATCHES_PER_IMAGE_PAIR_RAW, cudaMemcpyDeviceToDevice));

		//if (cur == 7) {
		//	SIFTMatchFilter::visualizeProjError(m_siftManager, vec2ui(0, cur), m_cachedFrames, MatrixConversion::toCUDA(m_intrinsicsDownsampled), 0.1f, 3.0f);
		//	std::cout << "waiting..." << std::endl;
		//	getchar();
		//}

		SIFTMatchFilter::filterByDenseVerify(m_siftManager, m_cachedFrames, MatrixConversion::toCUDA(m_intrinsicsDownsampled), 0.1f, 3.0f);//GlobalAppState::get().s_sensorDepthMin, GlobalAppState::get().s_sensorDepthMax);

		// print matches
		if (print) SiftVisualization::printMatches(outDir, m_siftManager, m_colorImages, cur, true);

		// to have for debug printing
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(getNumMatchesCUDA(cur, true), m_siftManager->d_currNumFilteredMatchesPerImagePair, sizeof(int)*cur, cudaMemcpyDeviceToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(getMatchDistsCUDA(cur, true), m_siftManager->d_currFilteredMatchDistances, sizeof(float)*cur*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED, cudaMemcpyDeviceToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(getMatchKeyIndicesCUDA(cur, true), m_siftManager->d_currFilteredMatchKeyPointIndices, sizeof(uint2)*cur*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED, cudaMemcpyDeviceToDevice));
	} // cur frames

	t.stop();
	std::cout << std::endl << "done! (" << t.getElapsedTimeMS() << " ms)" << std::endl;

	m_stage = FILTERED_DV;
}

//void TestMatching::matchFrame(unsigned int frame, bool print, bool checkReference)
//{
//	if (m_colorImages.front().getWidth() != m_widthSift) {
//		std::cout << "ERROR haven't implemented the resampling for color/sift res" << std::endl;
//		while (1);
//	}
//
//	SiftMatchGPU* siftMatcher = new SiftMatchGPU(GlobalBundlingState::get().s_maxNumKeysPerImage);
//	siftMatcher->InitSiftMatch();
//	const float ratioMax = GlobalBundlingState::get().s_siftMatchRatioMaxGlobal;
//	const float matchThresh = GlobalBundlingState::get().s_siftMatchThresh;
//	unsigned int numImages = m_siftManager->getNumImages();
//	const bool filtered = false;
//	const unsigned int minNumMatches = GlobalBundlingState::get().s_minNumMatchesGlobal;
//
//	std::cout << "matching frame " << frame << "... ";
//	Timer t;
//
//	// match frame cur to all previous
//	SIFTImageGPU& curImage = m_siftManager->getImageGPU(frame);
//	int num2 = (int)m_siftManager->getNumKeyPointsPerImage(frame);
//	if (num2 == 0) {
//		std::cout << "no keypoints for frame " << frame << "!" << std::endl;
//		SAFE_DELETE(siftMatcher); return;
//	}
//
//	// match to all previous
//	for (unsigned int prev = 0; prev < frame; prev++) {
//		SIFTImageGPU& prevImage = m_siftManager->getImageGPU(prev);
//		int num1 = (int)m_siftManager->getNumKeyPointsPerImage(prev);
//		if (num1 == 0) {
//			MLIB_CUDA_SAFE_CALL(cudaMemset(m_siftManager->d_currNumMatchesPerImagePair + prev, 0, sizeof(int)));
//			continue;
//		}
//
//		uint2 keyPointOffset = make_uint2(0, 0);
//		ImagePairMatch& imagePairMatch = m_siftManager->getImagePairMatchDEBUG(prev, frame, keyPointOffset);
//
//		siftMatcher->SetDescriptors(0, num1, (unsigned char*)prevImage.d_keyPointDescs);
//		siftMatcher->SetDescriptors(1, num2, (unsigned char*)curImage.d_keyPointDescs);
//		siftMatcher->GetSiftMatch(num1, imagePairMatch, keyPointOffset, matchThresh, ratioMax);
//	}  // prev frames
//
//	t.stop();
//	std::cout << "done! (" << t.getElapsedTimeMS() << " ms)" << std::endl;
//
//	// print matches
//	//if (print) SiftVisualization::printMatches("debug/", m_siftManager, m_colorImages, frame, false);
//
//	SAFE_DELETE(siftMatcher);
//	// debug output
//	std::vector<unsigned int> numMatchesRaw; unsigned int countRawMatch = 0;
//	m_siftManager->getNumRawMatchesDEBUG(numMatchesRaw);
//	for (unsigned int i = 0; i < numMatchesRaw.size(); i++) {
//		if (numMatchesRaw[i] > 0) countRawMatch++;
//	}
//	std::cout << "RAW: found matches to " << countRawMatch << std::endl;
//
//	//filter
//	MLIB_ASSERT(frame == m_siftManager->getNumImages() - 1); // TODO make this a param
//	//SIFTMatchFilter::filterKeyPointMatches(m_siftManager, MatrixConversion::toCUDA(m_colorCalibration.m_IntrinsicInverse), minNumMatches);
//	m_siftManager->FilterKeyPointMatchesCU(frame, MatrixConversion::toCUDA(m_colorCalibration.m_IntrinsicInverse),
//		GlobalBundlingState::get().s_minNumMatchesGlobal, GlobalBundlingState::get().s_maxKabschResidual2, false);
//
//	// debug output
//	std::vector<unsigned int> numMatchesFiltKey; unsigned int countFiltKeyMatch = 0;
//	m_siftManager->getNumFiltMatchesDEBUG(numMatchesFiltKey);
//	for (unsigned int i = 0; i < numMatchesFiltKey.size(); i++) {
//		if (numMatchesFiltKey[i] > 0) countFiltKeyMatch++;
//	}
//	std::cout << "FILT_KEY: found matches to " << countFiltKeyMatch << std::endl;
//
//	m_siftManager->FilterMatchesBySurfaceAreaCU(frame, MatrixConversion::toCUDA(m_colorCalibration.m_IntrinsicInverse), GlobalBundlingState::get().s_surfAreaPcaThresh);
//
//	// debug output
//	std::vector<unsigned int> numMatchesFiltSA; unsigned int countFiltSAMatch = 0;
//	m_siftManager->getNumFiltMatchesDEBUG(numMatchesFiltSA);
//	for (unsigned int i = 0; i < numMatchesFiltSA.size(); i++) {
//		if (numMatchesFiltSA[i] > 0) countFiltSAMatch++;
//	}
//	std::cout << "FILT_SA: found matches to " << countFiltSAMatch << std::endl;
//
//	//{ //!!!DEBUGGING
//	//	vec2ui imageIndices(0, frame);
//	//	std::vector<float4x4> transforms(frame); MLIB_CUDA_SAFE_CALL(cudaMemcpy(transforms.data(), m_siftManager->getFiltTransformsDEBUG(), sizeof(float4x4)*frame, cudaMemcpyDeviceToHost));
//	//	float4x4 transformPrvToCur = transforms[imageIndices.x];
//	//	SiftVisualization::saveImPairToPointCloud("debug/", m_cachedFrames, GlobalBundlingState::get().s_downsampledWidth, GlobalBundlingState::get().s_downsampledHeight,
//	//		imageIndices, MatrixConversion::toMlib(transformPrvToCur));
//	//	SIFTMatchFilter::visualizeProjError(m_siftManager, imageIndices, m_cachedFrames, MatrixConversion::toCUDA(m_intrinsicsDownsampled),
//	//		transformPrvToCur.getInverse(), 0.1f, 3.0f);
//	//}
//
//	m_siftManager->FilterMatchesByDenseVerifyCU(frame, GlobalBundlingState::get().s_downsampledWidth, GlobalBundlingState::get().s_downsampledHeight,
//		MatrixConversion::toCUDA(m_intrinsicsDownsampled), d_cachedFrames, GlobalBundlingState::get().s_projCorrDistThres, GlobalBundlingState::get().s_projCorrNormalThres,
//		GlobalBundlingState::get().s_projCorrColorThresh, GlobalBundlingState::get().s_verifySiftErrThresh, GlobalBundlingState::get().s_verifySiftCorrThresh,
//		//GlobalAppState::get().s_sensorDepthMin, GlobalAppState::get().s_sensorDepthMax); //TODO PARAMS
//		0.1f, 3.0f);
//
//	if (checkReference) {
//		std::cout << "CHECKING MATCHES WITH REFERENCE TRAJECTORY..." << std::endl;
//		// check with trajectory
//		const float maxResidualThres2 = 0.05 * 0.05;
//
//		// get data
//		std::vector<SIFTKeyPoint> keys;
//		keys.resize(m_siftManager->m_numKeyPoints);
//		MLIB_CUDA_SAFE_CALL(cudaMemcpy(keys.data(), m_siftManager->d_keyPoints, sizeof(SIFTKeyPoint) * keys.size(), cudaMemcpyDeviceToHost));
//
//		std::vector<unsigned int> numMatchesFilt; std::vector<float> matchDistsFilt; std::vector<uint2> matchKeyIndicesFilt;
//		m_siftManager->getNumFiltMatchesDEBUG(numMatchesFilt);
//		unsigned int countMatchFrames = 0;
//		for (unsigned int i = 0; i < numMatchesFilt.size(); i++) {
//			if (numMatchesFilt[i] > 0) countMatchFrames++;
//		}
//		std::cout << "\tfound matches to " << countMatchFrames << std::endl;
//
//		// compare to reference trajectory
//		for (unsigned int i = 0; i < numMatchesFilt.size(); i++) {
//			const vec2ui imageIndices(i, frame);
//			bool ok = true;
//			if (m_referenceTrajectory[imageIndices.x][0] == -std::numeric_limits<float>::infinity() ||
//				m_referenceTrajectory[imageIndices.y][0] == -std::numeric_limits<float>::infinity()) {
//				std::cout << "warning: no reference trajectory for " << imageIndices << std::endl;
//				ok = false; // just classify as bad
//			}
//			const mat4f refTransform = m_referenceTrajectory[imageIndices.y].getInverse() * m_referenceTrajectory[imageIndices.x]; // src to tgt
//
//			std::vector<vec3f> srcPts(MAX_MATCHES_PER_IMAGE_PAIR_FILTERED), tgtPts(MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
//
//			m_siftManager->getFiltKeyPointIndicesAndMatchDistancesDEBUG(i, matchKeyIndicesFilt, matchDistsFilt);
//			getSrcAndTgtPts(keys.data(), matchKeyIndicesFilt.data(), numMatchesFilt[i],
//				(float3*)srcPts.data(), (float3*)tgtPts.data(), MatrixConversion::toCUDA(m_colorCalibration.m_IntrinsicInverse));
//			float maxRes = 0.0f;
//			for (unsigned int m = 0; m < numMatchesFilt[i]; m++) {
//				vec3f d = refTransform * srcPts[m] - tgtPts[m];
//				float res = d | d;
//				if (res > maxRes) maxRes = res;
//			}
//			if (maxRes > maxResidualThres2) { // bad
//				SiftVisualization::printMatch(m_siftManager, "debug/" + std::to_string(i) + "-" + std::to_string(frame) + ".png",
//					imageIndices, m_colorImages[i], m_colorImages[frame], GlobalBundlingState::get().s_siftMatchThresh, true, -1);
//			}
//		}
//	}
//	std::cout << "DONE" << std::endl;
//}
//
//
//void TestMatching::debugOptimizeGlobal()
//{
//	unsigned int numFrames = m_siftManager->getNumImages();
//	unsigned int lastFrame = numFrames - 1;
//
//	const std::string initialTrajectoryFile = "debug/test208.trajectory";
//	std::cout << "using initial trajectory: " << initialTrajectoryFile << std::endl;
//	std::vector<mat4f> globTrajectory;
//	{
//		BinaryDataStreamFile s(initialTrajectoryFile, false);
//		s >> globTrajectory;
//	}
//	MLIB_ASSERT(globTrajectory.size() == m_siftManager->getNumImages());
//
//	// trajectory
//	float4x4* d_globalTrajectory = NULL;
//	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_globalTrajectory, sizeof(float4x4)*m_siftManager->getNumImages()));
//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_globalTrajectory, globTrajectory.data(), sizeof(float4x4)*globTrajectory.size(), cudaMemcpyHostToDevice));
//
//	//std::cout << "saving init to point cloud... ";
//	//SiftVisualization::saveToPointCloud("debug/init.ply", m_depthImages, m_colorImages, globTrajectory, m_depthCalibration.m_IntrinsicInverse);
//	//std::cout << "done!" << std::endl;
//
//	const unsigned int numNonLinIters = 4;
//	const unsigned int numLinIters = 100;
//
//	// optimize
//	SBA bundler;
//	const unsigned int maxNumImages = GlobalBundlingState::get().s_maxNumImages;
//	const unsigned int maxNumResiduals = MAX_MATCHES_PER_IMAGE_PAIR_FILTERED * (maxNumImages*(maxNumImages - 1)) / 2;
//	bundler.init(m_siftManager->getNumImages(), maxNumResiduals);
//	const bool useVerify = false;
//
//	bool isStart = true;
//	bool isEnd = true;
//	bool isScanDone = false;
//	for (unsigned int i = 0; i < 10; i++) {
//		bundler.align(m_siftManager, NULL, d_globalTrajectory, numNonLinIters, numLinIters,
//			useVerify, false, false, isStart, isEnd, isScanDone);
//
//		MLIB_CUDA_SAFE_CALL(cudaMemcpy(globTrajectory.data(), d_globalTrajectory, sizeof(mat4f)*numFrames, cudaMemcpyDeviceToHost));
//
//		//if (i % 10 == 0) {
//		std::cout << "saving opt " << i << " to point cloud... ";
//		SiftVisualization::saveToPointCloud("debug/opt" + std::to_string(i) + ".ply", m_depthImages, m_colorImages, globTrajectory, m_depthCalibration.m_IntrinsicInverse);
//		std::cout << "done!" << std::endl;
//		//}
//	}
//
//	MLIB_CUDA_SAFE_FREE(d_globalTrajectory);
//
//	int a = 5;
//}
//
//void TestMatching::checkCorrespondences()
//{
//	MLIB_ASSERT(!m_referenceTrajectory.empty());
//	const std::string outDir = "debug/_checkCorr/"; if (!util::directoryExists(outDir)) util::makeDirectory(outDir);
//	const std::string inspectDir = "debug/_checkCorr/inspect/"; if (!util::directoryExists(inspectDir)) util::makeDirectory(inspectDir);
//
//	// debug hack
//	m_siftManager->AddCurrToResidualsCU(m_siftManager->getNumImages() - 1, MatrixConversion::toCUDA(m_colorCalibration.m_IntrinsicInverse));
//
//	std::cout << "CHECKING MATCHES WITH REFERENCE TRAJECTORY..." << std::endl;
//	// check with trajectory
//	const float maxResidualThres2 = 0.05f * 0.05f;
//	const float inspectMaxResThresh2 = 0.1f * 0.1f;
//	const unsigned int numImages = m_siftManager->getNumImages();
//
//	// get data
//	unsigned int numCorr = m_siftManager->getNumGlobalCorrespondences();
//	std::vector<EntryJ> correspondences(numCorr);
//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(correspondences.data(), m_siftManager->getGlobalCorrespondencesDEBUG(), sizeof(EntryJ)*numCorr, cudaMemcpyDeviceToHost));
//	std::cout << "#corr = " << numCorr << std::endl;
//
//	unsigned int badCount = 0;
//
//	// compare to reference trajectory
//	for (unsigned int i = 0; i < correspondences.size(); i++) {
//		const EntryJ& corr = correspondences[i];
//		if (!corr.isValid()) continue;
//
//		const vec2ui imageIndices(corr.imgIdx_i, corr.imgIdx_j);
//		bool ok = true;
//		if (m_referenceTrajectory[imageIndices.x][0] == -std::numeric_limits<float>::infinity() ||
//			m_referenceTrajectory[imageIndices.y][0] == -std::numeric_limits<float>::infinity()) {
//			std::cout << "warning: no reference trajectory for " << imageIndices << std::endl;
//			ok = false; // just classify as bad
//		}
//		else {
//			const mat4f refTransform = m_referenceTrajectory[imageIndices.y].getInverse() * m_referenceTrajectory[imageIndices.x]; // src to tgt
//
//			vec3f src(corr.pos_i.x, corr.pos_i.y, corr.pos_i.z);
//			vec3f tgt(corr.pos_j.x, corr.pos_j.y, corr.pos_j.z);
//			vec3f d = refTransform * src - tgt;
//			float res = d | d;
//
//			if (res > maxResidualThres2) { // bad
//				const std::string dir = (res > inspectMaxResThresh2) ? inspectDir : outDir;
//				SiftVisualization::printMatch(dir + std::to_string(imageIndices.x) + "-" + std::to_string(imageIndices.y) + ".png",
//					corr, m_colorImages[imageIndices.x], m_colorImages[imageIndices.y], m_colorCalibration.m_Intrinsic);
//				ok = false;
//			}
//		}
//		if (!ok) badCount++;
//	}
//	std::cout << "found " << badCount << " bad correspondences" << std::endl;
//
//	// check if missing links
//	std::vector<unsigned int> numMatchesToPrev(numImages, 0);
//	for (unsigned int i = 0; i < correspondences.size(); i++) {
//		const EntryJ& corr = correspondences[i];
//		if (!corr.isValid()) continue;
//		MLIB_ASSERT(corr.imgIdx_i < corr.imgIdx_j);
//		numMatchesToPrev[corr.imgIdx_i]++;
//	}
//	for (unsigned int i = 1; i < numMatchesToPrev.size(); i++) {
//		if (numMatchesToPrev[i] == 0) {
//			std::cout << "image " << i << " has no matches to previous!" << std::endl;
//		}
//	}
//	std::cout << "DONE" << std::endl;
//}
//
//void TestMatching::printKeys()
//{
//	const std::string outDir = "debug/keys/"; if (!util::directoryExists(outDir)) util::makeDirectory(outDir);
//	const unsigned int numImages = m_siftManager->getNumImages();
//
//	for (unsigned int i = 0; i < numImages; i++) {
//		SiftVisualization::printKey(outDir + std::to_string(i) + ".png", m_colorImages[i], m_siftManager, i);
//	}
//	std::cout << "DONE" << std::endl;
//}
//
//void TestMatching::debugMatchInfo()
//{
//	const std::string outDir = "debug/_checkCorr/inspect/full/";
//	if (!util::directoryExists(outDir)) util::makeDirectory(outDir);
//
//	const std::vector<vec2ui> imagePairs = { vec2ui(39, 207), vec2ui(53, 206), vec2ui(55, 206), vec2ui(58, 206), vec2ui(59, 207) };
//	std::unordered_map<vec2ui, unsigned int> imagePairSet;
//	for (unsigned int i = 0; i < imagePairs.size(); i++) imagePairSet[imagePairs[i]] = i;
//
//	std::vector<std::vector<float3>> imPairSrcPts(imagePairs.size());
//	std::vector<std::vector<float3>> imPairTgtPts(imagePairs.size());
//
//	// collect matches for image pairs from corr array
//	unsigned int numCorr = m_siftManager->getNumGlobalCorrespondences();
//	std::vector<EntryJ> correspondences(numCorr);
//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(correspondences.data(), m_siftManager->getGlobalCorrespondencesDEBUG(), sizeof(EntryJ)*numCorr, cudaMemcpyDeviceToHost));
//	std::cout << "#corr = " << numCorr << std::endl;
//
//	for (unsigned int i = 0; i < correspondences.size(); i++) {
//		const EntryJ& corr = correspondences[i];
//		if (!corr.isValid()) continue;
//		vec2ui imageIndices(corr.imgIdx_i, corr.imgIdx_j);
//		auto a = imagePairSet.find(imageIndices);
//		if (a != imagePairSet.end()) {
//			unsigned int index = a->second;
//			imPairSrcPts[index].push_back(corr.pos_i);
//			imPairTgtPts[index].push_back(corr.pos_j);
//		}
//	}
//
//	// todo fix this hack
//}

void TestMatching::runOpt()
{
	MLIB_ASSERT(!m_colorImages.empty() && !m_cachedFrames.empty());

	//params
	const bool savePointClouds = false;
	const unsigned int maxNumIters = 4;

	//weights...
	//std::vector<float> weightsSparse(maxNumIters, 1.0f);
	std::vector<float> weightsSparse(maxNumIters, 0.0f);
	//std::vector<float> weightsDenseDepth(maxNumIters, 0.0f);
	//std::vector<float> weightsDenseDepth(maxNumIters, 0.5f);
	//std::vector<float> weightsDenseDepth(maxNumIters, 0.0f); for (unsigned int i = 0; i < maxNumIters; i++) weightsDenseDepth[i] = i + 1.0f;
	std::vector<float> weightsDenseDepth(maxNumIters, 1.0f);
	std::vector<float> weightsDenseColor(maxNumIters, 0.0f);
	//std::vector<float> weightsDenseColor(maxNumIters, 0.0f); for (unsigned int i = 0; i < maxNumIters; i++) weightsDenseColor[i] = (i + 1.0f) * 0.1f;
	//std::vector<float> weightsDenseColor(maxNumIters, 0.0f);	for (unsigned int i = 1; i < maxNumIters; i++) weightsDenseColor[i] = (i + 1.0f) * 5.0f;
	bool useGlobalDense = true;
	//bool useGlobalDense = false;

	const unsigned int numImages = (unsigned int)m_colorImages.size();

	//const std::string refFilename = "E:/Work/VolumetricSFS/tracking/ICLNUIM/livingRoom1.gt.freiburg"; std::vector<mat4f> referenceTrajectory;
	//std::cout << "loading reference trajectory from " << refFilename << "... "; loadTrajectory(refFilename, referenceTrajectory); std::cout << "done" << std::endl;
	//referenceTrajectory.resize(numImages);
	std::vector<mat4f> referenceTrajectory = m_referenceTrajectory;
	mat4f offset = referenceTrajectory.front().getInverse();
	for (unsigned int i = 0; i < referenceTrajectory.size(); i++) referenceTrajectory[i] = offset * referenceTrajectory[i];
	if (savePointClouds) {
		std::cout << "saving ref to point cloud... "; SiftVisualization::saveToPointCloud("debug/ref.ply", m_depthImages, m_colorImages, referenceTrajectory, m_depthCalibration.m_IntrinsicInverse); std::cout << "done" << std::endl;
		//SiftVisualization::saveCamerasToPLY("debug/refCameras.ply", referenceTrajectory);
	}
	//create cache
	CUDACache cudaCache(m_depthImages.front().getWidth(), m_depthImages.front().getHeight(), GlobalBundlingState::get().s_downsampledWidth, GlobalBundlingState::get().s_downsampledHeight, numImages, m_intrinsicsDownsampled);
	cudaCache.setCachedFrames(m_cachedFrames);

	//TODO incorporate valid images
	if (weightsSparse.front() > 0.0f) {
		constructSparseSystem(m_colorImages, m_depthImages, m_siftManager, &cudaCache);
	}
	else {
		// fake images
		for (unsigned int i = 0; i < numImages; i++) {
			SIFTImageGPU& cur = m_siftManager->createSIFTImageGPU();
			m_siftManager->finalizeSIFTImageGPU(0);
		}
		m_siftManager->setValidImagesDEBUG(std::vector<int>(numImages, 1));
	}

	// run opt
	SBA sba;
	const unsigned int maxNumImages = GlobalBundlingState::get().s_maxNumImages;
	const unsigned int maxNumResiduals = MAX_MATCHES_PER_IMAGE_PAIR_FILTERED * (maxNumImages*(maxNumImages - 1)) / 2;
	sba.init(numImages, maxNumResiduals);
	sba.setGlobalWeights(weightsSparse, weightsDenseDepth, weightsDenseColor, useGlobalDense);
	// params
	const unsigned int numPCGIts = 50;
	const bool useVerify = true;

	// initial transforms
	std::vector<mat4f> transforms(numImages, mat4f::identity());
	// rand init
	RNG::global.init(0, 1, 2, 3); /*const float maxTrans = 0.1f; const float maxRot = 7.0f;*/ const float maxTrans = 0.05f; const float maxRot = 5.0f; //for rgb only convergence radius not so large
	for (unsigned int i = 1; i < transforms.size(); i++) {
		float tx = RNG::global.uniform(0.0f, maxTrans); if (RNG::global.uniform(0, 1) == 0) tx = -tx;
		float ty = RNG::global.uniform(0.0f, maxTrans); if (RNG::global.uniform(0, 1) == 0) ty = -ty;
		float tz = RNG::global.uniform(0.0f, maxTrans); if (RNG::global.uniform(0, 1) == 0) ty = -ty;
		float rx = RNG::global.uniform(0.0f, maxRot); if (RNG::global.uniform(0, 1) == 0) rx = -rx;
		float ry = RNG::global.uniform(0.0f, maxRot); if (RNG::global.uniform(0, 1) == 0) ry = -ry;
		float rz = RNG::global.uniform(0.0f, maxRot); if (RNG::global.uniform(0, 1) == 0) ry = -ry;
		transforms[i] = mat4f::translation(tx, ty, tz) * mat4f::rotationZ(rz) * mat4f::rotationY(ry) * mat4f::rotationX(rx);
	}
	if (savePointClouds) {
		std::cout << "saving init to point cloud... "; SiftVisualization::saveToPointCloud("debug/init.ply", m_depthImages, m_colorImages, transforms, m_depthCalibration.m_IntrinsicInverse); std::cout << "done" << std::endl;
	}

	float4x4* d_transforms = NULL; MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_transforms, sizeof(float4x4)*numImages));
	//{//evaluate at ground truth
	//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_transforms, referenceTrajectory.data(), sizeof(float4x4)*numImages, cudaMemcpyHostToDevice));
	//	sba.setGlobalWeights(std::vector<float>(maxNumIters, 1.0f), std::vector<float>(maxNumIters, 1.0f), std::vector<float>(maxNumIters, 1.0f), true);
	//	sba.align(m_siftManager, &cudaCache, d_transforms, 1, 1, useVerify, false, false, true, false, false);
	//	std::cout << "waiting..." << std::endl; getchar();
	//}
	{//start at ground truth
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_transforms, referenceTrajectory.data(), sizeof(float4x4)*numImages, cudaMemcpyHostToDevice));
		sba.setGlobalWeights(std::vector<float>(maxNumIters, 0.0f), std::vector<float>(maxNumIters, 1.0f), std::vector<float>(maxNumIters, 0.0f), true);
		sba.align(m_siftManager, &cudaCache, d_transforms, 4, numPCGIts, useVerify, false, false, true, false, false);
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(transforms.data(), d_transforms, sizeof(float4x4)*numImages, cudaMemcpyDeviceToHost));
		const auto transErr = PoseHelper::evaluateAteRmse(transforms, referenceTrajectory);
		std::cout << "ate rmse = " << transErr.first << ", " << transErr.second << std::endl;
		std::cout << "waiting..." << std::endl; getchar();
	}

	MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_transforms, transforms.data(), sizeof(float4x4)*numImages, cudaMemcpyHostToDevice));
	////sparse only first
	//sba.setGlobalWeights(std::vector<float>(maxNumIters, 1.0f), std::vector<float>(maxNumIters, 0.0f), std::vector<float>(maxNumIters, 0.0f), false);
	//sba.align(m_siftManager, &cudaCache, d_transforms, 2, numPCGIts, useVerify, false, false, true, true, false);
	//{
	//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(transforms.data(), d_transforms, sizeof(float4x4)*numImages, cudaMemcpyDeviceToHost));
	//const auto transErr = PoseHelper::evaluateAteRmse(transforms, referenceTrajectory);
	//std::cout << "ate rmse = " << transErr.first << ", " << transErr.second << std::endl;
	//}
	//sparse + dense depth + dense color
	sba.setGlobalWeights(weightsSparse, weightsDenseDepth, weightsDenseColor, true);
	sba.align(m_siftManager, &cudaCache, d_transforms, maxNumIters, numPCGIts, useVerify, false, false, true, true, false);
	{
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(transforms.data(), d_transforms, sizeof(float4x4)*numImages, cudaMemcpyDeviceToHost));
		const auto transErr = PoseHelper::evaluateAteRmse(transforms, referenceTrajectory);
		std::cout << "ate rmse = " << transErr.first << ", " << transErr.second << std::endl;
	}

	MLIB_CUDA_SAFE_CALL(cudaMemcpy(transforms.data(), d_transforms, sizeof(float4x4)*numImages, cudaMemcpyDeviceToHost));

	// compare to reference trajectory		
	const auto transErr = PoseHelper::evaluateAteRmse(transforms, referenceTrajectory);
	std::cout << "*********************************" << std::endl;
	std::cout << " ate rmse = " << transErr.first << ", " << transErr.second << std::endl;
	std::cout << "*********************************" << std::endl;

	if (savePointClouds) {
		std::cout << "saving opt to point cloud... "; SiftVisualization::saveToPointCloud("debug/opt.ply", m_depthImages, m_colorImages, transforms, m_depthCalibration.m_IntrinsicInverse, false); std::cout << "done" << std::endl;
	}

	MLIB_CUDA_SAFE_FREE(d_transforms);
	int a = 5;
}

void TestMatching::printCacheFrames(const std::string& dir, const CUDACache* cache /*= NULL*/, unsigned int numPrint /*= (unsigned int)-1*/) const
{
	const std::vector<CUDACachedFrame>& cachedFrames = cache ? cache->getCacheFrames() : m_cachedFrames;
	if (cachedFrames.empty()) {
		std::cout << "no cached frames to print!" << std::endl;
		return;
	}
	if (numPrint == (unsigned int)-1) numPrint = (unsigned int)cachedFrames.size();
	std::cout << "printing cached frames... ";
	unsigned int width = GlobalBundlingState::get().s_downsampledWidth;
	unsigned int height = GlobalBundlingState::get().s_downsampledHeight;
	DepthImage32 depthImage(width, height); ColorImageR32 intensityImage(width, height); BaseImage<vec2f> intensityDerivImage(width, height);
	ColorImageR32G32B32A32 camPosImage(width, height), normalImage(width, height);
	ColorImageR32 dx(width, height), dy(width, height);
	for (unsigned int i = 0; i < numPrint; i++) {
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(depthImage.getData(), cachedFrames[i].d_depthDownsampled, sizeof(float)*width*height, cudaMemcpyDeviceToHost));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(camPosImage.getData(), cachedFrames[i].d_cameraposDownsampled, sizeof(float4)*width*height, cudaMemcpyDeviceToHost));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(normalImage.getData(), cachedFrames[i].d_normalsDownsampled, sizeof(float4)*width*height, cudaMemcpyDeviceToHost));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(intensityImage.getData(), cachedFrames[i].d_intensityDownsampled, sizeof(float)*width*height, cudaMemcpyDeviceToHost));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(intensityDerivImage.getData(), cachedFrames[i].d_intensityDerivsDownsampled, sizeof(float2)*width*height, cudaMemcpyDeviceToHost));
		intensityImage.setInvalidValue(-std::numeric_limits<float>::infinity());
		dx.setInvalidValue(-std::numeric_limits<float>::infinity());
		dy.setInvalidValue(-std::numeric_limits<float>::infinity());

		//debug check, save to point cloud
		for (unsigned int k = 0; k < width*height; k++) {
			normalImage.getData()[k] = (normalImage.getData()[k] + 1.0f) / 2.0f;
			normalImage.getData()[k].w = 1.0f; // make visible
		}

		FreeImageWrapper::saveImage(dir + std::to_string(i) + "_depth.png", ColorImageR32G32B32(depthImage));
		FreeImageWrapper::saveImage(dir + std::to_string(i) + "_camPos.png", camPosImage);
		FreeImageWrapper::saveImage(dir + std::to_string(i) + "_normal.png", normalImage);
		FreeImageWrapper::saveImage(dir + std::to_string(i) + "_intensity.png", intensityImage);

		for (unsigned int p = 0; p < intensityDerivImage.getNumPixels(); p++) {
			const vec2f& d = intensityDerivImage.getData()[p];
			dx.getData()[p] = d.x; dy.getData()[p] = d.y;
		}
		FreeImageWrapper::saveImage(dir + std::to_string(i) + "_intensityDerivX.png", dx);
		FreeImageWrapper::saveImage(dir + std::to_string(i) + "_intensityDerivY.png", dy);
	}
	std::cout << "done!" << std::endl;
}

void TestMatching::constructSparseSystem(const std::vector<ColorImageR8G8B8> &colorImages, const std::vector<DepthImage32> &depthImages,
	SIFTImageManager *siftManager, const CUDACache* cudaCache)
{
	//init keys
	SiftGPU* sift = new SiftGPU;
	sift->SetParams(m_widthSift, m_heightSift, false, 150, GlobalAppState::get().s_sensorDepthMin, GlobalAppState::get().s_sensorDepthMax);
	sift->InitSiftGPU();
	//init matcher
	SiftMatchGPU* siftMatcher = new SiftMatchGPU(GlobalBundlingState::get().s_maxNumKeysPerImage);
	siftMatcher->InitSiftMatch();
	const float ratioMax = GlobalBundlingState::get().s_siftMatchRatioMaxGlobal;
	const float matchThresh = GlobalBundlingState::get().s_siftMatchThresh;
	unsigned int numImages = siftManager->getNumImages();
	const bool filtered = false;
	const unsigned int minNumMatches = GlobalBundlingState::get().s_minNumMatchesGlobal;


	const unsigned int numTotalFrames = (unsigned int)colorImages.size();
	float *d_intensity = NULL, *d_depth = NULL;
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_intensity, sizeof(float)*m_widthSift*m_heightSift));
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_depth, sizeof(float)*m_widthDepth*m_heightDepth));

	const float4x4 siftIntrinsicsInv = MatrixConversion::toCUDA(m_colorCalibration.m_IntrinsicInverse);

	//TODO incorporate invalid
	for (unsigned int curFrame = 0; curFrame < numTotalFrames; curFrame++) {
		ColorImageR32 intensity = convertToGrayScale(colorImages[curFrame]);
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_intensity, intensity.getData(), sizeof(float)*m_widthSift*m_heightSift, cudaMemcpyHostToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_depth, depthImages[curFrame].getData(), sizeof(float)*m_widthDepth*m_heightDepth, cudaMemcpyHostToDevice));
		// detect keys
		SIFTImageGPU& cur = siftManager->createSIFTImageGPU();
		int success = sift->RunSIFT(d_intensity, d_depth);
		if (!success) throw MLIB_EXCEPTION("Error running SIFT detection");
		unsigned int numKeypoints = sift->GetKeyPointsAndDescriptorsCUDA(cur, d_depth);
		siftManager->finalizeSIFTImageGPU(numKeypoints);
		std::cout << "\t" << curFrame << ": " << numKeypoints << " keys" << std::endl;

		//matching
		if (curFrame > 0) {
			if (numKeypoints == 0) {
				MLIB_CUDA_SAFE_CALL(cudaMemset(siftManager->d_currNumMatchesPerImagePair, 0, sizeof(int)*curFrame));
				siftManager->invalidateFrame(curFrame);
				continue;
			}
			for (unsigned int prev = 0; prev < curFrame; prev++) { //match to prev
				SIFTImageGPU& prevImage = siftManager->getImageGPU(prev);
				int num1 = (int)siftManager->getNumKeyPointsPerImage(prev);
				if (num1 == 0) {
					MLIB_CUDA_SAFE_CALL(cudaMemset(siftManager->d_currNumMatchesPerImagePair + prev, 0, sizeof(int)));
					continue;
				}
				uint2 keyPointOffset = make_uint2(0, 0);
				ImagePairMatch& imagePairMatch = siftManager->getImagePairMatch(prev, curFrame, keyPointOffset);
				siftMatcher->SetDescriptors(0, num1, (unsigned char*)prevImage.d_keyPointDescs);
				siftMatcher->SetDescriptors(1, numKeypoints, (unsigned char*)cur.d_keyPointDescs);
				siftMatcher->GetSiftMatch(num1, imagePairMatch, keyPointOffset, matchThresh, ratioMax);

				//filter
				siftManager->SortKeyPointMatchesCU(curFrame, 0, numTotalFrames);
				siftManager->FilterKeyPointMatchesCU(curFrame, 0, numTotalFrames, siftIntrinsicsInv, minNumMatches,
					GlobalBundlingState::get().s_maxKabschResidual2);
				siftManager->FilterMatchesBySurfaceAreaCU(curFrame, 0, numTotalFrames, siftIntrinsicsInv, GlobalBundlingState::get().s_surfAreaPcaThresh);
				siftManager->FilterMatchesByDenseVerifyCU(curFrame, 0, numTotalFrames, cudaCache->getWidth(), cudaCache->getHeight(), MatrixConversion::toCUDA(cudaCache->getIntrinsics()),
					cudaCache->getCacheFramesGPU(), GlobalBundlingState::get().s_projCorrDistThres, GlobalBundlingState::get().s_projCorrNormalThres,
					GlobalBundlingState::get().s_projCorrColorThresh, GlobalBundlingState::get().s_verifySiftErrThresh, GlobalBundlingState::get().s_verifySiftCorrThresh,
					0.1f, 3.0f); //min/max
				siftManager->filterFrames(curFrame, 0, numTotalFrames);
				if (siftManager->getValidImages()[curFrame] != 0)
					siftManager->AddCurrToResidualsCU(curFrame, 0, numTotalFrames, siftIntrinsicsInv);
			}  // prev frames
		} //matching
	} //all frames

	//clean up
	SAFE_DELETE(siftMatcher);
	MLIB_CUDA_SAFE_FREE(d_intensity);
	MLIB_CUDA_SAFE_FREE(d_depth);
	SAFE_DELETE(sift);
	std::cout << "done!" << std::endl;
}

void TestMatching::analyzeLocalOpts()
{
	//TODO actually run the optimizations

	// find local where error(test0) > error(test1)
	//const std::string test0File = "dump/recording.sensor"; //sparse+dense
	//const std::string test1File = "dump/fr3_office.sensor"; //sparse
	//const std::string refFile   = "../data/tum/fr3_office_depth.sensor"; //reference
	const std::string test0File = "debug/opt_fr2_desk_sd.bin"; //sparse+dense
	const std::string test1File = "debug/opt_fr2_desk_s2.bin"; //sparse
	const std::string refFile = "debug/ref_fr2_desk.bin"; //reference

	std::cout << "loading trajectories... ";
	std::vector<mat4f> test0Trajectory, test1Trajectory, referenceTrajectory;
	if (util::getFileExtension(test0File) == "sensor") {
		CalibratedSensorData cs;
		BinaryDataStreamFile s(test0File, false);
		s >> cs; s.closeStream();
		test0Trajectory = cs.m_trajectory;
		BinaryDataStreamFile sout("debug/test0.trajectory", true);
		sout << test0Trajectory; sout.closeStream();
	}
	else {
		BinaryDataStreamFile s(test0File, false);
		s >> test0Trajectory;
	}
	if (util::getFileExtension(test1File) == "sensor") {
		CalibratedSensorData cs;
		BinaryDataStreamFile s(test1File, false);
		s >> cs; s.closeStream();
		test1Trajectory = cs.m_trajectory;
		BinaryDataStreamFile sout("debug/test1.trajectory", true);
		sout << test1Trajectory; sout.closeStream();
	}
	else {
		BinaryDataStreamFile s(test1File, false);
		s >> test1Trajectory;
	}
	if (util::getFileExtension(refFile) == "sensor") {
		CalibratedSensorData cs;
		BinaryDataStreamFile s(refFile, false);
		s >> cs; s.closeStream();
		referenceTrajectory = cs.m_trajectory;
		BinaryDataStreamFile sout("debug/ref.trajectory", true);
		sout << referenceTrajectory; sout.closeStream();
	}
	else {
		BinaryDataStreamFile s(refFile, false);
		s >> referenceTrajectory;
	}
	std::cout << "done!" << std::endl;
	size_t numTransforms = math::min(referenceTrajectory.size(), test0Trajectory.size());

	const unsigned int submapSize = GlobalBundlingState::get().s_submapSize;
	std::vector<float> errors0, errors1;
	std::vector<std::pair<unsigned int, float>> badIndices;
	std::vector<std::pair<unsigned int, float>> goodIndices;
	for (unsigned int i = 0; i < numTransforms; i += submapSize) {
		unsigned int numTransforms = std::min(submapSize, (int)referenceTrajectory.size() - i);
		std::vector<mat4f> local0(numTransforms), local1(numTransforms), refLocalTrajectory(numTransforms);
		mat4f offset0 = test0Trajectory[i].getInverse();
		mat4f offset1 = test1Trajectory[i].getInverse();
		mat4f refOffset = referenceTrajectory[i].getInverse();
		for (unsigned int t = 0; t < numTransforms; t++) {
			local0[t] = offset0 * test0Trajectory[i + t];
			local1[t] = offset1 * test1Trajectory[i + t];
			refLocalTrajectory[t] = refOffset * referenceTrajectory[i + t];
		}
		float err0 = PoseHelper::evaluateAteRmse(local0, refLocalTrajectory).first;
		errors0.push_back(err0);
		float err1 = PoseHelper::evaluateAteRmse(local1, refLocalTrajectory).first;
		errors1.push_back(err1);

		if (err0 > err1) badIndices.push_back(std::make_pair(i, err0 - err1));
		else goodIndices.push_back(std::make_pair(i, err1 - err0));
	}
	std::sort(badIndices.begin(), badIndices.end(), [](const std::pair<unsigned int, float> &left, const std::pair<unsigned int, float> &right) {
		return fabs(left.second) > fabs(right.second);
	});
	std::sort(goodIndices.begin(), goodIndices.end(), [](const std::pair<unsigned int, float> &left, const std::pair<unsigned int, float> &right) {
		return fabs(left.second) > fabs(right.second);
	});

	float totalErr0 = PoseHelper::evaluateAteRmse(test0Trajectory, referenceTrajectory).first;
	float totalErr1 = PoseHelper::evaluateAteRmse(test1Trajectory, referenceTrajectory).first;

	//compare global keys
	std::vector<mat4f> test0Keys, test1Keys, refKeys;
	for (unsigned int i = 0; i < numTransforms; i += submapSize) {
		test0Keys.push_back(test0Trajectory[i]);
		test1Keys.push_back(test1Trajectory[i]);
		refKeys.push_back(referenceTrajectory[i]);
	}
	float keysErr0 = PoseHelper::evaluateAteRmse(test0Keys, refKeys).first;
	float keysErr1 = PoseHelper::evaluateAteRmse(test1Keys, refKeys).first;

	std::cout << "sparse+dense:" << std::endl << "\ttotal error = " << totalErr0 << std::endl << "\tkeys error = " << keysErr0 << std::endl;
	std::cout << "sparse:" << std::endl << "\ttotal error = " << totalErr1 << std::endl << "\tkeys error = " << keysErr1 << std::endl;

	//compose trajectory
	std::vector<mat4f> composed;
	for (unsigned int i = 0; i < test0Trajectory.size(); i += submapSize) {
		unsigned int numTransforms = std::min(submapSize, (int)referenceTrajectory.size() - i);
		mat4f offset = test0Trajectory[i].getInverse();
		mat4f newOffset = test1Trajectory[i];
		for (unsigned int t = 0; t < numTransforms; t++) {
			mat4f trans = newOffset * offset * test0Trajectory[i + t];
			composed.push_back(trans);
		}
	}
	float compErr = PoseHelper::evaluateAteRmse(composed, referenceTrajectory).first;
	std::cout << "composed error = " << compErr << std::endl;

	std::sort(errors0.begin(), errors0.end(), std::greater<float>()); //sparse+dense
	std::sort(errors1.begin(), errors1.end(), std::greater<float>()); //sparse
	int a = 5;
}

void TestMatching::testGlobalDense()
{
	//{
	//	std::vector<mat4f> traj;
	//	BinaryDataStreamFile st("debug/opt_copy_s.bin", false);
	//	st >> traj; st.closeStream();
	//	SensorData data; data.readFromFile("../data/copyroom200/copyroom200.sens");
	//	if (data.m_frames.size() > traj.size()) data.m_frames.resize(traj.size());
	//	for (unsigned int i = 0; i < data.m_frames.size(); i++) {
	//		data.m_frames[i].m_frameToWorld = traj[i];
	//	}
	//	data.writeToFile("dump/recording.sens");
	//	std::cout << "done" << std::endl; getchar();
	//}

	//const std::string which = "fr1_desk_f20_3";//"fr1_desk_f20";
	//const std::string whichRef = "fr1_desk_from20";
	//const std::string which = "fr2_xyz";
	//const std::string whichRef = "fr2_xyz";
	//const std::string which = "half4";//"half3";//"half_i2";
	//const std::string whichRef = "fr2_xyz_half";
	//const std::string which = "fr3_office3";//"fr3_office_i3";//"fr3_office2";
	//const std::string whichRef = "fr3_office";
	//const std::string which = "fr3_nstn3";// "fr3_nstn2";
	//const std::string whichRef = "fr3_nstn";
	//const std::string origFile = "../data/tum/" + whichRef + ".sensor";

	//const std::string which = "liv3_sd";
	//const std::string whichRef = "livingroom3";
	//const std::string which = "liv2_sd";
	//const std::string whichRef = "livingroom2";
	//const std::string which = "liv1_sd";
	//const std::string whichRef = "livingroom1";
	//const std::string which = "liv0_sd";
	//const std::string whichRef = "livingroom0";
	//const std::string origFile = "../data/iclnuim/" + whichRef + ".sensor";

	const bool useReference = false;
	const bool writeSensorFile = true;

	//const std::string which = "gates371_sd";
	//const std::string which = "g0";
	//const std::string whichRef = "gates371";
	//const std::string origFile = "dump/" + whichRef + ".sens";

	const std::string which = "maryland_hotel1";
	const std::string whichRef = "maryland_hotel1";
	const std::string origFile = "../data/sun3d/" + whichRef + ".sens";

	bool loadCache = false;
	if (false && useReference) {
		std::cout << "check if need to update ref trajectory! (press key to continue)" << std::endl;
		getchar();

		CalibratedSensorData cs;
		BinaryDataStreamFile s(origFile, false);
		s >> cs; s.closeStream();
		BinaryDataStreamFile o("debug/ref_" + whichRef + ".bin", true);
		o << cs.m_trajectory;
		o.closeStream();

		std::cout << "depth intrinsics:" << std::endl << cs.m_CalibrationDepth.m_Intrinsic << std::endl;
		std::cout << "color intrinsics:" << std::endl << cs.m_CalibrationColor.m_Intrinsic << std::endl;
	}
	const std::string siftFile = "debug/" + which + ".sift";
	const std::string cacheFile = "debug/" + which + ".cache";
	const std::string trajFile = "debug/opt_" + which + ".bin";
	const std::string refTrajFile = "debug/ref_" + whichRef + ".bin";
	const unsigned int submapSize = GlobalBundlingState::get().s_submapSize;
	bool savePointClouds = false; const float maxDepth = 1.0f;

	std::vector<mat4f> trajectoryAll, refTrajectoryAll;
	std::vector<mat4f> trajectoryKeys, refTrajectoryKeys;
	if (useReference) {
		BinaryDataStreamFile s(refTrajFile, false);
		s >> refTrajectoryAll;
		mat4f refOffset = refTrajectoryAll.front().getInverse();
		for (unsigned int i = 0; i < refTrajectoryAll.size(); i++) refTrajectoryAll[i] = refOffset * refTrajectoryAll[i];
	}
	if (util::fileExists(trajFile)) {
		BinaryDataStreamFile s(trajFile, false);
		s >> trajectoryAll;
	}
	else {
		if (!useReference) throw MLIB_EXCEPTION("opt trajectory files does not exist!");
		std::cout << "using identity trajectory initialization" << std::endl;
		trajectoryAll.resize(refTrajectoryAll.size(), mat4f::identity());
	}
	size_t numTransforms = useReference ? std::min(trajectoryAll.size(), refTrajectoryAll.size()) : trajectoryAll.size();
	for (unsigned int i = 0; i < numTransforms; i += submapSize) {
		trajectoryKeys.push_back(trajectoryAll[i]);
		if (useReference) refTrajectoryKeys.push_back(refTrajectoryAll[i]);
	}

	std::cout << "loading sift from file... ";
	m_siftManager->loadFromFile(siftFile);
	std::cout << "done" << std::endl;

	CUDACache cudaCache(640, 480, GlobalBundlingState::get().s_downsampledWidth, GlobalBundlingState::get().s_downsampledHeight,
		GlobalBundlingState::get().s_maxNumImages, mat4f::identity());
	std::cout << "loading cache from file... ";
	if (loadCache) {
		cudaCache.loadFromFile(cacheFile);
		std::cout << "WARNING: cannot re-filter cached frames" << std::endl; getchar();
	}
	else {
		loadCachedFramesFromSensor(&cudaCache, origFile, submapSize, (unsigned int)trajectoryKeys.size());
		//std::cout << "saving cache to point cloud... "; SiftVisualization::saveToPointCloud("debug/cache.ply", &cudaCache, trajectoryKeys, maxDepth); std::cout << "done" << std::endl;
		//vec2ui indices(63, 88);
		//SiftVisualization::saveImPairToPointCloud("debug/test", cudaCache.getCacheFrames(), cudaCache.getWidth(), cudaCache.getHeight(), indices, trajectoryKeys[indices.y].getInverse() * trajectoryKeys[indices.x]);
		//printCacheFrames("debug/cache/", &cudaCache, 10);
		//cudaCache.saveToFile("debug/tmp.cache");
		//cudaCache.loadFromFile("debug/tmp.cache");
		//std::cout << "waiting..." << std::endl; getchar();
	}
	std::cout << "done" << std::endl;

	const unsigned int numImages = m_siftManager->getNumImages();

	//initial trajectory
	if (useReference) {
		const auto transErr = PoseHelper::evaluateAteRmse(trajectoryKeys, refTrajectoryKeys);
		std::cout << "[ init ate rmse = " << transErr.first << ", " << transErr.second << " ]" << std::endl;
		evaluateTrajectory(submapSize, trajectoryAll, trajectoryKeys, refTrajectoryAll);
		std::cout << std::endl;
	}

	SBA sba;
	const unsigned int maxNumImages = GlobalBundlingState::get().s_maxNumImages;
	const unsigned int maxNumResiduals = MAX_MATCHES_PER_IMAGE_PAIR_FILTERED * (maxNumImages*(maxNumImages - 1)) / 2;
	sba.init(numImages, maxNumResiduals);
	const unsigned int maxNumOutIts = 2;
	const unsigned int maxNumIters = 4;
	const unsigned int numPCGIts = 50;
	const bool useVerify = true;
	const bool isLocal = false;
	unsigned int numPerRemove = GlobalBundlingState::get().s_numOptPerResidualRemoval;
	//params
	//std::vector<float> weightsSparse(maxNumIters, 5.0f); //fr3_nstn
	//std::vector<float> weightsDenseDepth(maxNumIters, 0.3f);
	//std::vector<float> weightsDenseColor(maxNumIters, 0.2f);

	//std::vector<float> weightsSparse(maxNumIters, 1.0f); //fr2_xyz 
	//std::vector<float> weightsDenseDepth(maxNumIters, 0.0f);
	//std::vector<float> weightsDenseColor(maxNumIters, 1.0f);

	//std::vector<float> weightsSparse(maxNumIters, 0.2f); //fr1_desk 
	//std::vector<float> weightsDenseDepth(maxNumIters, 1.0f);
	//std::vector<float> weightsDenseColor(maxNumIters, 0.0f);

	//std::vector<float> weightsSparse(maxNumIters, 20.0f); //fr2_desk
	//std::vector<float> weightsDenseDepth(maxNumIters, 0.2f);
	//std::vector<float> weightsDenseColor(maxNumIters, 1.0f);

	//std::vector<float> weightsSparse(maxNumIters, 1.0f); //livingroom3
	//std::vector<float> weightsDenseDepth(maxNumIters, 1.0f);
	//std::vector<float> weightsDenseColor(maxNumIters, 0.0f);
	std::vector<float> weightsSparse(maxNumIters, 1.0f);
	std::vector<float> weightsDenseDepth(maxNumIters, 50.0f);
	std::vector<float> weightsDenseColor(maxNumIters, 0.0f);

	//if (savePointClouds) {
	//	std::cout << "saving init to point cloud... "; SiftVisualization::saveToPointCloud("debug/init.ply", m_depthImages, m_colorImages, trajectoryKeys, m_depthCalibration.m_IntrinsicInverse, maxDepth); std::cout << "done" << std::endl;
	//	std::cout << "saving ref to point cloud... "; SiftVisualization::saveToPointCloud("debug/ref.ply", m_depthImages, m_colorImages, refTrajectoryKeys, m_depthCalibration.m_IntrinsicInverse, maxDepth); std::cout << "done" << std::endl;
	//}

	float4x4* d_transforms = NULL; MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_transforms, sizeof(float4x4)*numImages));
	//if (false && useReference) {//evaluate at ground truth
	//	std::cout << "saving ref to point cloud... "; SiftVisualization::saveToPointCloud("debug/ref.ply", m_depthImages, m_colorImages, refTrajectoryKeys, m_depthCalibration.m_IntrinsicInverse, maxDepth); std::cout << "done" << std::endl;
	//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_transforms, refTrajectoryKeys.data(), sizeof(float4x4)*numImages, cudaMemcpyHostToDevice));
	//	sba.setGlobalWeights(std::vector<float>(maxNumIters, 0.0f), std::vector<float>(maxNumIters, 1.0f), std::vector<float>(maxNumIters, 0.0f), true);
	//	for (unsigned int i = 0; i < 1; i++) {
	//		sba.align(m_siftManager, &cudaCache, d_transforms, 4, numPCGIts, useVerify, isLocal, false, true, false, false);
	//		MLIB_CUDA_SAFE_CALL(cudaMemcpy(trajectoryKeys.data(), d_transforms, sizeof(float4x4)*numImages, cudaMemcpyDeviceToHost));
	//		const auto transErr = PoseHelper::evaluateAteRmse(trajectoryKeys, refTrajectoryKeys);
	//		std::cout << "ate rmse = " << transErr.first << ", " << transErr.second << std::endl;
	//	}
	//	std::cout << "saving ref opt to point cloud... "; SiftVisualization::saveToPointCloud("debug/optref.ply", m_depthImages, m_colorImages, trajectoryKeys, m_depthCalibration.m_IntrinsicInverse, maxDepth); std::cout << "done" << std::endl;
	//	std::cout << "waiting..." << std::endl; getchar();
	//}
	//{//evaluate sparse only
	//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_transforms, trajectoryKeys.data(), sizeof(float4x4)*numImages, cudaMemcpyHostToDevice));
	//	sba.setUseGlobalDenseOpt(false);
	//	sba.setGlobalWeights(std::vector<float>(maxNumIters, 1.0f), std::vector<float>(maxNumIters, 0.0f), std::vector<float>(maxNumIters, 0.0f));
	//	for (unsigned int i = 0; i < 8; i++)
	//		sba.align(m_siftManager, &cudaCache, d_transforms, maxNumIters, numPCGIts, useVerify, isLocal, false, true, false, false);
	//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(trajectoryKeys.data(), d_transforms, sizeof(float4x4)*numImages, cudaMemcpyDeviceToHost));
	//const auto transErr = PoseHelper::evaluateAteRmse(trajectoryKeys, refTrajectoryKeys);
	//std::cout << "ate rmse = " << transErr.first << ", " << transErr.second << std::endl;
	//	std::cout << "waiting..." << std::endl; getchar();
	//}
	//{//evaluate dense depth only
	//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_transforms, trajectoryKeys.data(), sizeof(float4x4)*numImages, cudaMemcpyHostToDevice));
	//	sba.setUseGlobalDenseOpt(true);
	//	sba.setGlobalWeights(std::vector<float>(maxNumIters, 0.0f), std::vector<float>(maxNumIters, 1.0f), std::vector<float>(maxNumIters, 0.0f));
	//	for (unsigned int i = 0; i < 8; i++)
	//		sba.align(m_siftManager, &cudaCache, d_transforms, maxNumIters, numPCGIts, useVerify, isLocal, false, true, false, false);
	//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(trajectoryKeys.data(), d_transforms, sizeof(float4x4)*numImages, cudaMemcpyDeviceToHost));
	//const auto transErr = PoseHelper::evaluateAteRmse(trajectoryKeys, refTrajectoryKeys);
	//std::cout << "ate rmse = " << transErr.first << ", " << transErr.second << std::endl;
	//	std::cout << "waiting..." << std::endl; getchar();
	//}
	//{//evaluate dense color only
	//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_transforms, trajectoryKeys.data(), sizeof(float4x4)*numImages, cudaMemcpyHostToDevice));
	//	sba.setUseGlobalDenseOpt(true);
	//	sba.setGlobalWeights(std::vector<float>(maxNumIters, 0.0f), std::vector<float>(maxNumIters, 0.0f), std::vector<float>(maxNumIters, 1.0f));
	//	for (unsigned int i = 0; i < 16; i++)
	//		sba.align(m_siftManager, &cudaCache, d_transforms, maxNumIters, numPCGIts, useVerify, isLocal, false, true, false, false);
	//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(trajectoryKeys.data(), d_transforms, sizeof(float4x4)*numImages, cudaMemcpyDeviceToHost));
	//const auto transErr = PoseHelper::evaluateAteRmse(trajectoryKeys, refTrajectoryKeys);
	//std::cout << "ate rmse = " << transErr.first << ", " << transErr.second << std::endl;
	//	std::cout << "waiting..." << std::endl; getchar();
	//}
	MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_transforms, trajectoryKeys.data(), sizeof(float4x4)*numImages, cudaMemcpyHostToDevice));

	////first sparse
	//sba.setGlobalWeights(std::vector<float>(maxNumIters, 1.0f), std::vector<float>(maxNumIters, 0.0f), std::vector<float>(maxNumIters, 0.0f), false);
	//for (unsigned int i = 0; i < 4; i++) {
	//	bool remove = (i % numPerRemove) == (numPerRemove - 1);
	//	sba.align(m_siftManager, &cudaCache, d_transforms, 4, numPCGIts, useVerify, isLocal, false, true, remove, false);
	//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(trajectoryKeys.data(), d_transforms, sizeof(float4x4)*numImages, cudaMemcpyDeviceToHost));
	//	const auto transErr = PoseHelper::evaluateAteRmse(trajectoryKeys, refTrajectoryKeys);
	//	std::cout << "[ ate rmse = " << transErr.first << ", " << transErr.second << " ]" << std::endl;
	//	std::cout << std::endl;
	//}
	//getchar();

	sba.setGlobalWeights(weightsSparse, weightsDenseDepth, weightsDenseColor, true); //some dense
	for (unsigned int i = 0; i < maxNumOutIts; i++) {
		bool remove = false;//(i % numPerRemove) == (numPerRemove - 1);
		sba.align(m_siftManager, &cudaCache, d_transforms, maxNumIters, numPCGIts, useVerify, isLocal, false, true, remove, false);
		if (useReference) {
			MLIB_CUDA_SAFE_CALL(cudaMemcpy(trajectoryKeys.data(), d_transforms, sizeof(float4x4)*numImages, cudaMemcpyDeviceToHost));
			const auto transErr = PoseHelper::evaluateAteRmse(trajectoryKeys, refTrajectoryKeys);
			std::cout << "[ ate rmse = " << transErr.first << ", " << transErr.second << " ]" << std::endl;
			std::cout << std::endl;
		}
	}

	MLIB_CUDA_SAFE_CALL(cudaMemcpy(trajectoryKeys.data(), d_transforms, sizeof(float4x4)*numImages, cudaMemcpyDeviceToHost));

	MLIB_CUDA_SAFE_FREE(d_transforms);
	if (savePointClouds) {
		std::cout << "saving opt to point cloud... "; SiftVisualization::saveToPointCloud("debug/opt.ply", m_depthImages, m_colorImages, trajectoryKeys, m_depthCalibration.m_IntrinsicInverse, 1, (unsigned int)m_depthImages.size(), maxDepth); std::cout << "done" << std::endl;
		//std::cout << "saving opt cache to point cloud... "; SiftVisualization::saveToPointCloud("debug/optCache.ply", &cudaCache, trajectoryKeys, maxDepth); std::cout << "done" << std::endl;	}
	}
	// compare to reference trajectory
	if (useReference) {
		const auto transErr = PoseHelper::evaluateAteRmse(trajectoryKeys, refTrajectoryKeys);
		std::cout << "*********************************" << std::endl;
		std::cout << " ate rmse = " << transErr.first << ", " << transErr.second << std::endl;
		std::cout << "*********************************" << std::endl;
		evaluateTrajectory(submapSize, trajectoryAll, trajectoryKeys, refTrajectoryAll);
		PoseHelper::saveToPoseFile("debug/gt.txt", refTrajectoryKeys);
		PoseHelper::saveToPoseFile("debug/opt.txt", trajectoryKeys);
	}
	else {
		PoseHelper::composeTrajectory(submapSize, trajectoryKeys, trajectoryAll);
	}
	{
		BinaryDataStreamFile s("debug/opt.bin", true);
		s << trajectoryKeys;
		s << trajectoryAll;
		s.closeStream();
	}

	if (writeSensorFile) {
		const std::string outFile = "dump/" + which + "_glob.sens";
		std::cout << "writing sensor file to " << outFile << "... ";
		SensorData sensorData;
		sensorData.loadFromFile(origFile);
		if (sensorData.m_frames.size() != trajectoryAll.size()) {
			std::cout << "warning: sensor data has " << sensorData.m_frames.size() << " frames vs " << trajectoryAll.size() << " transforms" << std::endl;
			if (sensorData.m_frames.size() > trajectoryAll.size()) sensorData.m_frames.resize(trajectoryAll.size());
		}
		for (unsigned int i = 0; i < sensorData.m_frames.size(); i++)
			sensorData.m_frames[i].setCameraToWorld(trajectoryAll[i]);
		sensorData.saveToFile(outFile);
		std::cout << "done!" << std::endl;
	}

	int a = 5;
}

void TestMatching::evaluateTrajectory(unsigned int submapSize, std::vector<mat4f>& all, const std::vector<mat4f>& keys, const std::vector<mat4f>& refAll)
{
	std::vector<mat4f> transforms;
	for (unsigned int i = 0; i < keys.size(); i++) {
		const mat4f& key = keys[i];
		transforms.push_back(key);

		const mat4f& offset = all[i*submapSize].getInverse();
		unsigned int num = std::min((int)submapSize, (int)all.size() - (int)(i * submapSize));
		for (unsigned int s = 1; s < num; s++) {
			transforms.push_back(key * offset * all[i*submapSize + s]);
		}
	}
	const auto transErr = PoseHelper::evaluateAteRmse(transforms, refAll);
	std::cout << "*********************************" << std::endl;
	std::cout << " ate rmse = " << transErr.first << ", " << transErr.second << std::endl;
	std::cout << "*********************************" << std::endl;
	PoseHelper::saveToPoseFile("debug/optAll.txt", transforms);
	all = transforms;
}

void TestMatching::compareDEBUG()
{
	const std::string ref = "fr2_desk_s2";
	const std::string test = "fr2_desk_sd";
	const std::string refTrajFile = "debug/ref_fr2_desk.bin";
	const unsigned int submapSize = GlobalBundlingState::get().s_submapSize;
	//trajectories
	std::vector<mat4f> optTrajectoryRef, optTrajectoryTest, referenceTrajectory;
	{
		BinaryDataStreamFile s("debug/opt_" + ref + ".bin", false);
		s >> optTrajectoryRef;
	}
	{
		BinaryDataStreamFile s(refTrajFile, false);
		s >> referenceTrajectory;
	}
	{
		BinaryDataStreamFile s("debug/opt_" + test + ".bin", false);
		s >> optTrajectoryTest;
	}

	SIFTImageManager siftManagerTest(GlobalBundlingState::get().s_submapSize, GlobalBundlingState::get().s_maxNumImages, GlobalBundlingState::get().s_maxNumKeysPerImage);

	std::cout << "loading sift from file... ";
	m_siftManager->loadFromFile("debug/" + ref + ".sift");
	siftManagerTest.loadFromFile("debug/" + test + ".sift");
	std::cout << "done" << std::endl;
	const std::vector<int>& validImagesRef = m_siftManager->getValidImages();
	const std::vector<int>& validImagesTest = siftManagerTest.getValidImages();

	const unsigned int numImages = m_siftManager->getNumImages();
	std::cout << "[ #images = " << numImages << " ]" << std::endl;
	std::cout << "#correspondences reference = " << m_siftManager->getNumGlobalCorrespondences() << std::endl;
	std::cout << "#correspondences test = " << siftManagerTest.getNumGlobalCorrespondences() << std::endl;

	//stats for correspondences
	std::vector<EntryJ> refCorr(m_siftManager->getNumGlobalCorrespondences());
	MLIB_CUDA_SAFE_CALL(cudaMemcpy(refCorr.data(), m_siftManager->getGlobalCorrespondencesGPU(), sizeof(EntryJ) * refCorr.size(), cudaMemcpyDeviceToHost));
	std::vector<EntryJ> testCorr(siftManagerTest.getNumGlobalCorrespondences());
	MLIB_CUDA_SAFE_CALL(cudaMemcpy(testCorr.data(), siftManagerTest.getGlobalCorrespondencesGPU(), sizeof(EntryJ) * testCorr.size(), cudaMemcpyDeviceToHost));

	std::vector<unsigned int> numImCorrPerImageRef(numImages), numImCorrPerImageTest(numImages);
	std::vector< std::vector<bool> > markers(numImages); for (unsigned int i = 0; i < numImages; i++) markers[i].resize(numImages, false);
	for (unsigned int i = 0; i < refCorr.size(); i++) {
		const EntryJ& corr = refCorr[i];
		if (corr.isValid() && validImagesRef[corr.imgIdx_i] && validImagesRef[corr.imgIdx_j]) {
			if (!markers[corr.imgIdx_i][corr.imgIdx_j]) {
				numImCorrPerImageRef[corr.imgIdx_i]++;
				markers[corr.imgIdx_i][corr.imgIdx_j] = true;
			}
			if (!markers[corr.imgIdx_j][corr.imgIdx_i]) {
				numImCorrPerImageRef[corr.imgIdx_j]++;
				markers[corr.imgIdx_j][corr.imgIdx_i] = true;
			}
		}
	}
	markers.clear(); markers.resize(numImages); for (unsigned int i = 0; i < numImages; i++) markers[i].resize(numImages, false);
	for (unsigned int i = 0; i < testCorr.size(); i++) {
		const EntryJ& corr = testCorr[i];
		if (corr.isValid() && validImagesTest[corr.imgIdx_i] && validImagesTest[corr.imgIdx_j]) {
			if (!markers[corr.imgIdx_i][corr.imgIdx_j]) {
				numImCorrPerImageTest[corr.imgIdx_i]++;
				markers[corr.imgIdx_i][corr.imgIdx_j] = true;
			}
			if (!markers[corr.imgIdx_j][corr.imgIdx_i]) {
				numImCorrPerImageTest[corr.imgIdx_j]++;
				markers[corr.imgIdx_j][corr.imgIdx_i] = true;
			}
		}
	}
	//std::vector<unsigned int> numCorrPerImageRef(numImages), numCorrPerImageTest(numImages);
	//for (unsigned int i = 0; i < refCorr.size(); i++) {
	//	const EntryJ& corr = refCorr[i];
	//	if (corr.isValid()) {
	//		numCorrPerImageRef[corr.imgIdx_i]++;
	//		numCorrPerImageRef[corr.imgIdx_j]++;
	//	}
	//}
	//for (unsigned int i = 0; i < testCorr.size(); i++) {
	//	const EntryJ& corr = testCorr[i];
	//	if (corr.isValid()) {
	//		numCorrPerImageTest[corr.imgIdx_i]++;
	//		numCorrPerImageTest[corr.imgIdx_j]++;
	//	}
	//}
	std::vector<mat4f> optKeysRef, refKeys, optKeysTest;
	size_t numTransforms = math::min(math::min(optTrajectoryRef.size(), referenceTrajectory.size()), optTrajectoryTest.size());
	for (unsigned int i = 0; i < numTransforms; i += submapSize) {
		optKeysRef.push_back(optTrajectoryRef[i]);
		optKeysTest.push_back(optTrajectoryTest[i]);
		refKeys.push_back(referenceTrajectory[i]);
	}
	std::vector<std::pair<unsigned int, float>> err2PerImageRef = PoseHelper::evaluateErr2PerImage(optKeysRef, refKeys);
	std::vector<std::pair<unsigned int, float>> err2PerImageTest = PoseHelper::evaluateErr2PerImage(optKeysTest, refKeys);

	std::vector<unsigned int> numKeysRef(numImages), numKeysTest(numImages);
	for (unsigned int i = 0; i < numImages; i++) {
		numKeysRef[i] = m_siftManager->getNumKeyPointsPerImage(i);
		numKeysTest[i] = siftManagerTest.getNumKeyPointsPerImage(i);
	}

	//std::vector<float> errPerKeyRef(numImages, 0.0f), errPerKeyTest(numImages, 0.0f);
	//for (unsigned int i = 3; i < optKeysRef.size(); i++) {
	//	errPerKeyRef[i] = PoseHelper::evaluateAteRmse(optKeysRef, refKeys, i).first;
	//	errPerKeyTest[i] = PoseHelper::evaluateAteRmse(optKeysTest, refKeys, i).first;
	//}

	std::vector<std::pair<vec2ui, float>> errPerImagePairCorrsRef, errPerImagePairCorrsTest;
	{
		std::unordered_map<vec2ui, vec2f> helper;
		for (unsigned int i = 0; i < refCorr.size(); i++) {
			const EntryJ& corr = refCorr[i];
			if (corr.isValid() && validImagesRef[corr.imgIdx_i] && validImagesRef[corr.imgIdx_j]) {
				vec2ui imageIndices(corr.imgIdx_i, corr.imgIdx_j);
				vec3f diff = referenceTrajectory[corr.imgIdx_i*submapSize] * vec3f(corr.pos_i.x, corr.pos_i.y, corr.pos_i.z) -
					referenceTrajectory[corr.imgIdx_j*submapSize] * vec3f(corr.pos_j.x, corr.pos_j.y, corr.pos_j.z);
				float err2 = diff.lengthSq();
				auto it = helper.find(imageIndices);
				if (it == helper.end()) helper[imageIndices] = vec2f(err2, 1.0f);
				else it->second += vec2f(err2, 1.0f);
			}
		}
		for (const auto a : helper) {
			errPerImagePairCorrsRef.push_back(std::make_pair(a.first, a.second.x / a.second.y));
		}
		helper.clear();
		for (unsigned int i = 0; i < testCorr.size(); i++) {
			const EntryJ& corr = testCorr[i];
			if (corr.isValid() && validImagesTest[corr.imgIdx_i] && validImagesTest[corr.imgIdx_j]) {
				vec2ui imageIndices(corr.imgIdx_i, corr.imgIdx_j);
				vec3f diff = referenceTrajectory[corr.imgIdx_i*submapSize] * vec3f(corr.pos_i.x, corr.pos_i.y, corr.pos_i.z) -
					referenceTrajectory[corr.imgIdx_j*submapSize] * vec3f(corr.pos_j.x, corr.pos_j.y, corr.pos_j.z);
				float err2 = diff.lengthSq();
				auto it = helper.find(imageIndices);
				if (it == helper.end()) helper[imageIndices] = vec2f(err2, 1.0f);
				else it->second += vec2f(err2, 1.0f);
			}
		}
		for (const auto a : helper) {
			errPerImagePairCorrsTest.push_back(std::make_pair(a.first, a.second.x / a.second.y));
		}
		std::sort(errPerImagePairCorrsRef.begin(), errPerImagePairCorrsRef.end(), [](const std::pair<vec2ui, float> &left, const std::pair<vec2ui, float> &right) {
			return fabs(left.second) > fabs(right.second);
		});
		std::sort(errPerImagePairCorrsTest.begin(), errPerImagePairCorrsTest.end(), [](const std::pair<vec2ui, float> &left, const std::pair<vec2ui, float> &right) {
			return fabs(left.second) > fabs(right.second);
		});
	}

	float totalErrRef = PoseHelper::evaluateAteRmse(optTrajectoryRef, referenceTrajectory).first;
	float totalErrTest = PoseHelper::evaluateAteRmse(optTrajectoryTest, referenceTrajectory).first;
	float keysErrRef = PoseHelper::evaluateAteRmse(optKeysRef, refKeys).first;
	float keysErrTest = PoseHelper::evaluateAteRmse(optKeysTest, refKeys).first;
	std::cout << "total error ref = " << totalErrRef << std::endl;
	std::cout << "keys error ref = " << keysErrRef << std::endl;
	std::cout << std::endl;
	std::cout << "total error test = " << totalErrTest << std::endl;
	std::cout << "keys error test = " << keysErrTest << std::endl;
	{
		std::ofstream s("debug/statsRef.txt");
		s << "ref" << std::endl;
		s << "image\terror2\t#corr\t#keys\timage pair\terr per image pair" << std::endl;
		for (unsigned int i = 0; i < err2PerImageRef.size(); i++)
			s << err2PerImageRef[i].first << "\t" << err2PerImageRef[i].second << "\t" << numImCorrPerImageRef[err2PerImageRef[i].first] << "\t" << numKeysRef[err2PerImageRef[i].first] << "\t" << errPerImagePairCorrsRef[i].first << "\t" << errPerImagePairCorrsRef[i].second << std::endl;
		for (size_t i = err2PerImageRef.size(); i < errPerImagePairCorrsRef.size(); i++)
			s << "\t\t\t\t" << errPerImagePairCorrsRef[i].first << "\t" << errPerImagePairCorrsRef[i].second << std::endl;
		s.close();
	}
	{
		std::ofstream s("debug/statsTest.txt");
		s << "test" << std::endl;
		s << "image\terror2\t#corr\t#keys\timage pair\terr per image pair" << std::endl;
		for (unsigned int i = 0; i < err2PerImageTest.size(); i++)
			s << err2PerImageTest[i].first << "\t" << err2PerImageTest[i].second << "\t" << numImCorrPerImageTest[err2PerImageTest[i].first] << "\t" << numKeysTest[err2PerImageTest[i].first] << "\t" << errPerImagePairCorrsTest[i].first << "\t" << errPerImagePairCorrsTest[i].second << std::endl;
		for (size_t i = err2PerImageTest.size(); i < errPerImagePairCorrsTest.size(); i++)
			s << "\t\t\t\t" << errPerImagePairCorrsTest[i].first << "\t" << errPerImagePairCorrsTest[i].second << std::endl;
		s.close();
	}
}

void TestMatching::loadCachedFramesFromSensor(CUDACache* cache, const std::string& filename, unsigned int skip, unsigned int numFrames /*= (unsigned int)-1*/, unsigned int startFrame /*= 0*/)
{
	if (util::getFileExtension(filename) == "sens") {
		loadCachedFramesFromSensorData(cache, filename, skip, numFrames, startFrame);
		return;
	}
	throw MLIB_EXCEPTION(".sensor unsupported!");

	if (util::getFileExtension(filename) != "sensor") throw MLIB_EXCEPTION("invalid file type " + filename + " for cache load");
	std::cout << "loading cached frames from sensor... ";
	CalibratedSensorData cs;
	{
		BinaryDataStreamFile s(filename, false);
		s >> cs;
		s.closeStream();
	}
	if (numFrames == (unsigned int)-1) numFrames = cs.m_DepthNumFrames / skip;

	m_colorImages.resize(numFrames);
	m_depthImages.resize(numFrames);
	m_referenceTrajectory.resize(numFrames);
	for (unsigned int i = 0; i < numFrames; i++) {
		const unsigned int oldIndex = (i + startFrame) * skip;
		MLIB_ASSERT(oldIndex < cs.m_DepthNumFrames);
		//m_colorImages[i] = ColorImageR8G8B8A8(cs.m_ColorImageWidth, cs.m_ColorImageHeight, cs.m_ColorImages[oldIndex]);
		m_depthImages[i] = DepthImage32(cs.m_DepthImageWidth, cs.m_DepthImageHeight, cs.m_DepthImages[oldIndex]);
		m_referenceTrajectory[i] = cs.m_trajectory[oldIndex];
	}

	m_depthCalibration = cs.m_CalibrationDepth;
	m_colorCalibration = cs.m_CalibrationColor;

	std::cout << "done! (" << m_colorImages.size() << " of " << cs.m_ColorNumFrames << ")" << std::endl;
	std::cout << "depth intrinsics:" << std::endl << m_depthCalibration.m_Intrinsic << std::endl;
	std::cout << "color intrinsics:" << std::endl << m_colorCalibration.m_Intrinsic << std::endl;

	initSiftParams(m_depthImages.front().getWidth(), m_depthImages.front().getHeight(),
		m_colorImages.front().getWidth(), m_colorImages.front().getHeight());

	m_stage = INITIALIZED;

	createCUDACache(cache);

	std::cout << "done!" << std::endl;
}

void TestMatching::printColorImages()
{
	const std::string filename = "../data/gates371/gates371.sens";
	std::cout << "loading sensor file.... ";
	SensorData sensorData;
	sensorData.loadFromFile(filename);
	std::cout << "done!" << std::endl;

	std::cout << "writing color images... ";
	const std::string outDir = "debug/color/";
	if (!util::directoryExists(outDir)) util::makeDirectory(outDir);

	for (unsigned int i = 0; i < sensorData.m_frames.size(); i += 10) {
		SensorData::RGBDFrameCacheRead::FrameState frameState;
		frameState.m_colorFrame = sensorData.decompressColorAlloc(i);
		//frameState.m_depthFrame = sensorData.decompressDepthAlloc(i);
		FreeImageWrapper::saveImage(outDir + std::to_string(i) + ".png", ColorImageR8G8B8(sensorData.m_colorWidth, sensorData.m_colorHeight, frameState.m_colorFrame));
		frameState.free();
	}
	std::cout << "done!" << std::endl;
}

void TestMatching::loadCachedFramesFromSensorData(CUDACache* cache, const std::string& filename, unsigned int skip, unsigned int numFrames, unsigned int startFrame)
{
	if (!(util::getFileExtension(filename) == "sens")) throw MLIB_EXCEPTION("invalid file type " + filename + " for sensorData");

	std::cout << "loading cached frames from sensor... ";
	SensorData sensorData;
	sensorData.loadFromFile(filename);
	SensorData::RGBDFrameCacheRead sensorDataCache(&sensorData, 10);
	const unsigned int numOrigFrames = (unsigned int)sensorData.m_frames.size();

	if (numFrames == (unsigned int)-1) numFrames = numOrigFrames / skip;

	m_colorImages.resize(numFrames);
	m_depthImages.resize(numFrames);
	m_referenceTrajectory.resize(numFrames);
	for (unsigned int i = 0; i < numFrames; i++) {
		const unsigned int oldIndex = (i + startFrame) * skip;
		MLIB_ASSERT(oldIndex < numOrigFrames);
		const auto& frame = sensorData.m_frames[oldIndex];
		vec3uc* colordata = sensorData.decompressColorAlloc(frame);
		unsigned short* depthdata = sensorData.decompressDepthAlloc(frame);
		m_colorImages[i] = ColorImageR8G8B8(sensorData.m_colorWidth, sensorData.m_colorHeight, colordata);
		m_depthImages[i] = DepthImage32(DepthImage16(sensorData.m_depthWidth, sensorData.m_depthHeight, depthdata));
		m_referenceTrajectory[i] = frame.getCameraToWorld();
		std::free(colordata);	std::free(depthdata);
	}

	m_depthCalibration.setMatrices(sensorData.m_calibrationDepth.m_intrinsic, sensorData.m_calibrationDepth.m_extrinsic);
	m_colorCalibration.setMatrices(sensorData.m_calibrationColor.m_intrinsic, sensorData.m_calibrationColor.m_extrinsic);

	std::cout << "done! (" << m_colorImages.size() << " of " << sensorData.m_frames.size() << ")" << std::endl;
	std::cout << "depth intrinsics:" << std::endl << m_depthCalibration.m_Intrinsic << std::endl;
	std::cout << "color intrinsics:" << std::endl << m_colorCalibration.m_Intrinsic << std::endl;

	initSiftParams(m_depthImages.front().getWidth(), m_depthImages.front().getHeight(),
		m_colorImages.front().getWidth(), m_colorImages.front().getHeight());

	m_stage = INITIALIZED;

	if (cache) createCUDACache(cache);

	std::cout << "done!" << std::endl;
}

void TestMatching::createCUDACache(CUDACache* cache)
{
	//cached frames
	std::vector<CUDACachedFrame>& cachedFrames = cache->getCachedFramesDEBUG();
	unsigned int width = cache->getWidth(); unsigned int height = cache->getHeight();

	const float scaleWidth = (float)width / (float)m_widthDepth;
	const float scaleHeight = (float)height / (float)m_heightDepth;
	m_intrinsicsDownsampled = m_depthCalibration.m_Intrinsic;
	m_intrinsicsDownsampled._m00 *= scaleWidth;  m_intrinsicsDownsampled._m02 *= scaleWidth;
	m_intrinsicsDownsampled._m11 *= scaleHeight; m_intrinsicsDownsampled._m12 *= scaleHeight;
	cache->setIntrinsics(m_depthCalibration.m_Intrinsic, m_intrinsicsDownsampled);

	float* d_depth = NULL; float* d_depthErodeHelper = NULL; uchar4* d_color = NULL;
	float4* d_helperCamPos = NULL; float4* d_helperNormal = NULL; float* d_filterHelperDown = NULL;
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_depth, sizeof(float) * m_widthDepth * m_heightDepth));
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_color, sizeof(uchar4) * m_widthSift * m_heightSift));
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_depthErodeHelper, sizeof(float) * m_widthDepth * m_heightDepth));
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_helperCamPos, sizeof(float4) * m_widthDepth * m_heightDepth));
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_helperNormal, sizeof(float4) * m_widthDepth * m_heightDepth));
	MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_filterHelperDown, sizeof(float) * width * height));

	//const float adaptDepthFactor = 1.0f;
	const float adaptIntensityFactor = 1.0f;
	const float intensityFilterSigma = GlobalBundlingState::get().s_colorDownSigma;
	const float depthFilterSigmaD = GlobalBundlingState::get().s_depthDownSigmaD;
	const float depthFilterSigmaR = GlobalBundlingState::get().s_depthDownSigmaR;
	//erode and smooth depth
	bool erode = GlobalBundlingState::get().s_erodeSIFTdepth;
	for (unsigned int i = 0; i < m_colorImages.size(); i++) {
		ColorImageR8G8B8A8 colorImage(m_colorImages[i]);
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_depth, m_depthImages[i].getData(), sizeof(float) * m_depthImages[i].getNumPixels(), cudaMemcpyHostToDevice));
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_color, colorImage.getData(), sizeof(uchar4) * colorImage.getNumPixels(), cudaMemcpyHostToDevice));
		if (erode) {
			unsigned int numIter = 2; numIter = 2 * ((numIter + 1) / 2);
			for (unsigned int k = 0; k < numIter; k++) {
				if (k % 2 == 0) {
					CUDAImageUtil::erodeDepthMap(d_depthErodeHelper, d_depth, 3, m_widthDepth, m_heightDepth, 0.05f, 0.3f);
				}
				else {
					CUDAImageUtil::erodeDepthMap(d_depth, d_depthErodeHelper, 3, m_widthDepth, m_heightDepth, 0.05f, 0.3f);
				}
			}
			//MLIB_CUDA_SAFE_CALL(cudaMemcpy(m_depthImages[i].getData(), d_depth, sizeof(float) * m_depthImages[i].getNumPixels(), cudaMemcpyDeviceToHost)); //for vis only
		}

		CUDACachedFrame& frame = cachedFrames[i];
		if (depthFilterSigmaD > 0.0f) {
			CUDAImageUtil::gaussFilterDepthMap(d_depthErodeHelper, d_depth, depthFilterSigmaD, depthFilterSigmaR, m_widthDepth, m_heightDepth);
			//CUDAImageUtil::adaptiveGaussFilterDepthMap(d_depthErodeHelper, d_depth, depthFilterSigmaD, 1.0f, depthFilterSigmaR, m_widthDepth, m_heightDepth);
			std::swap(d_depthErodeHelper, d_depth);
		}
		CUDAImageUtil::convertDepthFloatToCameraSpaceFloat4(d_helperCamPos, d_depth, MatrixConversion::toCUDA(m_depthCalibration.m_IntrinsicInverse), m_widthDepth, m_heightDepth);
		CUDAImageUtil::resampleFloat4(frame.d_cameraposDownsampled, width, height, d_helperCamPos, m_widthDepth, m_heightDepth);

		CUDAImageUtil::computeNormals(d_helperNormal, d_helperCamPos, m_widthDepth, m_heightDepth);
		//CUDAImageUtil::computeNormalsSobel(d_helperNormal, d_helperCamPos, m_widthDepth, m_heightDepth);
		CUDAImageUtil::resampleFloat4(frame.d_normalsDownsampled, width, height, d_helperNormal, m_widthDepth, m_heightDepth);

		CUDAImageUtil::convertNormalsFloat4ToUCHAR4(frame.d_normalsDownsampledUCHAR4, frame.d_normalsDownsampled, width, height);

		CUDAImageUtil::resampleFloat(frame.d_depthDownsampled, width, height, d_depth, m_widthDepth, m_heightDepth);

		CUDAImageUtil::resampleToIntensity(d_filterHelperDown, width, height, d_color, m_widthSift, m_heightSift);
		if (intensityFilterSigma > 0.0f) CUDAImageUtil::gaussFilterIntensity(frame.d_intensityDownsampled, d_filterHelperDown, intensityFilterSigma, width, height);
		//if (intensityFilterSigma > 0.0f) CUDAImageUtil::adaptiveGaussFilterIntensity(frame.d_intensityDownsampled, d_filterHelperDown, frame.d_depthDownsampled, intensityFilterSigma, 1.0f, width, height);
		//if (intensityFilterSigma > 0.0f) CUDAImageUtil::jointBilateralFilterFloat(frame.d_intensityDownsampled, d_filterHelperDown, frame.d_depthDownsampled, intensityFilterSigma, 0.01f, width, height);
		//if (intensityFilterSigma > 0.0f) CUDAImageUtil::adaptiveBilateralFilterIntensity(frame.d_intensityDownsampled, d_filterHelperDown, frame.d_depthDownsampled, intensityFilterSigma, 0.01f, adaptIntensityFactor, width, height);
		else std::swap(frame.d_intensityDownsampled, d_filterHelperDown);
		CUDAImageUtil::computeIntensityDerivatives(frame.d_intensityDerivsDownsampled, frame.d_intensityDownsampled, width, height);


		//CUDAImageUtil::resampleToIntensity(frame.d_intensityDownsampled, width, height, d_color, m_widthSift, m_heightSift);
		//CUDAImageUtil::computeIntensityGradientMagnitude(d_filterHelperDown, frame.d_intensityDownsampled, width, height);
		//if (intensityFilterSigma > 0.0f) CUDAImageUtil::gaussFilterIntensity(frame.d_intensityDownsampled, d_filterHelperDown, intensityFilterSigma, width, height);
		//else std::swap(frame.d_intensityDownsampled, d_filterHelperDown);
		//CUDAImageUtil::computeIntensityDerivatives(frame.d_intensityDerivsDownsampled, frame.d_intensityDownsampled, width, height);
	}
	cache->setCurrentFrame((unsigned int)m_colorImages.size() - 1);

	MLIB_CUDA_SAFE_FREE(d_depth);
	MLIB_CUDA_SAFE_FREE(d_color);
	MLIB_CUDA_SAFE_FREE(d_depthErodeHelper);
	MLIB_CUDA_SAFE_FREE(d_helperCamPos);
	MLIB_CUDA_SAFE_FREE(d_helperNormal);
	MLIB_CUDA_SAFE_FREE(d_filterHelperDown);
}

//void TestMatching::match()
//{
//	const std::string which = "aliv1";
//	const std::string siftFile = "debug/" + which + ".sift";
//	const std::string cacheFile = "debug/" + which + ".cache";
//	const std::string trajFile = "debug/opt_" + which + ".bin";
//	const std::string refSensorFile = "../data/iclnuim/aug-liv1.sensor";
//	const unsigned int submapSize = GlobalBundlingState::get().s_submapSize;
//
//	std::cout << "loading sift manager from file... ";
//	m_siftManager->loadFromFile(siftFile);
//	std::cout << "done!" << std::endl;
//	const unsigned int numKeys = m_siftManager->getNumImages();
//
//	CUDACache cudaCache(640, 480, GlobalBundlingState::get().s_downsampledWidth, GlobalBundlingState::get().s_downsampledHeight,
//		GlobalBundlingState::get().s_maxNumImages, mat4f::identity());
//	std::cout << "loading cache from file... ";
//	loadCachedFramesFromSensor(&cudaCache, refSensorFile, submapSize, numKeys);
//
//	// detect keypoints
//	detectKeys(m_colorImages, m_depthImages, m_siftManager);
//
//	//match
//	SiftMatchGPU* matcher = new SiftMatchGPU(GlobalBundlingState::get().s_maxNumKeysPerImage);
//	matcher->InitSiftMatch();
//	const float ratioMax = GlobalBundlingState::get().s_siftMatchRatioMaxGlobal;
//	const float matchThresh = GlobalBundlingState::get().s_siftMatchThresh;
//	const unsigned int minNumMatches = GlobalBundlingState::get().s_minNumMatchesGlobal;
//
//	const std::vector<int>& validImages = m_siftManager->getValidImages();
//	std::vector<vec2ui> frameIndices = { vec2ui(0, 201) };
//	for (unsigned int i = 0; i < frameIndices.size(); i++) {
//		int numKeyPrev = (int)m_siftManager->getNumKeyPointsPerImage(frameIndices[i].x);
//		int numKeyCur = (int)m_siftManager->getNumKeyPointsPerImage(frameIndices[i].y);
//
//		if (numKeyPrev > 0 && numKeyCur > 0 && validImages[frameIndices[i].x] && validImages[frameIndices[i].y]) {
//			const SIFTImageGPU& prev = m_siftManager->getImageGPU(frameIndices[i].x);
//			const SIFTImageGPU& cur = m_siftManager->getImageGPU(frameIndices[i].y);
//			uint2 keyPointOffset = make_uint2(0, 0);
//			ImagePairMatch& imagePairMatch = m_siftManager->getImagePairMatch(frameIndices[i].x, frameIndices[i].y, keyPointOffset);
//
//			matcher->SetDescriptors(0, numKeyPrev, (unsigned char*)prev.d_keyPointDescs);
//			matcher->SetDescriptors(1, numKeyCur, (unsigned char*)cur.d_keyPointDescs);
//			matcher->GetSiftMatch(numKeyPrev, imagePairMatch, keyPointOffset, matchThresh, ratioMax);
//
//			//filter
//			siftManager->SortKeyPointMatchesCU(curFrame, startFrame, numFrames);
//		}
//		else {
//			std::cout << "ERROR: images (" << frameIndices[i] << ") #keys = (" << numKeyPrev << ", " << numKeyCur << "); valid = (" << validImages[frameIndices[i].x] << ", " << validImages[frameIndices[i].y] << ")" << std::endl;
//		}
//	}
//}

void dfs(unsigned int frame, const std::vector< std::vector<unsigned int> > imageImageCorrs, std::vector<bool>& visited)
{
	for (unsigned int i = 0; i < imageImageCorrs[frame].size(); i++) {
		const unsigned int v = imageImageCorrs[frame][i];
		if (!visited[v]) {
			visited[v] = true;
			dfs(v, imageImageCorrs, visited);
		}
	}
}
//#define LOAD_REFERENCE
void TestMatching::debug()
{
	const std::string siftFile = "debug/filtMatches/241.sift";
	const std::string trajFile = "debug/filtMatches/241.trajectory";
	const std::string sensFile = "../data/testing/2016-08-08_03-04-30__C7BA9586-8237-4204-9116-02AE5804338A.sens";
	const unsigned int submapSize = GlobalBundlingState::get().s_submapSize;

	std::vector<mat4f> keysTrajectory;
	BinaryDataStreamFile sTraj(trajFile, false);
	sTraj >> keysTrajectory; sTraj.closeStream();
	//for (unsigned int i = 0; i < completeTrajectory.size(); i += submapSize) keysTrajectory.push_back(completeTrajectory[i]);

	m_siftManager->loadFromFile(siftFile);
	const unsigned int numKeys = m_siftManager->getNumImages();
	if (keysTrajectory.size() > numKeys) keysTrajectory.resize(numKeys);
	for (unsigned int i = 0; i < numKeys; i++) {
		if (m_siftManager->getValidImages()[i] == 0) keysTrajectory[i].setZero(-std::numeric_limits<float>::infinity());
	}

	CUDACache cudaCache(640, 480, GlobalBundlingState::get().s_downsampledWidth, GlobalBundlingState::get().s_downsampledHeight,
		GlobalBundlingState::get().s_maxNumImages, mat4f::identity());
	std::cout << "loading cache from file... ";
	//loadCachedFramesFromSensor(&cudaCache, sensFile, submapSize, numKeys);
	std::cout << "done!" << std::endl;

	if (true) {
		//get residual corrs
		std::vector<EntryJ> correspondences(m_siftManager->getNumGlobalCorrespondences());
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(correspondences.data(), m_siftManager->getGlobalCorrespondencesGPU(), sizeof(EntryJ)*correspondences.size(), cudaMemcpyDeviceToHost));
		std::vector<EntryJ> testCorrs; unsigned int startTestIdx = (unsigned int)-1;
		for (unsigned int i = 0; i < correspondences.size(); i++) {
			if (correspondences[i].imgIdx_j == 241) {
				startTestIdx = i;
				break;
			}
		}
		MLIB_ASSERT(startTestIdx != (unsigned int)-1);
		testCorrs.insert(testCorrs.begin(), correspondences.begin() + startTestIdx, correspondences.end());
		correspondences.resize(startTestIdx);
		std::unordered_map<vec2ui, std::vector<EntryJ>> testCorrsByImagePair;
		for (const auto& c : testCorrs) {
			vec2ui imageIndices(c.imgIdx_i, c.imgIdx_j);
			auto it = testCorrsByImagePair.find(imageIndices);
			if (it == testCorrsByImagePair.end()) testCorrsByImagePair[imageIndices] = std::vector<EntryJ>(1, c);
			else it->second.push_back(c);
		}

		std::vector<vec2ui> imagePairs;
		for (const auto& a : testCorrsByImagePair) imagePairs.push_back(a.first);
		//add back 
		std::vector<unsigned int> idxs = { 0,2 }; //total 17
		for (unsigned int idx : idxs) {
			std::cout << "adding in image pair " << imagePairs[idx] << std::endl;
			const auto& newCorrs = testCorrsByImagePair[imagePairs[idx]];
			correspondences.insert(correspondences.end(), newCorrs.begin(), newCorrs.end());
		}
		m_siftManager->setGlobalCorrespondencesDEBUG(correspondences);
	}

	const unsigned int maxNumIters = 3; const unsigned int numPCGIts = 150;
	const bool useVerify = false;
	SBA sba;
	const unsigned int maxNumImages = GlobalBundlingState::get().s_maxNumImages;
	const unsigned int maxNumResiduals = MAX_MATCHES_PER_IMAGE_PAIR_FILTERED * (maxNumImages*(maxNumImages - 1)) / 2;
	sba.init(maxNumImages, maxNumResiduals);
	sba.setGlobalWeights(std::vector<float>(maxNumIters, 1.0f), std::vector<float>(maxNumIters, 0.0f), std::vector<float>(maxNumIters, 0.0f), false); //sparse only
	float4x4* d_transforms = NULL; MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_transforms, sizeof(float4x4) * numKeys));
	MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_transforms, keysTrajectory.data(), sizeof(float4x4)*numKeys, cudaMemcpyHostToDevice));
	//SiftVisualization::saveToPointCloud("debug/_init.ply", m_depthImages, m_colorImages, keysTrajectory, m_depthCalibration.m_IntrinsicInverse);
	sba.align(m_siftManager, &cudaCache, d_transforms, maxNumIters, numPCGIts, useVerify, false, false, true, true, false);
	MLIB_CUDA_SAFE_CALL(cudaMemcpy(keysTrajectory.data(), d_transforms, sizeof(float4x4)*keysTrajectory.size(), cudaMemcpyDeviceToHost));
	MLIB_CUDA_SAFE_FREE(d_transforms);
	for (unsigned int i = 0; i < numKeys; i++) {
		if (m_siftManager->getValidImages()[i] == 0) keysTrajectory[i].setZero(-std::numeric_limits<float>::infinity());
	}
	//SiftVisualization::saveToPointCloud("debug/_opt.ply", m_depthImages, m_colorImages, keysTrajectory, m_depthCalibration.m_IntrinsicInverse);
	//std::cout << "(press key to continue)" << std::endl;
	//getchar();

	if (true) {
		//get residual corrs
		std::vector<EntryJ> correspondences(m_siftManager->getNumGlobalCorrespondences());
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(correspondences.data(), m_siftManager->getGlobalCorrespondencesGPU(), sizeof(EntryJ)*correspondences.size(), cudaMemcpyDeviceToHost));
		//get residuals
		std::vector< std::pair<float, vec2ui> > residuals;
		for (unsigned int i = 0; i < correspondences.size(); i++) {
			const EntryJ& corr = correspondences[i];
			if (corr.isValid()) {
				vec3f res = keysTrajectory[corr.imgIdx_i] * vec3f(corr.pos_i.x, corr.pos_i.y, corr.pos_i.z) - keysTrajectory[corr.imgIdx_j] * vec3f(corr.pos_j.x, corr.pos_j.y, corr.pos_j.z);
				res = math::abs(res);
				float r = std::max(res.x, std::max(res.y, res.z));
				residuals.push_back(std::make_pair(r, vec2ui(corr.imgIdx_i, corr.imgIdx_j)));
			}
		} //correspondences
		std::sort(residuals.begin(), residuals.end(), [](const std::pair<float, vec2ui> &left, const std::pair<float, vec2ui> &right) {
			return fabs(left.first) > fabs(right.first);
		});
		std::cout << "max residual = " << residuals.front().first << " (" << residuals.front().second << ")" << std::endl;
		getchar();
		//find image-image 
		const float thresh = 0.05;//0.08f;
		std::unordered_map<vec2ui, float> imageImageResidualsSet;
		for (unsigned int i = 0; i < residuals.size(); i++) {
			if (residuals[i].first < thresh) break;
			auto it = imageImageResidualsSet.find(residuals[i].second);
			if (it == imageImageResidualsSet.end()) imageImageResidualsSet[residuals[i].second] = residuals[i].first;
			else it->second = std::max(it->second, residuals[i].first);
		}//residuals
		std::cout << "\t" << imageImageResidualsSet.size() << " image pairs with residual > " << thresh << std::endl;
		const unsigned int maxToPrint = 5;//20;
		residuals.clear();
		for (const auto& a : imageImageResidualsSet) residuals.push_back(std::make_pair(a.second, a.first));
		std::sort(residuals.begin(), residuals.end(), [](const std::pair<float, vec2ui> &left, const std::pair<float, vec2ui> &right) {
			return fabs(left.first) > fabs(right.first);
		});
		if (residuals.size() > maxToPrint) residuals.resize(maxToPrint);
		std::cout << "printing " << residuals.size() << " high residual" << std::endl;
		for (const auto& impair : residuals) {
			ColorImageR8G8B8 image1 = m_colorImages[impair.second.x];
			ColorImageR8G8B8 image2 = m_colorImages[impair.second.y];
			SiftVisualization::printMatch("debug/maxres/" + std::to_string((int)(100 * impair.first)) + "_" + std::to_string(impair.second.x) + "-" + std::to_string(impair.second.y) + ".png",
				impair.second, correspondences, image1, image2, m_colorCalibration.m_Intrinsic);
			const DepthImage32& depth1 = m_depthImages[impair.second.x];
			const DepthImage32& depth2 = m_depthImages[impair.second.y];
			image1.resize(depth1.getWidth(), depth1.getHeight());	image2.resize(depth2.getWidth(), depth2.getHeight());
			SiftVisualization::saveKeyMatchToPointCloud("debug/maxres/" + std::to_string((int)(100 * impair.first)) + "_" + std::to_string(impair.second.x) + "-" + std::to_string(impair.second.y),
				impair.second, correspondences, depth1, image1, depth2, image2, keysTrajectory, m_depthCalibration.m_IntrinsicInverse);
		} //print
		std::cout << "waiting..." << std::endl;
		getchar();
	}

	std::cout << "waiting..." << std::endl;
	getchar();
	//SBA sba;
	//const unsigned int maxNumImages = 11;
	//const unsigned int maxNumResiduals = MAX_MATCHES_PER_IMAGE_PAIR_FILTERED * (maxNumImages*(maxNumImages - 1)) / 2;
	//sba.init(maxNumImages, maxNumResiduals);

	//sba.setGlobalWeights(std::vector<float>(maxNumIters, 0.0f), std::vector<float>(maxNumIters, 1.0f), std::vector<float>(maxNumIters, 0.0f), true); //dense depth only
	//float4x4* d_transforms = NULL; MLIB_CUDA_SAFE_CALL(cudaMalloc(&d_transforms, sizeof(float4x4) * 2));
	//m_siftManager->setNumImagesDEBUG(2);
	//m_siftManager->setGlobalCorrespondencesDEBUG(std::vector<EntryJ>());
	//m_siftManager->updateGPUValidImages();
	//mat4f lastRigidTransform = mat4f::identity();
	//std::vector<mat4f> frame2frameTrajectory(1385, mat4f::identity());
	//CUDACache localCache(640, 480, GlobalBundlingState::get().s_downsampledWidth, GlobalBundlingState::get().s_downsampledHeight,
	//	2, m_depthCalibration.m_Intrinsic);
	//for (unsigned int k = 1; k < 1385; k++) {
	//	localCache.setCurrentFrame(0);
	//	localCache.copyCacheFrameFrom(&cudaCache, k - 1);
	//	localCache.copyCacheFrameFrom(&cudaCache, k);
	//	std::vector<mat4f> initTransforms(2, mat4f::identity());
	//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_transforms, initTransforms.data(), sizeof(float4x4)*initTransforms.size(), cudaMemcpyHostToDevice));
	//	sba.align(m_siftManager, &localCache, d_transforms, maxNumIters, numPCGIts, useVerify, false, false, false, false, false);
	//	MLIB_CUDA_SAFE_CALL(cudaMemcpy(initTransforms.data(), d_transforms, sizeof(float4x4)*initTransforms.size(), cudaMemcpyDeviceToHost));
	//	lastRigidTransform = lastRigidTransform * initTransforms[1];
	//	frame2frameTrajectory[k] = lastRigidTransform;
	//}
	//MLIB_CUDA_SAFE_FREE(d_transforms);
	//BinaryDataStreamFile sOut("debug/frame2frame.bin", true);
	//sOut << frame2frameTrajectory;
	//sOut.closeStream();
	//SiftVisualization::saveToPointCloud("debug/_test.ply", m_depthImages, m_colorImages, frame2frameTrajectory, m_depthCalibration.m_IntrinsicInverse, 20);
	//std::cout << "waiting..." << std::endl;
	//getchar();
}


