#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include "Common.h"

namespace MemoryPool
{
	//使用无锁span信息存储	
	struct SpanTracker{
		std::atomic<void*> spanAddr{nullptr};
		std::atomic<size_t> numPage{0};
		std::atomic<size_t> blockCount{0};
		std::atomic<size_t> freeCount{0}; //用于追踪span还有多少剩余块
	};

	class CentralCache{
		//this a test
	}


}
