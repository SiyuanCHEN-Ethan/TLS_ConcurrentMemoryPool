#pragma once

#include "common.h"

class ThreadCache
{
private:
	Freelist _freelist[NLISTS];//代表着每个线程具有的自由链表,下面挂载小内存块

public:
	//申请和释放内存
	void* Allocate(size_t size);
	void Dealloctor(void* ptr,size_t size);

	//从central cache获取对象
	void* FetchFromCentralCache(size_t index, size_t size);

	//释放对象时，若链表过长，回收内存回到中心堆  // CSY注：中心堆指CentralCache
	void ListTooLong(Freelist* list, size_t size);


};

//静态的 不是所有可见
//每个线程有个自己的指针，用(_declspec(thread)), 我们在使用时，每次来都是自己的，就不用加锁了 //？
//CSY： _declspec(thread)是隐式声明一个线程本地存储变量的方式，其修饰的变量最好是static的，将会由编译器链接器加载器合作支持这个变量，达到TLS的效果
//每个线程都有自己的tlslist
_declspec (thread) static ThreadCache* tlslist = nullptr;