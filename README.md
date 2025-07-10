# hypara

[![CMake on multiple platforms](https://github.com/geoyee/hypara/actions/workflows/cmake-multi-platform.yml/badge.svg?branch=main)](https://github.com/geoyee/hypara/actions/workflows/cmake-multi-platform.yml)
![Static Badge](https://img.shields.io/badge/C++-17-red) ![Static Badge](https://img.shields.io/badge/License-Apache2.0-blue)

基于《深入应用 C++11: 代码优化与工程级应用》中 `TaskCpp`的 `Task`和 `When_All_Any`进行修改，以满足某些需求。

## 使用

仅头文件，将[hypara.hpp](./hypara.hpp)拷贝到目标项目，或添加为子模块，链接 `hypara`即可。运行示例[main.cpp](./sample/main.cpp)可以使用 CMake 直接构建此项目。

## 示例

```c++
#include <hypara.hpp>
#include <cmath>
#include <iostream>

struct Calculator
{
    double square(int x)
    {
        return std::pow(x, 2);
    }

    static double cube(int x)
    {
        return std::pow(x, 3);
    }
};

int main()
{
    Calculator calc;
    hyp::Worker<double, int> worker;

    // 添加任务并命名
    worker.add_function("power_zero", [](int x) { return std::pow(x, 0); });
    worker.add_function("square", &Calculator::square, &calc);
    worker.add_function("cube", &Calculator::cube);

    // Any 策略 - 获取第一个完成的任务结果
    if (auto result = worker.execute_any(5))
    {
        auto [name, value] = *result;
        std::cout << "Any: " << name << " returned " << value << std::endl;
    }

    // AnyWith 策略 - 获取第一个满足条件的结果
    if (auto result = worker.execute_any_with([](double v) { return v > 100; }, 5))
    {
        auto [name, value] = *result;
        std::cout << "AnyWith: " << name << " returned " << value << std::endl;
    }

    // All 策略 - 获取所有任务结果
    auto all_results = worker.execute_all(5);
    std::cout << "All results:\n";
    for (auto& [name, value] : all_results)
    {
        std::cout << "  " << name << ": " << value << std::endl;
    }

    // Best 策略 - 获取最佳结果
    if (auto result = worker.execute_best([](double a, double b) { return a < b; }, 5))
    {
        auto [name, value] = *result;
        std::cout << "Best: " << name << " returned " << value << std::endl;
    }

    // OrderWith 策略 - 按顺序获取满足条件的结果
    if (auto result = worker.execute_order_with([](double v) { return v > 10; }, 5))
    {
        auto [name, value] = *result;
        std::cout << "OrderWith: " << name << " returned " << value << std::endl;
    }

    return 0;
}

```
