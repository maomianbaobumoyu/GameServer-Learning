#pragma once
#include<mutex>       // 互斥锁，实现多线程安全
#include<iostream>    // 标准输入输出
#include<cstdlib>     // malloc/free/realloc 系统内存分配函数
#include<cstring>     // memcpy 内存拷贝
#include<new>         // 定位new、std::bad_alloc 异常
#include<stdexcept>

// ==================== 一级空间配置器 ====================
// 封装 malloc/free/realloc，处理**大块内存**分配
// 核心功能：处理内存分配失败（OOM），支持设置回调函数
template <int __inst>
class __malloc_alloc_template {

private:
	// 内存分配失败处理函数（OOM：Out Of Memory）
	static void* _S_oom_malloc(size_t);
	static void* _S_oom_realloc(void*, size_t);
	// 内存分配失败的回调函数指针
	static void (*__malloc_alloc_oom_handler)();
public:

	// 分配内存：封装 malloc
	static void* allocate(size_t __n)
	{
		void* __result = malloc(__n);
		// 分配失败，调用OOM处理函数
		if (0 == __result) __result = _S_oom_malloc(__n);
		return __result;
	}

	// 释放内存：封装 free
	static void deallocate(void* __p, size_t /* __n */)
	{
		free(__p);
	}

	// 重分配内存：封装 realloc
	static void* reallocate(void* __p, size_t /* old_sz */, size_t __new_sz)
	{
		void* __result = realloc(__p, __new_sz);
		// 分配失败，调用OOM处理函数
		if (0 == __result) __result = _S_oom_realloc(__p, __new_sz);
		return __result;
	}

	// 设置OOM回调函数（用户自定义内存释放逻辑）
	static void (*__set_malloc_handler(void (*__f)()))()
	{
		void (*__old)() = __malloc_alloc_oom_handler;
		__malloc_alloc_oom_handler = __f;
		return(__old);
	}

};

// 静态成员初始化：OOM回调函数默认为空
template<int __inst>
void (*__malloc_alloc_template<__inst>::__malloc_alloc_oom_handler)() = nullptr;

// malloc分配内存失败（OOM）处理逻辑
template <int __inst>
void*
__malloc_alloc_template<__inst>::_S_oom_malloc(size_t __n)
{
	void (*__my_malloc_handler)();
	void* __result;

	// 死循环尝试分配内存
	for (;;) {
		__my_malloc_handler = __malloc_alloc_oom_handler;
		// 无自定义回调，直接抛出异常
		if (0 == __my_malloc_handler) { throw std::bad_alloc(); }
		// 执行用户自定义的内存释放回调
		(*__my_malloc_handler)();
		// 再次尝试分配内存
		__result = malloc(__n);
		if (__result) return(__result);
	}
}

// realloc分配内存失败（OOM）处理逻辑
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

// 定义别名，简化使用
typedef __malloc_alloc_template<0> malloc_alloc;

// ==================== 二级空间配置器（核心） ====================
// 处理**小块内存**分配，采用 自由链表+内存池 设计
// 多线程安全，专为STL容器优化，减少内存碎片、提升分配效率
template<typename T>
class myallcoator
{
public:
	// STL分配器必须的类型定义
	using value_type = T;

	// 构造函数
	constexpr myallcoator() noexcept
	{
	}

	// 拷贝构造/泛型拷贝构造（STL容器要求）
	constexpr myallcoator(const myallcoator&) noexcept = default;
	template<class _Other>
	constexpr myallcoator(const myallcoator<_Other>&) noexcept
	{
	}

	// ---------------- 核心内存分配函数 ----------------
	// __n：STL容器传入的【元素个数】
	T* allocate(size_t __n)
	{
		// 转换为【实际需要的字节数】（元素个数 * 单个元素大小）
		__n = __n * sizeof(T);

		void* __ret = 0;
		// 大于128字节：大块内存，交给一级配置器处理
		if (__n > (size_t)_MAX_BYTES) {
			__ret = malloc_alloc::allocate(__n);
		}
		// 小于等于128字节：小块内存，使用自由链表分配
		else {
			// 获取对应大小的自由链表头指针（二级指针：用于修改链表头）
			_Obj* volatile* __my_free_list = _S_free_list + _S_freelist_index(__n);

			// 加锁：多线程安全，防止并发修改自由链表
			std::lock_guard<std::mutex> guard(mtx);

			// 取出链表头节点
			_Obj* __result = *__my_free_list;
			// 链表为空：需要重新填充内存
			if (__result == 0) __ret = _S_refill(_S_round_up(__n));
			// 链表有可用内存：摘除头节点，返回给用户
			else {
				*__my_free_list = __result->_M_free_list_link;
				__ret = __result;
			}
		}
		return (T*)__ret;
	}

