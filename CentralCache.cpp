#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_inst;

//��page cache��ȡһ��span
Span* CentralCache::GetOneSpan(SpanList& spanlist, size_t byte_size)
{
	Span* span = spanlist.Begin();
	while (span != spanlist.End())//��ǰ�ҵ�һ��span
	{
		if (span->_list != nullptr) //�ҵ�����װ��obj��list��Ϊ�յ�һ��span��span�е�obj��ȡ��֮��᲻�ϸ�����_list��
			return span;
		else
			span = span->_next;
	}


	////���Դ�׮
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



	// �ߵ������˵��ǰ��û�л�ȡ��span,���ǿյģ�����һ��pagecache��ȡspan������bytesize�Լ�֮ǰ��NumMovePage����
	Span* newspan = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(byte_size));
	// ��spanҳ�зֳ���Ҫ�Ķ�����������
	char* cur = (char*)(newspan->_pageid << PAGE_SHIFT); //CSY����õ�ַ
	char* end = cur + (newspan->_npage << PAGE_SHIFT);
	newspan->_list = cur;// CSY�� ����������newspan��
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


//��ȡһ���������ڴ���� CSY: һ����thread cache�е�FetchFromCentralCache����������̵߳������ȡ�ڴ����
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t n, size_t byte_size)//CSY����ߴ����start ��end ָ�룬ֱ�������ã������޸�
{
	size_t index = SizeClass::Index(byte_size); //CSY��������Ҫ���ĸ�spanlist��ȡ
	SpanList& spanlist = _spanlist[index];//��ֵ->�������� //CSY��������õ��� spanlist���͵�һ������

	////��ʱ��ǵü���
	//spanlist.Lock(); //CSY���������õ����ֶ��������������������һ��unique_lock������
	std::unique_lock<std::mutex> lock(spanlist._mutex);


	Span* span = GetOneSpan(spanlist, byte_size);
	//������Ѿ���ȡ��һ��newspan

	//��span�л�ȡrange����
	size_t batchsize = 0;//CSY��ʵ����ȡ���Ķ�������
	void* prev = nullptr;//��ǰ����ǰһ��
	void* cur = span->_list;//��cur��������������
	for (size_t i = 0; i < n; ++i)
	{
		prev = cur;
		cur = NEXT_OBJ(cur); //CSY�� NEXT_OBJ�ķ���Ҳ��γ����ˣ�������һ��obj�������һ��obj�ĵ�ַ
		++batchsize;
		if (cur == nullptr)//��ʱ�ж�cur�Ƿ�Ϊ�գ�Ϊ�յĻ�����ǰֹͣ
			break;
	}

	start = span->_list;
	end = prev;

	span->_list = cur; //���µ�ǰspan��prev��֮ǰ�Ķ���ȡ����
	span->_usecount += batchsize;

	//���յ�span�Ƶ���󣬱��ַǿյ�span��ǰ�� //CSY������ȡ��ʱ����ʹ��GetOneSpan������ȡ��
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

	//��������ѭ������
	// CentralCache:�Ե�ǰͰ���м���(Ͱ��)����С��������
	// PageCache:���������SpanListȫ�ּ���
	// ��Ϊ���ܴ��ڶ���߳�ͬʱȥϵͳ�����ڴ�����
	//spanlist.Lock();
	std::unique_lock<std::mutex> lock(spanlist._mutex);

	while (start)
	{
		void* next = NEXT_OBJ(start);

		////��ʱ��ǵü���
		//spanlist.Lock(); // �����˺ܶ��������

		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		NEXT_OBJ(start) = span->_list;
		span->_list = start;//CSY���ָ���һ������ص�span����Ķ���������

		//��һ��span�Ķ���ȫ���ͷŻ�����ʱ�򣬽�span����pagecache,������ҳ�ϲ�
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