#pragma once

#include "Common.h"

//上面的ThreadCache里面没有的话，要从中心获取

/*
进行资源的均衡，对于ThreadCache的某个资源过剩的时候，可以回收ThreadCache内部的的内存
从而可以分配给其他的ThreadCache
只有一个中心缓存，对于所有的线程来获取内存的时候都应该是一个中心缓存
所以对于中心缓存可以使用单例模式来进行创建中心缓存的类
对于中心缓存来说要加锁
*/

//设计成单例模式  CSY：单例模式的饿汉版，在主函数之前就初始化，可以避免多线程竞争。
class CentralCache
{
public:
	static CentralCache* Getinstance()
	{
		return &_inst;
	}

	//从page cache获取一个span
	Span* GetOneSpan(SpanList& spanlist, size_t byte_size);

	//从中心缓存获取一定数量的对象给thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t n, size_t byte_size);

	//将一定数量的对象释放给span跨度
	void ReleaseListToSpans(void* start, size_t size);

private:
	SpanList _spanlist[NLISTS]; //CSY： 在common.h中定义了NLISTS为184，最高的应该是64*1024，一个span才1页（4k）的大小，怎么够用呢？-->184足够内存对齐到64*1024, 一个span不止1页，项目里定义的NPAGES应该是128

private:
	CentralCache() {}//声明不实现，防止默认构造，自己创建
	CentralCache(CentralCache&) = delete;

	static CentralCache _inst;
};