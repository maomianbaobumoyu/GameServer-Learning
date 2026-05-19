#pragma once
#include<mutex>

//多线程-线程安全问题
//移植SGI STL二级空间配置器内存池源码 模版实现
//空间配置器->容器使用的->容器产生的对象很有可能在多个线程中去操作的

template<typename T>
class myallcoator
{
public:
	//开辟内存
	T* allcoate(size_t __n)
	{
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

		return __ret;
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
		if (__old_sz > (size_t)__MAX_BYTES && __new_sz > (size_t)_MAX_BYTES) {
			return(realloc(__p, __new_sz));
		}
		if (__old_sz > (size_t)_MAX_BYTES && __new_sz > (size_t)_MAX_BYTES) return(__p);
		__result = allcoate(__new_sz);
		__copy_sz = __new_sz > __old_sz ? __old_sz : __new_sz;
		memcpy(__result, __p, __copy_sz);
		deallocate(__p, __old_sz);
		return(result);
	}

	//对象构造
	void construct(T* __p, const T& val)
	{
		new (__p) T(val);//定位new来实现
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
		_Obj* volatile __my_free_list;
		_Obj* __result;
		_Obj* __current_obj;
		_Obj* __next_obj;
		int __i;

		if (1 == __nobjs) return (__chunk);//只获取到了一个，直接返回
		__my_free_list = _S_free_list + _S_freelist_index(__n);

		__result = (_Obj*)__chunk;
		*__my_free_list = __next_obj = (_Obj*)(chunk + __n);
		
		for (__i = 1;; __i++)
		{
			__current_obj = __next_obj;
			__next_obj = (_obj*)((char*)__next_obj + __n);
			
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
			return(__result);
		}
		else {
			size_t __bytes_to_get = 2 * __total_bytes + _S_round_up(_S_heap_size >> 4);

			if (__bytes_left > 0) {
				_Obj* volatile __my_free_list = _S_free_list + _S_freelist_index(__bytes_left);
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
_Obj* volatile myallcoator<T>::_S_free_list[_NFREELISTS] = 
{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
