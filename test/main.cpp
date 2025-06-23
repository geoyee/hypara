#include <hypara.hpp>
#include <catch2/catch_all.hpp>
#include <cmath>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

// 测试函数
double fast_task(int x)
{
    return std::pow(x, 2);
}

double slow_task(int x)
{
    std::this_thread::sleep_for(200ms);
    return std::pow(x, 3);
}

double conditional_task(int x)
{
    if (x % 2 == 0)
    {
        std::this_thread::sleep_for(20ms);
    }
    return x;
}

struct TestClass
{
    double member_task(int x)
    {
        return std::pow(x, 1.5);
    }

    static double static_task(int x)
    {
        return std::pow(x, 0.5);
    }
};

// 测试用例
TEST_CASE("Task basic functionality", "[task]")
{
    SECTION("Run task with lambda")
    {
        hyp::Task<double(int)> task([](int x) { return x * 2.0; });
        auto fut = task.run(5);
        REQUIRE(fut.get() == Catch::Approx(10.0));
    }

    SECTION("Task then chain")
    {
        hyp::Task<double(int)> task1([](int x) { return x * 2.0; });
        auto task2 = task1.then([](double x) { return x + 3.0; });
        REQUIRE(task2.get(5) == Catch::Approx(13.0));
    }

    SECTION("Task with member function")
    {
        TestClass obj;
        hyp::Task<double(int)> task([&obj](int x) { return obj.member_task(x); });
        REQUIRE(task.get(4) == Catch::Approx(8.0)); // 4^1.5 = 8
    }
}

TEST_CASE("TaskGroup functionality", "[taskgroup]")
{
    TestClass obj;
    hyp::TaskGroup<double, int> group;

    SECTION("Add different task types")
    {
        group.add_function("func1", fast_task);
        group.add_function("func2", &TestClass::member_task, &obj);
        group.add_function("func3", &TestClass::static_task);
        group.add_function("func4", [](int x) { return x * 1.0; });

        REQUIRE(group.execute_all(4).size() == 4);
    }

    SECTION("Execute Any strategy")
    {
        group.add_function("fast", [](int) { return 9.0; }); // 直接返回结果，避免延迟
        group.add_function("slow", slow_task);

        auto start = std::chrono::steady_clock::now();
        auto result = group.execute_any(3);
        auto duration = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

        REQUIRE(result.has_value());
        REQUIRE(result.value().second == Catch::Approx(9.0));
        REQUIRE(ms < 20); // 应该非常快速
    }

    SECTION("Execute AnyWith strategy")
    {
        group.add_function("func7", [](int x) { return x * 10; });
        group.add_function("func8", [](int x) { return x * 20; });
        group.add_function("func9", [](int x) { return x * 30; });

        auto result = group.execute_any_with([](double val) { return val > 250; }, 10);

        REQUIRE(result.has_value());
        REQUIRE(result.value().second == Catch::Approx(300.0)); // 10*30=300
    }

    SECTION("Execute All strategy")
    {
        group.add_function("fast", [](int x) { return x; });
        group.add_function("conditional", [](int x) { return x; });

        auto results = group.execute_all(3, 100ms);

        REQUIRE(results.size() == 2); // 两个任务都应该完成
        bool fast_found = false, conditional_found = false;
        for (auto& [name, value] : results)
        {
            if (name == "fast")
            {
                REQUIRE(value == Catch::Approx(3.0));
                fast_found = true;
            }
            else if (name == "conditional")
            {
                REQUIRE(value == Catch::Approx(3.0));
                conditional_found = true;
            }
        }
        REQUIRE(fast_found);
        REQUIRE(conditional_found);
    }

    SECTION("Execute Best strategy")
    {
        group.add_function("func13", [](int x) { return x * 1.0; });
        group.add_function("func14", [](int x) { return x * 2.0; });
        group.add_function("func15", [](int x) { return x * 3.0; });

        auto result = group.execute_best([](double a, double b) { return a < b; }, // min value
                                         5);

        REQUIRE(result.has_value());
        REQUIRE(result.value().second == Catch::Approx(5.0)); // 5*1=5
    }

    SECTION("Execute OrderWith strategy")
    {
        group.add_function("func16", [](int x) { return x * 1.0; });
        group.add_function("func17", [](int x) { return x * 3.0; });
        group.add_function("func18", [](int x) { return x * 2.0; });

        auto result = group.execute_order_with([](double val) { return val > 12; }, 5);

        REQUIRE(result.has_value());
        REQUIRE(result.value().second == Catch::Approx(15.0)); // Third task: 5*3=15
    }

    SECTION("Execute OrderWith with timeout")
    {
        group.add_function("func19",
                           [](int)
                           {
                               std::this_thread::sleep_for(30ms);
                               return 1.0;
                           });
        group.add_function("func20",
                           [](int)
                           {
                               std::this_thread::sleep_for(10ms);
                               return 2.0;
                           });

        auto result = group.execute_order_with([](double val) { return val > 0; }, 3, 20ms);

        REQUIRE_FALSE(result.has_value());
    }

    SECTION("AnyWith with no match")
    {
        group.add_function("func21", [](int x) { return x * 1.0; });
        group.add_function("func22", [](int x) { return x * 2.0; });

        auto result = group.execute_any_with([](double val) { return val > 100; }, 10);

        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Empty group handling")
    {
        REQUIRE_FALSE(group.execute_any(5).has_value());
        REQUIRE_FALSE(group.execute_any_with([](double) { return true; }, 5).has_value());
        REQUIRE(group.execute_all(5).empty());
        REQUIRE_FALSE(group.execute_best([](double, double) { return true; }, 5).has_value());
        REQUIRE_FALSE(group.execute_order_with([](double) { return true; }, 5).has_value());
    }
}

TEST_CASE("Composite tasks", "[composite]")
{
    SECTION("All composite task")
    {
        std::vector<hyp::Task<double(int)>> tasks;
        tasks.emplace_back([](int x) { return x * 1.0; });
        tasks.emplace_back([](int x) { return x * 2.0; });
        tasks.emplace_back([](int x) { return x * 3.0; });

        auto composite = hyp::All(tasks, 5);
        auto results = composite.get();

        REQUIRE(results.size() == 3);
        REQUIRE(results[0] == Catch::Approx(5.0));
        REQUIRE(results[1] == Catch::Approx(10.0));
        REQUIRE(results[2] == Catch::Approx(15.0));
    }

    SECTION("Any composite task")
    {
        std::vector<hyp::Task<double(int)>> tasks;
        tasks.emplace_back(
            [](int)
            {
                std::this_thread::sleep_for(50ms);
                return 1.0;
            });
        tasks.emplace_back([](int) { return 2.0; }); // 立即返回

        auto composite = hyp::Any(tasks, 0);
        auto result = composite.get();

        REQUIRE(result.first == 1); // 索引为1的任务应该先完成
        REQUIRE(result.second == Catch::Approx(2.0));
    }

    SECTION("Best composite task")
    {
        std::vector<hyp::Task<double(int)>> tasks;
        tasks.emplace_back([](int x) { return x * 3.0; });
        tasks.emplace_back([](int x) { return x * 1.0; });
        tasks.emplace_back([](int x) { return x * 2.0; });

        auto composite = hyp::Best([](double a, double b) { return a < b; }, // min value
                                   tasks,
                                   5);

        REQUIRE(composite.get() == Catch::Approx(5.0)); // 5*1=5
    }
}

TEST_CASE("Timeout handling", "[timeout]")
{
    hyp::TaskGroup<double, int> group;

    SECTION("Any with timeout")
    {
        group.add_function("func1",
                           [](int)
                           {
                               std::this_thread::sleep_for(100ms);
                               return 1.0;
                           });
        group.add_function("func2", [](int) { return 2.0; });

        auto result = group.execute_any(0, 10ms);

        REQUIRE(result.has_value());
        REQUIRE(result.value().second == Catch::Approx(2.0)); // 快速任务应该完成
    }

    SECTION("AnyWith with timeout")
    {
        group.add_function("func3",
                           [](int)
                           {
                               std::this_thread::sleep_for(30ms);
                               return 10.0;
                           });
        group.add_function("func4",
                           [](int)
                           {
                               std::this_thread::sleep_for(40ms);
                               return 20.0;
                           });

        auto result = group.execute_any_with([](double val) { return val > 15; }, 3, 35ms);

        REQUIRE_FALSE(result.has_value());
    }

    SECTION("All with partial results")
    {
        group.add_function("func5",
                           [](int x)
                           {
                               std::this_thread::sleep_for(10ms);
                               return x * 1.0;
                           });
        group.add_function("func6",
                           [](int x)
                           {
                               std::this_thread::sleep_for(50ms);
                               return x * 2.0;
                           });
        group.add_function("func7",
                           [](int x)
                           {
                               std::this_thread::sleep_for(100ms);
                               return x * 3.0;
                           });

        auto results = group.execute_all(5, 75ms);

        REQUIRE(results.size() == 1); // 只有第一个任务应该完成
        REQUIRE(results[0].second == Catch::Approx(5.0));
    }

    SECTION("Best with partial results")
    {
        group.add_function("func8",
                           [](int x)
                           {
                               std::this_thread::sleep_for(10ms);
                               return x * 3.0;
                           });
        group.add_function("func9",
                           [](int x)
                           {
                               std::this_thread::sleep_for(50ms);
                               return x * 1.0;
                           });
        group.add_function("func10",
                           [](int x)
                           {
                               std::this_thread::sleep_for(100ms);
                               return x * 2.0;
                           });

        auto result = group.execute_best([](double a, double b) { return a < b; }, // min value
                                         5,
                                         75ms);

        REQUIRE(result.has_value());
        REQUIRE(result.value().second == Catch::Approx(15.0)); // 只有第一个任务完成 (5*3=15)
    }
}

TEST_CASE("Performance testing", "[performance]")
{
    constexpr int TASK_COUNT = 100;
    hyp::TaskGroup<double, int> group;

    for (int i = 0; i < TASK_COUNT; i++)
    {
        group.add_function("task_" + std::to_string(i), [i](int x) { return x * i; });
    }

    SECTION("Any strategy performance")
    {
        auto start = std::chrono::high_resolution_clock::now();
        auto result = group.execute_any(5);
        auto duration = std::chrono::high_resolution_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

        REQUIRE(result.has_value());
        std::cout << "Any strategy with " << TASK_COUNT << " tasks took " << ms << " ms\n";
        REQUIRE(ms < 10); // 应该非常快速
    }

    SECTION("All strategy performance")
    {
        auto start = std::chrono::high_resolution_clock::now();
        auto results = group.execute_all(5);
        auto duration = std::chrono::high_resolution_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

        REQUIRE(results.size() == TASK_COUNT);
        std::cout << "All strategy with " << TASK_COUNT << " tasks took " << ms << " ms\n";
    }
}

TEST_CASE("Boundary testing", "[boundary]")
{
    hyp::TaskGroup<double, int> group;

    SECTION("Large number of tasks")
    {
        constexpr int TASK_COUNT = 1000;
        for (int i = 0; i < TASK_COUNT; i++)
        {
            group.add_function("task_" + std::to_string(i), [i](int x) { return x * i; });
        }

        auto results = group.execute_all(5);
        REQUIRE(results.size() == TASK_COUNT);
    }

    SECTION("Long running tasks")
    {
        group.add_function("long_task1",
                           [](int)
                           {
                               std::this_thread::sleep_for(2s);
                               return 1.0;
                           });
        group.add_function("long_task2",
                           [](int)
                           {
                               std::this_thread::sleep_for(2s);
                               return 2.0;
                           });

        auto start = std::chrono::steady_clock::now();
        auto result = group.execute_any(0, 100ms);
        auto duration = std::chrono::steady_clock::now() - start;

        REQUIRE_FALSE(result.has_value());
        REQUIRE(duration < 150ms);
    }
}
