#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_inst;

//从page cache获取一个span
Span* CentralCache::GetOneSpan(SpanList& spanlist, size_t byte_size)
{
	Span* span = spanlist.Begin();
	while (span != spanlist.End())//当前找到一个span
	{
		if (span->_list != nullptr) //找到其中装有obj的list不为空的一个span（span中的obj被取走之后会不断更新其_list）
			return span;
		else
			span = span->_next;
	}


	////测试打桩
	//Span* newspan = new Span;
	//newspan->_objsize = 16;
	//void* ptr = malloc(16 * 8);
	//void* cur = ptr;
	//for (size_t i = 0; i < 7; ++i)
	//{
	//	void* next = (char*)cur + 16;
	//	NEXT_OBJ(cur) = next;
	//	cur = next;
	//}
	//NEXT_OBJ(cur) = nullptr;
	//newspan->_list = ptr;



	// 走到这儿，说明前面没有获取到span,都是空的，到下一层pagecache获取span，根据bytesize以及之前的NumMovePage函数
	Span* newspan = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(byte_size));
	// 将span页切分成需要的对象并链接起来
	char* cur = (char*)(newspan->_pageid << PAGE_SHIFT); //CSY：获得地址
	char* end = cur + (newspan->_npage << PAGE_SHIFT);
	newspan->_list = cur;// CSY： 将对象块挂在newspan上
	newspan->_objsize = byte_size;

	while (cur + byte_size < end)
	{
		char* next = cur + byte_size;
		NEXT_OBJ(cur) = next;
		cur = next;
	}
	NEXT_OBJ(cur) = nullptr;

	spanlist.PushFront(newspan);

	return newspan;
}


//获取一个批量的内存对象 CSY: 一般由thread cache中的FetchFromCentralCache引用来完成线程到中央的取内存过程
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t n, size_t byte_size)//CSY：这边传入的start 和end 指针，直接是引用，可以修改
{
	size_t index = SizeClass::Index(byte_size); //CSY：计算需要从哪个spanlist上取
	SpanList& spanlist = _spanlist[index];//赋值->拷贝构造 //CSY：这里采用的是 spanlist类型的一个引用

	////到时候记得加锁
	//spanlist.Lock(); //CSY：本来采用的是手动加锁，现在这里采用了一个unique_lock来管理
	std::unique_lock<std::mutex> lock(spanlist._mutex);


	Span* span = GetOneSpan(spanlist, byte_size);
	//到这儿已经获取到一个newspan

	//从span中获取range对象
	size_t batchsize = 0;//CSY：实际能取出的对象数量
	void* prev = nullptr;//提前保存前一个
	void* cur = span->_list;//用cur来遍历，往后走
	for (size_t i = 0; i < n; ++i)
	{
		prev = cur;
		cur = NEXT_OBJ(cur); //CSY： NEXT_OBJ的方法也多次出现了，就是在一个obj里存了下一个obj的地址
		++batchsize;
		if (cur == nullptr)//随时判断cur是否为空，为空的话，提前停止
			break;
	}

	start = span->_list;
	end = prev;

	span->_list = cur; //更新当前span，prev及之前的都被取走了
	span->_usecount += batchsize;

	//将空的span移到最后，保持非空的span在前面 //CSY：本来取的时候是使用GetOneSpan函数获取的
	if (span->_list == nullptr)
	{
		spanlist.Erase(span);
		spanlist.PushBack(span);
	}

	//spanlist.Unlock();

	return batchsize;
}

void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	size_t index = SizeClass::Index(size);
	SpanList& spanlist = _spanlist[index];

	//将锁放在循环外面
	// CentralCache:对当前桶进行加锁(桶锁)，减小锁的粒度
	// PageCache:必须对整个SpanList全局加锁
	// 因为可能存在多个线程同时去系统申请内存的情况
	//spanlist.Lock();
	std::unique_lock<std::mutex> lock(spanlist._mutex);

	while (start)
	{
		void* next = NEXT_OBJ(start);

		////到时候记得加锁
		//spanlist.Lock(); // 构成了很多的锁竞争

		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		NEXT_OBJ(start) = span->_list;
		span->_list = start;//CSY：恢复了一个对象回到span下面的对象链表中

		//当一个span的对象全部释放回来的时候，将span还给pagecache,并且做页合并
		if (--span->_usecount == 0)
		{
			spanlist.Erase(span);
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		}

		//spanlist.Unlock();

		start = next;
	}

	//spanlist.Unlock();
}