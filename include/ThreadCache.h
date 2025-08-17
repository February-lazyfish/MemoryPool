#pragma once
#include "Common.h"
#include <cstddef>

namespace MemoryPool
{

//Thread-local cache
	class ThreadCache{
	public:
		static ThreadCache* getInstance(){
			// thread_local: each thread has its own independent copy of the variables.
			static thread_local ThreadCache instance;
			return &instance;
		}
		void* allocate(size_t size);
		void deallocate(void* ptr,size_t size);

	private:
		ThreadCache() = default;
		//secure memory block from central cache	
		void fetchFromCentralCache(size_t index);
		//return the meory block to the central cache
		void returnToCentralCache(void * start,size_t size);
		//calculate the number of memory blocks obtained in batches
		size_t getBatchNum(size_t size);
		//determine whether to return the memory to the central cahche
		bool shouldReturnToCentralCache(size_t index);
	private:
		std::array<void*,FREE_LIST_SIZE> freeList_;
		std::array<size_t,FREE_LIST_SIZE> freeListSize_;

	};	
}
