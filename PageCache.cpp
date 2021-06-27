#include "PageCache.h"

PageCache PageCache::_inst;


//大对象申请，直接从系统
Span* PageCache::AllocBigPageObj(size_t size)
{
	assert(size > MAX_BYTES);

	size = SizeClass::_Roundup(size, PAGE_SHIFT); //对齐
	size_t npage = size >> PAGE_SHIFT;
	if (npage < NPAGES) //如果npage < 129 
	{
		Span* span = NewSpan(npage);
		span->_objsize = size;
		
		return span;
	}
	else
	{
		void* ptr = VirtualAlloc(0, npage << PAGE_SHIFT,
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

		if (ptr == nullptr)
			throw std::bad_alloc();

		Span* span = new Span;
		span->_npage = npage;
		span->_pageid = (PageID)ptr >> PAGE_SHIFT;
		span->_objsize = npage << PAGE_SHIFT;

		_idspanmap[span->_pageid] = span;

		return span;
	}
}

void PageCache::FreeBigPageObj(void* ptr, Span* span)
{
	size_t npage = span->_objsize >> PAGE_SHIFT;
	if (npage < NPAGES) //相当于还是小于128页
	{
		span->_objsize = 0;
		ReleaseSpanToPageCache(span); //还给PageCache
	}
	else
	{
		_idspanmap.erase(npage);
		delete span;
		VirtualFree(ptr, 0, MEM_RELEASE); //CSY：为什么这里释放的内存大小是0呢？
	}
}

Span* PageCache::NewSpan(size_t n)
{
	// 加锁，防止多个线程同时到PageCache中申请span
	// 这里必须是给全局加锁，不能单独的给每个桶加锁
	// 如果对应桶没有span,是需要向系统申请的
	// 可能存在多个线程同时向系统申请内存的可能
	std::unique_lock<std::mutex> lock(_mutex); //CSY:unique_lock 类似已知的lock_guard ，也是一个自动上锁，以及出作用域后自动解锁的类。

	return _NewSpan(n);
}



Span* PageCache::_NewSpan(size_t n)
{
	assert(n < NPAGES);
	if (!_spanlist[n].Empty()) //不为空，则将spanlist管理的第一个span给返回
		return _spanlist[n].PopFront();

	//CSY：往下执行说明上面对应是empty的
	for (size_t i = n + 1; i < NPAGES; ++i)//CSY：如果最匹配的没有，则去找稍大的
	{
		if (!_spanlist[i].Empty())  
		{
			Span* span = _spanlist[i].PopFront();
			Span* splist = new Span;

			splist->_pageid = span->_pageid;
			splist->_npage = n;
			span->_pageid = span->_pageid + n;//CSY：做一个切分，页号为原页号+n
			span->_npage = span->_npage - n; //页的大小为原大小i - n

			//splist->_pageid = span->_pageid + n;
			//span->_npage = splist->_npage - n;
			//span->_npage = n;

			for (size_t i = 0; i < n; ++i)
				_idspanmap[splist->_pageid + i] = splist; //CSY：_idspanmap 是以PageID为键值的哈希表
			//CSY：将新分割出来的页映射到新的span上，其中有n页，每一页都是映射到splist这个span

			//_spanlist[splist->_npage].PushFront(splist);
			//return span;

			_spanlist[span->_npage].PushFront(span);//CSY：挂回对应的页数的span位置
			return splist;
		}
	}

	Span* span = new Span;

	// 到这里说明SpanList中没有合适的span,只能向系统申请128页的内存
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, (NPAGES - 1) * (1 << PAGE_SHIFT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	//  brk
#endif


	span->_pageid = (PageID)ptr >> PAGE_SHIFT;
	span->_npage = NPAGES - 1;

	for (size_t i = 0; i < span->_npage; ++i)
		_idspanmap[span->_pageid + i] = span;

	_spanlist[span->_npage].PushFront(span);  //方括号
	return _NewSpan(n);// CSY：系统调用之后，重新执行，这一次应该有合适的span了
}

// 获取从对象到span的映射
Span* PageCache::MapObjectToSpan(void* obj)
{
	//计算页号
	PageID id = (PageID)obj >> PAGE_SHIFT;
	auto it = _idspanmap.find(id);
	if (it != _idspanmap.end())
	{
		return it->second;
	}
	else
	{
		assert(false);
		return nullptr;
	}
}

void PageCache::ReleaseSpanToPageCache(Span* cur)
{
	// 必须上全局锁,可能多个线程一起从ThreadCache中归还数据
	std::unique_lock<std::mutex> lock(_mutex);


	// 当释放的内存是大于128页,直接将内存归还给操作系统,不能合并
	if (cur->_npage >= NPAGES)
	{
		void* ptr = (void*)(cur->_pageid << PAGE_SHIFT);
		// 归还之前删除掉页到span的映射
		_idspanmap.erase(cur->_pageid);
		VirtualFree(ptr, 0, MEM_RELEASE);
		delete cur;
		return;
	}


	// 向前合并
	while (1)
	{
		////超过128页则不合并
		//if (cur->_npage > NPAGES - 1)
		//	break;

		PageID curid = cur->_pageid;
		PageID previd = curid - 1;
		auto it = _idspanmap.find(previd);

		// 没有找到
		if (it == _idspanmap.end())
			break;

		// 前一个span不空闲
		if (it->second->_usecount != 0)
			break;

		Span* prev = it->second;

		//超过128页则不合并
		if (cur->_npage + prev->_npage > NPAGES - 1)
			break;

		// 先把prev从链表中移除
		_spanlist[prev->_npage].Erase(prev);

		// 合并
		prev->_npage += cur->_npage;
		//修正id->span的映射关系
		for (PageID i = 0; i < cur->_npage; ++i)
		{
			_idspanmap[cur->_pageid + i] = prev;
		}
		delete cur;

		// 继续向前合并
		cur = prev;
	}


	//向后合并
	while (1)
	{
		////超过128页则不合并
		//if (cur->_npage > NPAGES - 1)
		//	break;

		PageID curid = cur->_pageid;
		PageID nextid = curid + cur->_npage;
		//std::map<PageID, Span*>::iterator it = _idspanmap.find(nextid);
		auto it = _idspanmap.find(nextid);

		if (it == _idspanmap.end()) //CSY：找不到映射
			break;

		if (it->second->_usecount != 0) //CSY：找到的span不为空
			break;

		Span* next = it->second;

		//超过128页则不合并
		if (cur->_npage + next->_npage >= NPAGES - 1)
			break;

		_spanlist[next->_npage].Erase(next);

		cur->_npage += next->_npage;
		//修正id->Span的映射关系
		for (PageID i = 0; i < next->_npage; ++i)
		{
			_idspanmap[next->_pageid + i] = cur;
		}

		delete next;
	}

	// 最后将合并好的span插入到span链中
	_spanlist[cur->_npage].PushFront(cur);
}
