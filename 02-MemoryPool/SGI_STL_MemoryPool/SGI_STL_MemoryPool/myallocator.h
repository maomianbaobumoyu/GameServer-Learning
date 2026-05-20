#pragma once
#include<mutex>
#include<iostream>
#include<cstdlib>   
#include<cstring>   
#include<new> 


//封装了malloc和Free操作，可以设置OOM释放内存的回调函数
template <int __inst>
class __malloc_alloc_template {

private:

	static void* _S_oom_malloc(size_t);
	static void* _S_oom_realloc(void*, size_t);
	static void (*__malloc_alloc_oom_handler)();
public:

	static void* allocate(size_t __n)
	{
		void* __result = malloc(__n);
		if (0 == __result) __result = _S_oom_malloc(__n);
		return __result;
	}

	static void deallocate(void* __p, size_t /* __n */)
	{
		free(__p);
	}

	static void* reallocate(void* __p, size_t /* old_sz */, size_t __new_sz)
	{
		void* __result = realloc(__p, __new_sz);
		if (0 == __result) __result = _S_oom_realloc(__p, __new_sz);
		return __result;
	}

	static void (*__set_malloc_handler(void (*__f)()))()
	{
		void (*__old)() = __malloc_alloc_oom_handler;
		__malloc_alloc_oom_handler = __f;
		return(__old);
	}

};

template<int __inst>
void (*__malloc_alloc_template<__inst>::__malloc_alloc_oom_handler)() = nullptr;

//11
template <int __inst>
void*
__malloc_alloc_template<__inst>::_S_oom_malloc(size_t __n)
{
	void (*__my_malloc_handler)();
	void* __result;

	for (;;) {
		__my_malloc_handler = __malloc_alloc_oom_handler;
		if (0 == __my_malloc_handler) { throw std::bad_alloc(); }
		(*__my_malloc_handler)();
		__result = malloc(__n);
		if (__result) return(__result);
	}
}

template <int __inst>
void* __malloc_alloc_template<__inst>::_S_oom_realloc(void* __p, size_t __n)
{
	void (*__my_malloc_handler)();
	void* __result;

	for (;;) {
		__my_malloc_handler = __malloc_alloc_oom_handler;
		if (0 == __my_malloc_handler) { throw std::bad_alloc(); }
		(*__my_malloc_handler)();
		__result = realloc(__p, __n);
		if (__result) return(__result);
	}
}

typedef __malloc_alloc_template<0> malloc_alloc;

//多线程-线程安全问题
//移植SGI STL二级空间配置器内存池源码 模版实现
//空间配置器->容器使用的->容器产生的对象很有可能在多个线程中去操作的

template<typename T>
class myallcoator
{
public:
	using value_type = T;

	constexpr myallcoator() noexcept
	{
	}

	constexpr myallcoator(const myallcoator&) noexcept = default;
	template<class _Other>
	constexpr myallcoator(const myallcoator<_Other>&) noexcept
	{
	}
	//开辟内存  
	T* allocate(size_t __n)//C++中的stl容器传的是元素的个数
	{
		__n = __n * sizeof(T);//但我们需要实际元素的大小，所以进行处理

		void* __ret = 0;
		if (__n > (size_t)_MAX_BYTES) {//分配大块内存
			__ret = malloc_alloc::allocate(__n);
		}
		else {//小块内存，先定位自由链表里的位置
			_Obj* volatile* __my_free_list = _S_free_list + _S_freelist_index(__n);

			std::lock_guard<std::mutex> guard(mtx);//上锁

			_Obj* __result = *__my_free_list;
			if (__result == 0) __ret = _S_refill(_S_round_up(__n));
			else {
				*__my_free_list = __result->_M_free_list_link;
				__ret = __result;
			}
		}
		return (T*)__ret;
	}



	//释放内存
	void deallocate(void* __p, size_t __n)
	{
		if (__n > (size_t)_MAX_BYTES) malloc_alloc::deallocate(__p, __n);
		else {
			_Obj* volatile* __my_free_list = _S_free_list + _S_freelist_index(__n);
			_Obj* __q = (_Obj*)__p;

			std::lock_guard<std::mutex> guard(mtx);
			__q->_M_free_list_link = *__my_free_list;
			*__my_free_list = __q;
		}
	}

	//内存扩容&缩容
	static void* reallocate(void* __p, size_t __old_sz, size_t __new_sz)
	{
		void* __result;
		size_t __copy_sz;
		// 【修正】__MAX_BYTES → _MAX_BYTES (笔误多了下划线)
		if (__old_sz > (size_t)_MAX_BYTES && __new_sz > (size_t)_MAX_BYTES) {
			return(realloc(__p, __new_sz));
		}
		if (__old_sz > (size_t)_MAX_BYTES && __new_sz > (size_t)_MAX_BYTES) return(__p);
		// 【修正】allcoate → allocate
		__result = allocate(__new_sz);
		__copy_sz = __new_sz > __old_sz ? __old_sz : __new_sz;
		memcpy(__result, __p, __copy_sz);
		deallocate(__p, __old_sz);
		// 【修正】result → __result (变量名笔误)
		return(__result);
	}

