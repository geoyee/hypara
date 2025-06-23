#include <hypara.hpp>
#include <iostream>

struct A
{
    double f1(int x)
    {
        return std::pow(x, 1);
    }

    static double f2(int x)
    {
        return std::pow(x, 2);
    }
};

int main()
{
    A a;

    // 创建任务组，指定任务签名: double(int)
    hyp::TaskGroup<double, int> tasks;

    // 添加各种类型的任务
    tasks.add_function([](int x) -> double { return std::pow(x, 0); });
    tasks.add_function(&A::f1, &a); // 成员函数
    tasks.add_function(&A::f2);     // 静态成员函数

    // 设置超时时间
    auto timeout = std::chrono::milliseconds(100);

    // 执行不同策略
    auto r1 = tasks.execute_any(5, timeout);
    auto r2 = tasks.execute_any_with([](double res) { return res > 10; }, 5, timeout);
    auto r3 = tasks.execute_all(5, timeout);
    auto r4 = tasks.execute_best([](double a, double b) { return a < b; }, 5, timeout);
    auto r5 = tasks.execute_order_with([](double res) { return res > 10; }, 5, timeout);

    // 输出结果
    std::cout << "Any: ";
    if (r1)
    {
        std::cout << *r1;
    }
    else
    {
        std::cout << "timeout";
    }
    std::cout << std::endl;

    std::cout << "AnyWith: ";
    if (r2)
    {
        std::cout << *r2;
    }
    else
    {
        std::cout << "not found";
    }
    std::cout << std::endl;

    std::cout << "All: ";
    for (auto val : r3)
    {
        std::cout << val << " ";
    }
    std::cout << std::endl;

    std::cout << "Best: ";
    if (r4)
    {
        std::cout << *r4;
    }
    else
    {
        std::cout << "no results";
    }
    std::cout << std::endl;

    std::cout << "OrderWith: ";
    if (r5)
    {
        std::cout << *r5;
    }
    else
    {
        std::cout << "not found";
    }
    std::cout << std::endl;

    return 0;
}