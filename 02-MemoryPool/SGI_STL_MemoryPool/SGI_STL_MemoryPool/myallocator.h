#pragma once


//多线程-线程安全问题
//移植SGI STL二级空间配置器内存池源码 模版实现
//空间配置器->容器使用的->容器产生的对象很有可能在多个线程中去操作的

template<typename T>
class myallcoator
{
public:
	//开辟内存
	T* allcoate(size_t __n);

	//释放内存
	void deallocate(void* __p, size_t __n);

	//内存扩容&缩容
	static void reallocate(void* __p, size_t __old_sz, size_t __new_sz);

	//对象构造
	void construct(T* __p, const T& val)
	{
		new (_p) T(val);//定位new来实现
	}

	//对象析构
	void destory(T* __p)
	{
		__p->~T();
	}

private:
	enum { _ALIGN = 8 };//自由链表是从8字节开始，以8字节为对其方式，直到_MAX_BYTES
	enum {_MAX_BYTES =128};//内存池最大的chunk块
	enum {_NFREELISTS = 16};//自由链表的个数

	// 每一个chunk块的头信息，_M_free_list_link存储下一个chunk块的地址

	union _Obj {
		union _Obj* _M_free_list_link;
		char _M_client_data[1];
	};

	//已分配的内存chunk块的使用情况
	static char* _S_start_free;
	static char* _S_end_free;
	static size_t _S_heap_size;

	static _Obj* volatile _S_free_list[_NFREELISTS];
	//防止被线程缓存

	//将__bytes上调至最邻近的8的倍数
	static size_t _S_round_up(size_t __bytes)
	{
		return (((__bytes)+(size_t)_ALIGN - 1) & ~((size_t)_ALIGN - 1));
	}

	//返回 __bytes 大小的小额区块位于 free-list中的编号
	static size_t _S_freelist_index(size_t __bytes) {
		return (((__bytes)+(size_t)_ALIGN - 1) / (size_t)_ALIGN - 1);
	}
};

template<typename T>
char* myallcoator<T>::_S_start_free = nullptr;
template<typename T>
char* myallcoator<T>::_S_end_free = nullptr;
template<typename T>
size_t myallcoator<T>::_S_heap_size = nullptr;
//类外初始化内存chunk块的使用情况

template<typename T>
_Obj* volatile myallcoator<T>::_S_free_list[_NFREELISTS] = { nullptr };
