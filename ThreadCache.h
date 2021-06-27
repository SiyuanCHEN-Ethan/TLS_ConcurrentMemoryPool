#pragma once

#include "common.h"

class ThreadCache
{
private:
	Freelist _freelist[NLISTS];//������ÿ���߳̾��е���������,�������С�ڴ��

public:
	//������ͷ��ڴ�
	void* Allocate(size_t size);
	void Dealloctor(void* ptr,size_t size);

	//��central cache��ȡ����
	void* FetchFromCentralCache(size_t index, size_t size);

	//�ͷŶ���ʱ������������������ڴ�ص����Ķ�  // CSYע�����Ķ�ָCentralCache
	void ListTooLong(Freelist* list, size_t size);


};

//��̬�� �������пɼ�
//ÿ���߳��и��Լ���ָ�룬��(_declspec(thread)), ������ʹ��ʱ��ÿ���������Լ��ģ��Ͳ��ü����� //��
//CSY�� _declspec(thread)����ʽ����һ���̱߳��ش洢�����ķ�ʽ�������εı��������static�ģ������ɱ���������������������֧������������ﵽTLS��Ч��
//ÿ���̶߳����Լ���tlslist
_declspec (thread) static ThreadCache* tlslist = nullptr;