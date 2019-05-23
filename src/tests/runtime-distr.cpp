
#include <thread>
#include "utility.hpp"
#include "stopwatch.hpp"
#include "../dataset.hpp"
#include "../vm_compiled.hpp"
#include "../blake2/blake2.h"

struct Outlier {
	Outlier(int idx, double rtime) : index(idx), runtime(rtime) {}
	int index;
	double runtime;
};

int main(int argc, char** argv) {
	constexpr int distributionSize = 100;
	int distribution[distributionSize + 1] = { 0 };
	Stopwatch sw;
	alignas(16) uint8_t hash[64];
	
	double totalRuntime = 0;
	double maxRuntime = 0;
	std::vector<Outlier> outliers;
	randomx_flags flags = (randomx_flags)(RANDOMX_FLAG_FULL_MEM | RANDOMX_FLAG_JIT);

	bool softAes, largePages;
	int totalCount, initThreadCount;
	double binSize, offset;
	int32_t seed;

	readOption("--softAes", argc, argv, softAes);
	readIntOption("--nonces", argc, argv, totalCount, 10000);
	readIntOption("--init", argc, argv, initThreadCount, 1);
	readFloatOption("--binSize", argc, argv, binSize, 10e-6);
	readFloatOption("--offset", argc, argv, offset, 100e-6);
	readIntOption("--seed", argc, argv, seed, 0);
	readOption("--largePages", argc, argv, largePages);

	if (softAes) {
		std::cout << " - software AES mode" << std::endl;
	}
	else {
		flags = (randomx_flags)(flags | RANDOMX_FLAG_HARD_AES);
		std::cout << " - hardware AES mode" << std::endl;
	}

	if (largePages) {
		flags = (randomx_flags)(flags | RANDOMX_FLAG_LARGE_PAGES);
		std::cout << " - large pages mode" << std::endl;
	}
	else {
		std::cout << " - small pages mode" << std::endl;
	}

	std::cout << "Initializing..." << std::endl;

	blake2b(&hash, sizeof hash, &seed, sizeof seed, nullptr, 0);

	randomx_cache *cache = randomx_alloc_cache(flags);
	if (cache == nullptr) {
		std::cout << "Cache allocation failed" << std::endl;
		return 1;
	}
	randomx_init_cache(cache, &seed, sizeof seed);

	randomx_dataset *dataset = randomx_alloc_dataset(flags);
	if (dataset == nullptr) {
		std::cout << "Dataset allocation failed" << std::endl;
		return 1;
	}

	std::vector<std::thread> threads;
	uint32_t datasetItemCount = randomx_dataset_item_count();
	if (initThreadCount > 1) {
		auto perThread = datasetItemCount / initThreadCount;
		auto remainder = datasetItemCount % initThreadCount;
		uint32_t startItem = 0;
		for (int i = 0; i < initThreadCount; ++i) {
			auto count = perThread + (i == initThreadCount - 1 ? remainder : 0);
			threads.push_back(std::thread(&randomx_init_dataset, dataset, cache, startItem, count));
			startItem += count;
		}
		for (unsigned i = 0; i < threads.size(); ++i) {
			threads[i].join();
		}
	}
	else {
		randomx_init_dataset(dataset, cache, 0, datasetItemCount);
	}
	randomx_release_cache(cache);

	std::cout << "Running " << totalCount << " programs..." << std::endl;

	randomx_vm* vm = randomx_create_vm(flags, nullptr, dataset);
	vm->initScratchpad(&hash);
	vm->resetRoundingMode();

	for (int i = 0; i < totalCount; ++i) {
		sw.restart();
		vm->run(&hash);
		double elapsed = sw.getElapsed();
		//std::cout << "Elapsed: " << elapsed << std::endl;
		totalRuntime += elapsed;
		if (elapsed > maxRuntime)
			maxRuntime = elapsed;
		int bin = (elapsed - offset) / binSize;
		if (bin < 0)
			bin = 0;
		if (bin > distributionSize) {
			bin = distributionSize;
			outliers.push_back(Outlier(i, elapsed));
		}
		distribution[bin]++;
		blake2b(hash, sizeof(hash), vm->getRegisterFile(), sizeof(randomx::RegisterFile), nullptr, 0);
	}

	for (int i = 0; i < distributionSize + 1; ++i) {
		std::cout << i << " " << distribution[i] << std::endl;
	}

	std::cout << "Average runtime: " << totalRuntime / totalCount << std::endl;
	std::cout << "Maximum runtime: " << maxRuntime << std::endl;

	std::cout << "Outliers: " << std::endl;

	for (Outlier& ol : outliers) {
		std::cout << " " << ol.index << ": " << ol.runtime << std::endl;
	}

	return 0;
}