	//对象构造
	void construct(T* __p, const T& val)
	{
		new (__p) T(val);//定位new来实现
	}

	//对象析构  【修正】destory → destroy
	void destroy(T* __p)
	{
		__p->~T();
	}

private:
	enum { _ALIGN = 8 };//自由链表是从8字节开始，以8字节为对其方式，直到_MAX_BYTES
	enum { _MAX_BYTES = 128 };//内存池最大的chunk块
	enum { _NFREELISTS = 16 };//自由链表的个数

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

	static std::mutex mtx;//内存池基于freelist实现，需要考虑线程安全

	//将__bytes上调至最邻近的8的倍数
	static size_t _S_round_up(size_t __bytes)
	{
		return (((__bytes)+(size_t)_ALIGN - 1) & ~((size_t)_ALIGN - 1));
	}

	//返回 __bytes 大小的小额区块位于 free-list中的编号
	static size_t _S_freelist_index(size_t __bytes) {
		return (((__bytes)+(size_t)_ALIGN - 1) / (size_t)_ALIGN - 1);
	}
	//把分配好的chunk块进行链接
	static void* _S_refill(size_t __n)
	{
		int __nobjs = 20;
		char* __chunk = _S_chunk_alloc(__n, __nobjs);
		_Obj* volatile* __my_free_list;
		_Obj* __result;
		_Obj* __current_obj;
		_Obj* __next_obj;
		int __i;

		if (1 == __nobjs) return (__chunk);//只获取到了一个，直接返回
		__my_free_list = _S_free_list + _S_freelist_index(__n);

		__result = (_Obj*)__chunk;
	
		*__my_free_list = __next_obj = (_Obj*)(__chunk + __n);

		for (__i = 1;; __i++)
		{
			__current_obj = __next_obj;
		
			__next_obj = (_Obj*)((char*)__next_obj + __n);

			if (__nobjs - 1 == __i)
			{
				__current_obj->_M_free_list_link = nullptr;
				break;
			}
			else {
				__current_obj->_M_free_list_link = __next_obj;
			}
		}

		return(__result);
	}
	//主要分配自由链表，chunk块
	static char* _S_chunk_alloc(size_t __size, int& __nobjs)
	{
		char* __result;
		size_t __total_bytes = __size * __nobjs;
		size_t __bytes_left = _S_end_free - _S_start_free;

		if (__bytes_left >= __total_bytes) {
			__result = _S_start_free;
			_S_start_free += __total_bytes;
			return(__result);
		}
		else if (__bytes_left >= __size) {
			__nobjs = (int)(__bytes_left / __size);
			__total_bytes = __size * __nobjs;
			__result = _S_start_free;
			_S_start_free += __total_bytes;
			return(__result);
		}
		else {
			size_t __bytes_to_get = 2 * __total_bytes + _S_round_up(_S_heap_size >> 4);

			if (__bytes_left > 0) {
				_Obj* volatile* __my_free_list = _S_free_list + _S_freelist_index(__bytes_left);
				//自由链表本身就是指针，需要用二级指针指向它
				((_Obj*)_S_start_free)->_M_free_list_link = *__my_free_list;
				*__my_free_list = (_Obj*)_S_start_free;
			}
			_S_start_free = (char*)malloc(__bytes_to_get);
			if (nullptr == _S_start_free) {
				size_t __i;
				_Obj* volatile* __my_free_list;
				_Obj* __p;

				for (__i = __size; __i <= (size_t)_MAX_BYTES; __i += (size_t)_ALIGN) {
					__my_free_list = _S_free_list + _S_freelist_index(__i);
					__p = *__my_free_list;
					if (0 != __p) {
						*__my_free_list = __p->_M_free_list_link;
						_S_start_free = (char*)__p;
						_S_end_free = _S_start_free + __i;
						return(_S_chunk_alloc(__size, __nobjs));
					}
				}
				_S_end_free = 0;
				_S_start_free = (char*)malloc_alloc::allocate(__bytes_to_get);
			}
			_S_heap_size += __bytes_to_get;
			_S_end_free = _S_start_free + __bytes_to_get;
			return(_S_chunk_alloc(__size, __nobjs));
		}
	}

};

template<typename T>
char* myallcoator<T>::_S_start_free = nullptr;
template<typename T>
char* myallcoator<T>::_S_end_free = nullptr;
template<typename T>
size_t myallcoator<T>::_S_heap_size = 0;
//类外初始化内存chunk块的使用情况

template<typename T>
std::mutex myallcoator<T>::mtx;

template<typename T>
typename myallcoator<T>::_Obj* volatile myallcoator<T>::_S_free_list[myallcoator<T>::_NFREELISTS] =
{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };