#pragma once
#include <cstddef>
#include <array>
#include <atomic>

namespace MemoryPool
{
// define align and size
constexpr size_t ALIGNMENT = 8;  // Align to mutiples of 8
// A method will adopt the default allocation method of the operating
// system if it exceeds this threshold
constexpr size_t MAX_BYTES = 256 * 1024;  
constexpr size_t FREE_LIST_SIZE = MAX_BYTES / ALIGNMENT; // computer the size of the FreeLink

// define the head of the memory blcok 
struct BlockHeader{
	size_t size;    //the size of memory block
	bool inUse;    // the flag of used
	BlockHeader* next;
};
// manage the classes of different sizes
class SizeClass{
public:
	/*
	 * for example: 19
	 *    1 0 0 1 1 
	 *    17 + 8 - 1
	 *    
	 *    1 0 0 1 1
	 *    0 0 1 1 1
	 *    1 1 0 1 0
	 *
	 *    ~ 7 = 11000
	 *  & 11000 = 11000 align to 24
	 * */
	static size_t roundUp(size_t bytes){
		return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
	}
	/* first, we need to determine which of bytes and ALIGNMENT is larger
	 * second, we need to find index through and return this value
	 * the method has similarities with the one mentioned above
	 * */
	static size_t getIndex(size_t bytes){
		bytes = std::max(bytes,ALIGNMENT);
		return (bytes + ALIGNMENT - 1) / ALIGNMENT - 1;
	}
};
}

