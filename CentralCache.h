#pragma once

#include "Common.h"

//�����ThreadCache����û�еĻ���Ҫ�����Ļ�ȡ

/*
������Դ�ľ��⣬����ThreadCache��ĳ����Դ��ʣ��ʱ�򣬿��Ի���ThreadCache�ڲ��ĵ��ڴ�
�Ӷ����Է����������ThreadCache
ֻ��һ�����Ļ��棬�������е��߳�����ȡ�ڴ��ʱ��Ӧ����һ�����Ļ���
���Զ������Ļ������ʹ�õ���ģʽ�����д������Ļ������
�������Ļ�����˵Ҫ����
*/

//��Ƴɵ���ģʽ  CSY������ģʽ�Ķ����棬��������֮ǰ�ͳ�ʼ�������Ա�����߳̾�����
class CentralCache
{
public:
	static CentralCache* Getinstance()
	{
		return &_inst;
	}

	//��page cache��ȡһ��span
	Span* GetOneSpan(SpanList& spanlist, size_t byte_size);

	//�����Ļ����ȡһ�������Ķ����thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t n, size_t byte_size);

	//��һ�������Ķ����ͷŸ�span���
	void ReleaseListToSpans(void* start, size_t size);

private:
	SpanList _spanlist[NLISTS]; //CSY�� ��common.h�ж�����NLISTSΪ184����ߵ�Ӧ����64*1024��һ��span��1ҳ��4k���Ĵ�С����ô�����أ�-->184�㹻�ڴ���뵽64*1024, һ��span��ֹ1ҳ����Ŀ�ﶨ���NPAGESӦ����128

private:
	CentralCache() {}//������ʵ�֣���ֹĬ�Ϲ��죬�Լ�����
	CentralCache(CentralCache&) = delete;

	static CentralCache _inst;
};