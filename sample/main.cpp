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
    hyp::TaskGroup<double, int> tasks;

    // 添加任务并命名
    tasks.add_function("power_zero", [](int x) { return std::pow(x, 0); });
    tasks.add_function("square", &Calculator::square, &calc);
    tasks.add_function("cube", &Calculator::cube);

    // Any 策略 - 获取第一个完成的任务结果
    if (auto result = tasks.execute_any(5))
    {
        auto [name, value] = *result;
        std::cout << "Any: " << name << " returned " << value << std::endl;
    }

    // AnyWith 策略 - 获取第一个满足条件的结果
    if (auto result = tasks.execute_any_with([](double v) { return v > 100; }, 5))
    {
        auto [name, value] = *result;
        std::cout << "AnyWith: " << name << " returned " << value << std::endl;
    }

    // All 策略 - 获取所有任务结果
    auto all_results = tasks.execute_all(5);
    std::cout << "All results:\n";
    for (auto& [name, value] : all_results)
    {
        std::cout << "  " << name << ": " << value << std::endl;
    }

    // Best 策略 - 获取最佳结果
    if (auto result = tasks.execute_best([](double a, double b) { return a < b; }, 5))
    {
        auto [name, value] = *result;
        std::cout << "Best: " << name << " returned " << value << std::endl;
    }

    // OrderWith 策略 - 按顺序获取满足条件的结果
    if (auto result = tasks.execute_order_with([](double v) { return v > 10; }, 5))
    {
        auto [name, value] = *result;
        std::cout << "OrderWith: " << name << " returned " << value << std::endl;
    }

    return 0;
}
