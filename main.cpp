#include "hypara.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <thread>

class Timer final
{
    using time_point_type = std::decay_t<decltype(std::chrono::high_resolution_clock::now())>;

public:
    Timer(const Timer&) = delete;
    Timer(Timer&&) = delete;

    Timer()
    {
        m_start = std::chrono::high_resolution_clock::now();
    }

    ~Timer()
    {
        time_point_type end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - m_start);
        std::cout << "This work use " << static_cast<double>(duration.count()) << " ms\n" << std::endl;
    }

private:
    time_point_type m_start;
};

constexpr double DBL_NaN = std::numeric_limits<double>::quiet_NaN();

int main()
{
    std::random_device rnd;
    std::mt19937 rGen(rnd());
    std::cout.precision(8);

    std::vector<HypTask<double(double)>> tasks{
        HypTask<double(double)>(
            [&rGen](double x) -> double
            {
                std::uniform_real_distribution<> tDis(0.1, 1);
                std::uniform_real_distribution<> eDis(-0.08, 0.08);
                double t = tDis(rGen);
                double e = eDis(rGen);
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(t * 100)));
                return /*x < 10 ?*/ (std::sqrt(x) + e) /*: -1*/;
            }),
        HypTask<double(double)>(
            [&rGen](double x) -> double
            {
                std::uniform_real_distribution<> tDis(1, 9.9);
                std::uniform_real_distribution<> eDis(-0.0009, 0.0009);
                double t = tDis(rGen);
                double e = eDis(rGen);
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(t * 100)));
                return /*x < 50 ?*/ (std::sqrt(x) + e) /*: -1*/;
            }),
        HypTask<double(double)>(
            [&rGen](double x) -> double
            {
                std::uniform_real_distribution<> tDis(10, 19.9);
                std::uniform_real_distribution<> eDis(-0.00002, 0.00002);
                double t = tDis(rGen);
                double e = eDis(rGen);
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(t * 100)));
                return /*x < 100 ?*/ (std::sqrt(x) + e) /*: -1*/;
            }),
        HypTask<double(double)>(
            [&rGen](double x) -> double
            {
                std::uniform_real_distribution<> tDis(20, 50);
                std::uniform_real_distribution<> eDis(-0.00000000000001, 0.00000000000001);
                double t = tDis(rGen);
                double e = eDis(rGen);
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(t * 100)));
                return std::sqrt(x) + e;
            })};
    auto check = [](double x) -> bool { return std::abs(x - 2.8284271247461900976033774484194) < 1e-12; };

    {
        auto task = tasks[0];
        Timer t;
        auto res = task.get(8.0);
        std::cout << "Task sqrt(8.0) = " << res << std::endl;
    }
    {
        auto task = HypAll(tasks, 8.0);
        Timer t;
        auto res = task.get();
        double ave =
            static_cast<double>(std::accumulate(res.begin(), res.end(), 0.0) / static_cast<double>(res.size()));
        std::cout << "HypAll sqrt(8.0) = " << ave << std::endl;
    }
    {
        auto task = HypAny(tasks, 8.0);
        Timer t;
        auto res = task.get();
        std::cout << "HypAny sqrt(8.0) = " << res.second << " and index = " << res.first << std::endl;
    }
    {
        auto task = HypOnly(check, DBL_NaN, tasks, 8.0);
        Timer t;
        auto res = task.get();
        std::cout << "HypOnly sqrt(8.0) = " << res.second << " and index = " << res.first << std::endl;
    }

    return 0;
}