	// ---------------- 核心内存释放函数 ----------------
	void deallocate(void* __p, size_t __n)
	{
		// 大块内存：直接调用free
		if (__n > (size_t)_MAX_BYTES) malloc_alloc::deallocate(__p, __n);
		// 小块内存：回收至自由链表
		else {
			// 找到对应大小的自由链表
			_Obj* volatile* __my_free_list = _S_free_list + _S_freelist_index(__n);
			_Obj* __q = (_Obj*)__p;

			// 加锁：多线程安全
			std::lock_guard<std::mutex> guard(mtx);
			// 将内存块插回自由链表头部
			__q->_M_free_list_link = *__my_free_list;
			*__my_free_list = __q;
		}
	}

	// ---------------- 内存重分配（扩容/缩容） ----------------
	static void* reallocate(void* __p, size_t __old_sz, size_t __new_sz)
	{
		void* __result;
		size_t __copy_sz;
		//如果新值旧值都大于128，说明不是从内存池中分配的，使用realloc调整大小
		if (__old_sz > (size_t)_MAX_BYTES && __new_sz > (size_t)_MAX_BYTES) {
			return(realloc(__p, __new_sz));
		}
		//如果是小块内存，如果新值旧值向上取值结果一样，没有必要调整，直接返回
		if (_S_round_up(__old_sz) == _S_round_up(__new_sz)) return(__p);
		//如果新值旧值不同，通过allocate分配对应new值的大小
		__result = allocate(__new_sz);
		//如果new>old，就拷贝old大小的数据，否则拷贝new大小的数据
		__copy_sz = __new_sz > __old_sz ? __old_sz : __new_sz;
		memcpy(__result, __p, __copy_sz);//使用memcpy把数据从旧的chunk块copy到新的chunk块
		deallocate(__p, __old_sz);
		//释放掉旧的chunk块，返回新chunk块的地址
		return(__result);
	}

	// ---------------- 对象构造：定位new ----------------
	void construct(T* __p, const T& val)
	{
		new (__p) T(val);// 在指定内存地址构造对象，不分配新内存
	}

	// ---------------- 对象析构 ----------------
	void destroy(T* __p)
	{
		__p->~T();// 调用析构函数，不释放内存
	}

private:
	// 配置常量定义
	enum { _ALIGN = 8 };// 内存对齐基数：按8字节对齐
	enum { _MAX_BYTES = 128 };// 小块内存最大阈值：128字节
	enum { _NFREELISTS = 16 };// 自由链表个数：128/8=16个（8/16/.../128字节）

	// 自由链表节点结构体（共用体：节省内存）
	union _Obj {
		union _Obj* _M_free_list_link; // 指向下一个空闲内存块
		char _M_client_data[1];        // 占位符，用户实际使用的内存
	};

	// 内存池全局变量
	static char* _S_start_free;    // 内存池起始地址
	static char* _S_end_free;      // 内存池结束地址
	static size_t _S_heap_size;    // 内存池总分配大小

	// 自由链表数组：16个链表，对应不同大小的内存块
	// volatile：禁止编译器优化，保证多线程可见性
	static _Obj* volatile _S_free_list[_NFREELISTS];

	// 互斥锁：保证多线程下自由链表/内存池操作安全
	static std::mutex mtx;

	// ---------------- 内存对齐函数 ----------------
	// 将字节数上调到最接近的8的倍数
	static size_t _S_round_up(size_t __bytes)
	{
		return (((__bytes)+(size_t)_ALIGN - 1) & ~((size_t)_ALIGN - 1));
	}

	// ---------------- 计算自由链表下标 ----------------
	// 根据内存大小，找到对应的自由链表索引
	static size_t _S_freelist_index(size_t __bytes) {
		return (((__bytes)+(size_t)_ALIGN - 1) / (size_t)_ALIGN - 1);
	}

