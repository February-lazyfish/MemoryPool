#pragma once
#include <atomic>
#include <map>
#include <mutex>
#include <list>
#include <type_traits>
#include <unordered_map>
#include <chrono>
#include <thread>

namespace MemoryPool 
{
/**
 * 系统级页缓存管理器（单例模式）
 * 职责：
 * 1. 以4KB页为单位管理系统内存
 * 2. 合并/分割Span满足不同大小需求
 * 3. 按LRU策略超时释放内存
 */
class PageCache {
public:
    static const size_t PAGE_SIZE = 4096; // 页大小固定4KB（x86_64系统标准页大小）

    // 获取单例实例（线程安全）
    static PageCache& getInstance();

    // 核心接口
    void* allocateSpan(size_t numPages);    // 分配指定页数的内存块
    void deallocateSpan(void* ptr, size_t numPages); // 释放内存块

private:
    /**
     * 内存块描述结构
     * 关键字段：
     * - pageAddr: 内存块起始地址（始终按PAGE_SIZE对齐）
     * - numPages: 连续页数
     * - lastFreeTime: 最后释放时间戳（用于LRU判断）
     */
    struct Span {
        void* pageAddr;                     // 内存块起始地址
        size_t numPages;                    // 包含的页数
        Span* next;                         // 空闲链表指针
        std::chrono::steady_clock::time_point lastFreeTime; // 最后释放时间
    };

    PageCache();  // 私有构造函数（单例模式）
	~PageCache();
    
	void startBackgroundThread();  //线程开始
    void stopBackgroundThread();    //线程终止
    void backgroundWork();     //线程工作函数
	
    // 系统级内存操作
    void* systemAlloc(size_t numPages);     // 从OS申请内存
    void systemDealloc(void* ptr, size_t numPages); // 归还内存给OS

    // 内存释放策略
    void releaseIdleSpans();                // 释放空闲Span（双条件触发）
    bool removeFromFreeList(Span* span);    // 从空闲链表移除指定Span

    // 内存管理数据结构
    std::map<size_t, Span*> freeSpans_;     // 按页数分组的空闲Span链表
    std::map<void*, Span*> spanMap_;        // 页地址到Span的映射（用于合并）
    std::mutex mutex_;                      // 全局互斥锁

    // LRU相关结构
    std::list<Span*> lruQueue_;             // LRU队列（头部最新，尾部最旧）
    std::unordered_map<Span*, std::list<Span*>::iterator> lruMap_; // 快速定位
    
    // 策略参数
    size_t totalCachedPages_ = 0;           // 当前缓存的总页数
    const size_t MAX_CACHE_PAGES = 1024;    // 缓存页数阈值（4MB）
    const std::chrono::seconds IDLE_TIMEOUT{30}; // 空闲超时时间
	

    std::thread cleanupThread_;  //创建一个线程来清理内存
    std::atomic<bool> running_{true};  //结束标志
    std::mutex threadMutex_;   //线程锁防止多个线程同时进入到清理函数中
};
} // namespace Kama_memoryPool
