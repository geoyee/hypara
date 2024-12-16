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
                std::uniform_real_distribution<> eDis(-0.01, 0.01);
                double t = tDis(rGen);
                double e = eDis(rGen);
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(t * 500)));
                return x < 10 ? (std::sqrt(x) + e) : DBL_NaN;
            }),
        HypTask<double(double)>(
            [&rGen](double x) -> double
            {
                std::uniform_real_distribution<> tDis(1.1, 2);
                std::uniform_real_distribution<> eDis(-0.00001, 0.00001);
                double t = tDis(rGen);
                double e = eDis(rGen);
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(t * 500)));
                return x < 100 ? (std::sqrt(x) + e) : DBL_NaN;
            }),
        HypTask<double(double)>(
            [&rGen](double x) -> double
            {
                std::uniform_real_distribution<> tDis(2.1, 3);
                std::uniform_real_distribution<> eDis(-0.000000001, 0.000000001);
                double t = tDis(rGen);
                double e = eDis(rGen);
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(t * 500)));
                return x < 1000 ? (std::sqrt(x) + e) : DBL_NaN;
            }),
        HypTask<double(double)>(
            [&rGen](double x) -> double
            {
                std::uniform_real_distribution<> tDis(3.1, 4);
                std::uniform_real_distribution<> eDis(-0.00000000000000001, 0.00000000000000001);
                double t = tDis(rGen);
                double e = eDis(rGen);
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(t * 500)));
                return std::sqrt(x) + e;
            })};
    auto check = [](double x) -> bool { return (x != DBL_NaN) && (std::abs(x - 4.0) < 1e-8); };

    {
        Timer _;
        auto res = tasks[0].get(8.0);
        std::cout << "Task sqrt(8.0) = " << res << std::endl;
    }
    {
        Timer _;
        auto res = HypAll(tasks, 8.0).get();
        double ave =
            static_cast<double>(std::accumulate(res.begin(), res.end(), 0.0) / static_cast<double>(res.size()));
        std::cout << "HypAll sqrt(8.0) = " << ave << std::endl;
    }
    {
        Timer _;
        auto res = HypAny(tasks, 8.0).get();
        std::cout << "HypAny sqrt(8.0) = " << res.second << " and index = " << res.first << std::endl;
    }
    {
        Timer _;
        auto res = HypOnly(check, DBL_NaN, tasks, 16.0).get();
        std::cout << "HypOnly sqrt(16.0) = " << res.second << " and index = " << res.first << std::endl;
    }

    return 0;
}