	// ---------------- 填充自由链表 ----------------
	// 内存池分配一块内存，切割成多个块，填充到自由链表
	static void* _S_refill(size_t __n)
	{
		int __nobjs = 20; // 默认分配20个内存块
		// 从内存池申请内存
		char* __chunk = _S_chunk_alloc(__n, __nobjs);
		_Obj* volatile* __my_free_list;
		_Obj* __result;
		_Obj* __current_obj;
		_Obj* __next_obj;
		int __i;

		// 只分配到1块：直接返回，不填充链表
		if (1 == __nobjs) return (__chunk);
		// 找到对应自由链表
		__my_free_list = _S_free_list + _S_freelist_index(__n);

		// 第一块返回给用户
		__result = (_Obj*)__chunk;
		// 剩余块串联成自由链表
		*__my_free_list = __next_obj = (_Obj*)(__chunk + __n);

		// 遍历串联所有内存块
		for (__i = 1;; __i++)
		{
			__current_obj = __next_obj;
			__next_obj = (_Obj*)((char*)__next_obj + __n);

			// 最后一块next置空
			if (__nobjs - 1 == __i)
			{
				__current_obj->_M_free_list_link = nullptr;
				break;
			}
			// 串联节点
			else {
				__current_obj->_M_free_list_link = __next_obj;
			}
		}

		return(__result);
	}

	// ---------------- 从内存池分配内存 ----------------
	// 核心：向系统申请内存，补充内存池
	static char* _S_chunk_alloc(size_t __size, int& __nobjs)
	{
		char* __result;
		size_t __total_bytes = __size * __nobjs; // 总需要字节数
		size_t __bytes_left = _S_end_free - _S_start_free; // 内存池剩余内存

		// 内存池剩余内存足够：直接分配
		if (__bytes_left >= __total_bytes) {
			__result = _S_start_free;
			_S_start_free += __total_bytes;
			return(__result);
		}
		// 内存池剩余内存至少分配1块：分配剩余所有
		else if (__bytes_left >= __size) {
			__nobjs = (int)(__bytes_left / __size);
			__total_bytes = __size * __nobjs;
			__result = _S_start_free;
			_S_start_free += __total_bytes;
			return(__result);
		}
		// 内存池内存不足：向系统malloc申请新内存
		else {
			size_t __bytes_to_get = 2 * __total_bytes + _S_round_up(_S_heap_size >> 4);

			// 回收内存池剩余的零碎内存，插入对应自由链表
			if (__bytes_left > 0) {
				_Obj* volatile* __my_free_list = _S_free_list + _S_freelist_index(__bytes_left);
				((_Obj*)_S_start_free)->_M_free_list_link = *__my_free_list;
				*__my_free_list = (_Obj*)_S_start_free;
			}
			// 系统调用malloc申请内存
			_S_start_free = (char*)malloc(__bytes_to_get);
			// 系统malloc失败：尝试从更大的自由链表中挪用内存
			if (nullptr == _S_start_free) {
				size_t __i;
				_Obj* volatile* __my_free_list;
				_Obj* __p;

				// 遍历更大的自由链表，寻找可用内存
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
				// 无内存可用：交给一级配置器处理OOM
				_S_end_free = 0;
				_S_start_free = (char*)malloc_alloc::allocate(__bytes_to_get);
			}
			// 更新内存池大小
			_S_heap_size += __bytes_to_get;
			_S_end_free = _S_start_free + __bytes_to_get;
			// 递归重新分配
			return(_S_chunk_alloc(__size, __nobjs));
		}
	}

};

// ==================== 静态成员变量初始化 ====================
template<typename T>
char* myallcoator<T>::_S_start_free = nullptr; // 内存池起始地址
template<typename T>
char* myallcoator<T>::_S_end_free = nullptr;   // 内存池结束地址
template<typename T>
size_t myallcoator<T>::_S_heap_size = 0;       // 内存池总大小

template<typename T>
std::mutex myallcoator<T>::mtx; // 互斥锁初始化

// 自由链表数组初始化（全为空）
template<typename T>
typename myallcoator<T>::_Obj* volatile myallcoator<T>::_S_free_list[myallcoator<T>::_NFREELISTS] =
{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };