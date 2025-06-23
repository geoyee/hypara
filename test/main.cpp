#include <hypara.hpp>
#include <catch2/catch_all.hpp>
#include <cmath>
#include <iostream>
#include <thread>

// 测试函数
double fast_task(int x)
{
    return std::pow(x, 2);
}

double slow_task(int x)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return std::pow(x, 3);
}

double conditional_task(int x)
{
    if (x % 2 == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
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
        group.add_function(fast_task);
        group.add_function(&TestClass::member_task, &obj);
        group.add_function(&TestClass::static_task);
        group.add_function([](int x) { return x * 1.0; });

        REQUIRE(group.execute_all(4).size() == 4);
    }

    SECTION("Execute Any strategy")
    {
        group.add_function(fast_task);
        group.add_function(slow_task);

        auto start = std::chrono::steady_clock::now();
        auto result = group.execute_any(3);
        auto duration = std::chrono::steady_clock::now() - start;

        REQUIRE(result.has_value());
        REQUIRE(result.value() == Catch::Approx(9.0)); // fast_task(3) = 9
        REQUIRE(duration < std::chrono::milliseconds(40));
    }

    SECTION("Execute AnyWith strategy")
    {
        group.add_function([](int x) { return x * 10; });
        group.add_function([](int x) { return x * 20; });
        group.add_function([](int x) { return x * 30; });

        auto result = group.execute_any_with([](double val) { return val > 250; }, 5);

        REQUIRE(result.has_value());
        REQUIRE(result.value() == Catch::Approx(300.0)); // 10*30=300
    }

    SECTION("Execute All strategy")
    {
        group.add_function(fast_task);
        group.add_function(slow_task);
        group.add_function(conditional_task);

        auto results = group.execute_all(3, std::chrono::milliseconds(30));

        REQUIRE(results.size() == 2); // Only fast_task and conditional_task should complete
        REQUIRE((results[0] == Catch::Approx(9.0) || results[0] == Catch::Approx(3.0)));
        REQUIRE((results[1] == Catch::Approx(9.0) || results[1] == Catch::Approx(3.0)));
    }

    SECTION("Execute Best strategy")
    {
        group.add_function([](int x) { return x * 1.0; });
        group.add_function([](int x) { return x * 2.0; });
        group.add_function([](int x) { return x * 3.0; });

        auto result = group.execute_best([](double a, double b) { return a < b; }, // min value
                                         5);

        REQUIRE(result.has_value());
        REQUIRE(result.value() == Catch::Approx(5.0)); // 5*1=5
    }

    SECTION("Execute OrderWith strategy")
    {
        group.add_function([](int x) { return x * 1.0; });
        group.add_function([](int x) { return x * 3.0; });
        group.add_function([](int x) { return x * 2.0; });

        auto result = group.execute_order_with([](double val) { return val > 12; }, 5);

        REQUIRE(result.has_value());
        REQUIRE(result.value() == Catch::Approx(15.0)); // Third task: 5*3=15
    }

    SECTION("Execute OrderWith with timeout")
    {
        group.add_function(
            [](int x)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                return x * 1.0;
            });
        group.add_function(
            [](int x)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                return x * 2.0;
            });

        auto result = group.execute_order_with([](double val) { return val > 5; }, 3, std::chrono::milliseconds(20));

        REQUIRE_FALSE(result.has_value());
    }

    SECTION("AnyWith with no match")
    {
        group.add_function([](int x) { return x * 1.0; });
        group.add_function([](int x) { return x * 2.0; });

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
        tasks.emplace_back(slow_task);
        tasks.emplace_back(fast_task);

        auto composite = hyp::Any(tasks, 4);
        auto result = composite.get();

        REQUIRE(result.first == 1);                    // fast_task should be first
        REQUIRE(result.second == Catch::Approx(16.0)); // 4^2=16
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
        group.add_function(slow_task);
        group.add_function(fast_task);

        auto result = group.execute_any(3, std::chrono::milliseconds(10));

        REQUIRE(result.has_value());
        REQUIRE(result.value() == Catch::Approx(9.0)); // fast_task should complete
    }

    SECTION("AnyWith with timeout")
    {
        group.add_function(
            [](int x)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                return x * 10.0;
            });
        group.add_function(
            [](int x)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
                return x * 20.0;
            });

        auto result = group.execute_any_with([](double val) { return val > 50; }, 3, std::chrono::milliseconds(35));

        REQUIRE_FALSE(result.has_value());
    }

    SECTION("All with partial results")
    {
        group.add_function(
            [](int x)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                return x * 1.0;
            });
        group.add_function(
            [](int x)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                return x * 2.0;
            });
        group.add_function(
            [](int x)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                return x * 3.0;
            });

        auto results = group.execute_all(5, std::chrono::milliseconds(40));

        REQUIRE(results.size() == 2);
        REQUIRE(results[0] == Catch::Approx(5.0));
        REQUIRE(results[1] == Catch::Approx(10.0));
    }

    SECTION("Best with partial results")
    {
        group.add_function(
            [](int x)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                return x * 3.0;
            });
        group.add_function(
            [](int x)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                return x * 1.0;
            });
        group.add_function(
            [](int x)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                return x * 2.0;
            });

        auto result = group.execute_best([](double a, double b) { return a < b; }, // min value
                                         5,
                                         std::chrono::milliseconds(40));

        REQUIRE(result.has_value());
        REQUIRE(result.value() == Catch::Approx(15.0)); // Only first task completed (5*3=15)
    }
}
