#include "../include/PageCache.h"
#include <bits/types/struct_sched_param.h>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <ratio>
#include <sys/mman.h>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <algorithm>

namespace MemoryPool
{
	PageCache::PageCache(){
		startBackgroundThread();
	}
	PageCache::~PageCache(){
		stopBackgroundThread();
	}
	//secure a static instance
	PageCache& PageCache::getInstance(){
		static PageCache instance;
		return instance;
	}

	void PageCache::startBackgroundThread(){
		std::lock_guard<std::mutex> lock(threadMutex_);
		if(!cleanupThread_.joinable()){
			cleanupThread_ = std::thread(&PageCache::backgroundWork,this);
		}
	}  //线程开始
	void PageCache::stopBackgroundThread(){
		{
			std::lock_guard<std::mutex> lock(threadMutex_);
			running_ = false;
		}
		if(cleanupThread_.joinable()){
			cleanupThread_.join();
		}
	}    //线程终止
	void PageCache::backgroundWork(){
		while(running_){
			std::this_thread::sleep_for(std::chrono::seconds(5));
			std::lock_guard<std::mutex> lock(mutex_);
			if(running_) releaseIdleSpans();
		}
	}//线程工作函数
/**
 * 分配Span核心逻辑：
 * 1. 优先从空闲链表中获取
 * 2. 不足时向系统申请
 * 3. 大Span分割时维护映射关系
 */
	void* PageCache::allocateSpan(size_t numPage){
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = freeSpans_.lower_bound(numPage);
		if(it!=freeSpans_.end()){
			Span* span = it->second;
			if(span->next){
				freeSpans_[it->first] = span->next;
			}else{
				freeSpans_.erase(it);	
			}

			if(span->numPages > numPage){
				Span * newSpan = new Span();
				newSpan->pageAddr = static_cast<char*>(span->pageAddr) + span->numPages*PAGE_SIZE;
				newSpan->numPages = span->numPages - numPage;
				newSpan->next = nullptr;	

				auto & list = freeSpans_[newSpan->numPages];
				newSpan->next = list;
				list = newSpan;

				for(size_t i = 0;i<newSpan->numPages;++i){
					void* adrr = static_cast<char*>(newSpan->pageAddr) + i*PAGE_SIZE;
					spanMap_[adrr] = newSpan;
				}
				span->numPages = numPage;
			}
			spanMap_[span->pageAddr] = span;
			return span->pageAddr;
		}

		void* memory = systemAlloc(numPage);
		if(!memory) return nullptr;
		Span* span = new Span();
		span->pageAddr = memory;
		span->numPages = numPage;
		span->next = nullptr;
		totalCachedPages_+=numPage;

		for(size_t i = 0;i<numPage;++i){
			void* addr = static_cast<char*>(memory) + i*PAGE_SIZE;
			spanMap_[addr] = span;
		}
		return memory;
	}
/**
 * 释放Span核心逻辑：
 * 1. 合并前后相邻的空闲Span
 * 2. 更新LRU信息
 * 3. 触发释放检查
 */
	void PageCache::deallocateSpan(void* ptr,size_t numPage){
		if(!ptr || numPage == 0) return;
		std::lock_guard<std::mutex> lock(mutex_);

		auto it = spanMap_.find(ptr);
		if(it == spanMap_.end()) return;
		Span* span = it->second;
 /* ---- 前向合并 ---- */
		void* prevAddr = static_cast<char*>(ptr) - PAGE_SIZE;
		auto prevIt = spanMap_.find(prevAddr);
		if(prevIt != spanMap_.end()){
			Span* prevSpan = prevIt->second;
			if(removeFromFreeList(prevSpan)){
				prevSpan->numPages += span->numPages;
				spanMap_.erase(ptr);
				delete span;
				span = prevSpan;
			}
		}
		/* ---- 后向合并 ---- */
		void * nextAddr = static_cast<char*>(ptr) + numPage*PAGE_SIZE;
		auto nextIt = spanMap_.find(nextAddr);
		if(nextIt != spanMap_.end()){
			Span * nextSpan = nextIt->second;
			if(removeFromFreeList(nextSpan)){
				span->numPages += nextSpan->numPages;
				spanMap_.erase(nextAddr);
				delete nextSpan;
			}
		}
 /* ---- 更新LRU ---- */
		span->lastFreeTime = std::chrono::steady_clock::now();
		lruQueue_.push_front(span);
		lruMap_[span] = lruQueue_.begin();
/* ---- 更新空闲列表 ---- */
		auto & list = freeSpans_[span->numPages];
		span->next = list;
		list = span;
		totalCachedPages_ += span->numPages;
/*----------紧急情况立即触发-----------*/
		if(totalCachedPages_ > MAX_CACHE_PAGES * 1.5){
			releaseIdleSpans();
		}	
	}

	/**
	 * 释放空闲Span（双条件触发）
	 * 条件1：缓存总数 > MAX_CACHE_PAGES
	 * 条件2：Span空闲时间 > 30秒
	 * 策略：从LRU尾部开始释放直到满足条件
	 */
	void PageCache::releaseIdleSpans(){
		std::lock_guard<std::mutex> lock(mutex_);
		auto now = std::chrono::steady_clock::now();
		size_t releasePages = 0;
		const size_t TARGET_RELEASE = MAX_CACHE_PAGES / 3;

		while(!lruQueue_.empty() && releasePages < TARGET_RELEASE){
			Span * span = lruQueue_.back();

			//双条件检查
			bool overThreshold = totalCachedPages_ > MAX_CACHE_PAGES;
			bool timeoutExpired = (now-span->lastFreeTime) > IDLE_TIMEOUT;
			if(overThreshold && timeoutExpired){
				lruQueue_.pop_back();
				lruMap_.erase(span);

				if(!removeFromFreeList(span)) continue;
				systemDealloc(span->pageAddr,span->numPages);
				releasePages += span->numPages;
				totalCachedPages_ -= span->numPages;

				//清理映射关系
				for(size_t i = 0;i<span->numPages;++i){
					void* addr = static_cast<char*>(span->pageAddr) + i*PAGE_SIZE;
					spanMap_.erase(addr);
				}
				delete span;
			}
		}
	}

	bool PageCache::removeFromFreeList(Span* span){    // 从空闲链表移除指定Span
		auto & list = freeSpans_[span->numPages];
		if(list == span){
			list = span->next;
			return true;
		}
		
		Span* curr = list;
		while(curr && curr->next){
			if(curr->next == span){
				curr->next = span->next;
				return true;
			}
			curr = curr->next;
		}
		return false;
	}

	void* PageCache::systemAlloc(size_t numPages){
		size_t size = numPages*PAGE_SIZE;
		void* ptr = mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
		if(ptr==MAP_FAILED) return nullptr;
		madvise(ptr,size,MADV_WILLNEED);
		return ptr;
	}

	void PageCache::systemDealloc(void* ptr,size_t numPages){
		size_t size = numPages * PAGE_SIZE;
		madvise(ptr,size,MADV_DONTNEED);
		munmap(ptr,size);
	}
}
