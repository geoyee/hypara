#include "hypara.hpp"

#include <cmath>
#include <iostream>
#include <numeric>
#include <thread>

using namespace std::chrono_literals;

class Timer final
{
    using time_point_type = std::decay_t<decltype(std::chrono::high_resolution_clock::now())>;

public:
    Timer(const Timer&) = delete;
    Timer(Timer&&) = delete;

    Timer() : m_start(std::chrono::high_resolution_clock::now()) { }

    ~Timer()
    {
        time_point_type end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - m_start);
        std::cout << "This block finished and use " << static_cast<int>(duration.count()) << " ms" << std::endl;
    }

private:
    time_point_type m_start;
};

#define TEST_BLOCK_START                                                                                               \
    {                                                                                                                  \
        Timer _;

#define TEST_BLOCK_END                                                                                                 \
    }                                                                                                                  \
    std::cout << std::endl;

int main()
{
    TEST_BLOCK_START
    {
        using TaskType = HypTask<double(int)>;
        std::vector<TaskType> tasks{TaskType([](int x) -> double { return std::pow(x, 0); }),
                                    TaskType([](int x) -> double { return std::pow(x, 1); }),
                                    TaskType([](int x) -> double { return std::pow(x, 2); }),
                                    TaskType([](int x) -> double { return std::pow(x, 3); })};
        double res =
            HypAll(tasks, 5)
                .then([](const std::vector<double>& res) { return std::accumulate(res.begin(), res.end(), 0.0); })
                .get();
        std::cout << "5^0 + 5^1 + 5^2 + 5^3 = " << res << std::endl;
    }
    TEST_BLOCK_END

    TEST_BLOCK_START
    {
        using TaskType = HypTask<double(int)>;
        std::vector<TaskType> tasks{TaskType([](int x) -> double { return std::pow(x, 2) + 0.1; }),
                                    TaskType([](int x) -> double { return std::pow(x, 2) + 0.01; }),
                                    TaskType([](int x) -> double { return std::pow(x, 2) + 0.001; }),
                                    TaskType([](int x) -> double { return std::pow(x, 2) + 0.0001; })};
        auto comparison = [](const double& a, const double& b) { return a < b; };
        double res = HypBest(comparison, tasks, 5).get();
        std::cout << "5^2 = " << res << std::endl;
    }
    TEST_BLOCK_END

    TEST_BLOCK_START
    {
        using TaskType = HypTask<double(int)>;
        std::vector<TaskType> tasks{TaskType(
                                        [](int x) -> double
                                        {
                                            std::this_thread::sleep_for(200ms);
                                            return std::pow(x, 2);
                                        }),
                                    TaskType(
                                        [](int x) -> double
                                        {
                                            std::this_thread::sleep_for(300ms);
                                            return std::pow(x, 2);
                                        }),
                                    TaskType(
                                        [](int x) -> double
                                        {
                                            std::this_thread::sleep_for(100ms);
                                            return std::pow(x, 2);
                                        }),
                                    TaskType(
                                        [](int x) -> double
                                        {
                                            std::this_thread::sleep_for(400ms);
                                            return std::pow(x, 2);
                                        })};
        std::pair<size_t, double> res = HypAny(tasks, 5).get();
        std::cout << "Index = " << res.first << ", and res = " << res.second << std::endl;
    }
    TEST_BLOCK_END

    TEST_BLOCK_START
    {
        using TaskType = HypTask<double(int)>;
        std::vector<TaskType> tasks{TaskType(
                                        [](int x) -> double
                                        {
                                            std::this_thread::sleep_for(200ms);
                                            return std::pow(x, 2);
                                        }),
                                    TaskType(
                                        [](int x) -> double
                                        {
                                            std::this_thread::sleep_for(300ms);
                                            return std::pow(x, 2);
                                        }),
                                    TaskType(
                                        [](int x) -> double
                                        {
                                            std::this_thread::sleep_for(100ms);
                                            return -std::pow(x, 2);
                                        }),
                                    TaskType(
                                        [](int x) -> double
                                        {
                                            std::this_thread::sleep_for(400ms);
                                            return std::pow(x, 2);
                                        })};
        auto check = [](double x) { return x > 0; };
        std::pair<int, double> res = HypAnyWith(check, 0, tasks, 5).get();
        std::cout << "Index = " << res.first << ", and res = " << res.second << std::endl;
    }
    TEST_BLOCK_END

    TEST_BLOCK_START
    {
        using TaskType = HypTask<double(int)>;
        std::vector<TaskType> tasks{TaskType(
                                        [](int x) -> double
                                        {
                                            std::this_thread::sleep_for(200ms);
                                            return -std::pow(x, 2);
                                        }),
                                    TaskType(
                                        [](int x) -> double
                                        {
                                            std::this_thread::sleep_for(300ms);
                                            return std::pow(x, 2);
                                        }),
                                    TaskType(
                                        [](int x) -> double
                                        {
                                            std::this_thread::sleep_for(100ms);
                                            return std::pow(x, 2);
                                        }),
                                    TaskType(
                                        [](int x) -> double
                                        {
                                            std::this_thread::sleep_for(400ms);
                                            return std::pow(x, 2);
                                        })};
        auto check = [](const double& x) { return x > 0; };
        std::pair<size_t, double> res = HypOrderWith(check, tasks, 5).get();
        std::cout << "Index = " << res.first << ", and res = " << res.second << std::endl;
    }
    TEST_BLOCK_END

    return 0;
}