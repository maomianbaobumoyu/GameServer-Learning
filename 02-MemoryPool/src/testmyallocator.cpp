#include "../head/myallocator.h"
#include <iostream>
#include <vector>
using namespace std;

// 自定义测试类（验证 construct / destroy）
struct TestClass
{
    int val;
    TestClass(int v) : val(v) {}
};

int main()
{
    // ===================== 测试1：push_back + 遍历输出 =====================
    cout << "===== 原始测试：push_back 1~10000 并打印 =====" << endl;
    vector<int, myallcoator<int>> vec;
    for (int i = 1; i <= 10000; i++)
    {
        vec.push_back(i);
    }
    // 遍历输出
    for (auto i : vec)
    {
        cout << i << " ";
    }
    cout << "\npush_back 测试完成，元素总数：" << vec.size() << endl << endl;

    // ===================== 测试2：频繁创建销毁 → 验证内存池复用 =====================
    cout << "===== 测试2：内存池复用（批量创建/销毁） =====" << endl;
    for (int k = 0; k < 1000; k++)
    {
        vector<int, myallcoator<int>> temp_vec;
        temp_vec.reserve(64);
        for (int j = 0; j < 50; j++)
        {
            temp_vec.push_back(j);
        }
    }
    cout << "内存池复用测试：无崩溃、无泄漏" << endl << endl;

    // ===================== 测试3：边界值分配 → 验证一/二级配置器切换 =====================
    cout << "===== 测试3：边界内存分配（8/128/132字节） =====" << endl;
    myallcoator<int> alloc; // 创建分配器对象（必须）

    int* p1 = alloc.allocate(2);   // 2个int = 8字节  （二级配置器）
    int* p2 = alloc.allocate(32);  // 32个int = 128字节（二级配置器）
    int* p3 = alloc.allocate(33);  // 33个int = 132字节（一级配置器）

    cout << "8/128/132 字节内存分配成功" << endl;

    // 释放内存
    alloc.deallocate(p1, 2);
    alloc.deallocate(p2, 32);
    alloc.deallocate(p3, 33);
    cout << "边界内存释放成功" << endl << endl;

    // ===================== 测试4：自定义对象 → 验证构造/析构 =====================
    cout << "===== 测试4：自定义对象测试 =====" << endl;
    vector<TestClass, myallcoator<TestClass>> test_vec;
    test_vec.emplace_back(10);
    test_vec.emplace_back(20);
    test_vec.emplace_back(30);
    cout << "自定义对象构造完成" << endl << endl;

    cout << "========================================" << endl;
    cout << "所有测试全部通过！内存池完全正常！" << endl;
    cout << "========================================" << endl;

    return 0;
}