#include "hypara.hpp"

#include <iostream>
#include <cmath>
#include <thread>
#include <numeric>
#include <random>

int main()
{
    // Init
    std::random_device rnd;
    std::mt19937 rGen(rnd());
    std::cout.precision(8);

    // Task base
    {
        HypTask<double(double)> task([](double x) -> double { return std::sqrt(x); });
        double res = task.get(8.0);
        std::cout << "sqrt(8.0) = " << res << std::endl;
    }
    // All, Any and Only
    {
        std::vector<HypTask<double(double)>> tasks{
            HypTask<double(double)>(
                [&rGen](double x) -> double
                {
                    auto start = std::chrono::high_resolution_clock::now();
                    std::uniform_real_distribution<> tDis(0.1, 1);
                    std::uniform_real_distribution<> eDis(-0.08, 0.08);
                    double t = tDis(rGen);
                    double e = eDis(rGen);
                    std::this_thread::sleep_for(std::chrono::milliseconds((int)(t * 100)));
                    auto end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::milli> elapsed = end - start;
                    std::cout << "[Index 1]: use " << elapsed.count() << "ms" << std::endl;
                    return x > 10 ? (std::sqrt(x) + e) : -1;
                }),
            HypTask<double(double)>(
                [&rGen](double x) -> double
                {
                    auto start = std::chrono::high_resolution_clock::now();
                    std::uniform_real_distribution<> tDis(1, 9.9);
                    std::uniform_real_distribution<> eDis(-0.0009, 0.0009);
                    double t = tDis(rGen);
                    double e = eDis(rGen);
                    std::this_thread::sleep_for(std::chrono::milliseconds((int)(t * 100)));
                    auto end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::milli> elapsed = end - start;
                    std::cout << "[Index 2]: use " << elapsed.count() << "ms" << std::endl;
                    return x > 50 ? (std::sqrt(x) + e) : -1;
                }),
            HypTask<double(double)>(
                [&rGen](double x) -> double
                {
                    auto start = std::chrono::high_resolution_clock::now();
                    std::uniform_real_distribution<> tDis(10, 19.9);
                    std::uniform_real_distribution<> eDis(-0.00002, 0.00002);
                    double t = tDis(rGen);
                    double e = eDis(rGen);
                    std::this_thread::sleep_for(std::chrono::milliseconds((int)(t * 100)));
                    auto end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::milli> elapsed = end - start;
                    std::cout << "[Index 3]: use " << elapsed.count() << "ms" << std::endl;
                    return x > 100 ? (std::sqrt(x) + e) : -1;
                }),
            HypTask<double(double)>(
                [&rGen](double x) -> double
                {
                    auto start = std::chrono::high_resolution_clock::now();
                    std::uniform_real_distribution<> tDis(20, 50);
                    std::uniform_real_distribution<> eDis(-0.00000000000001, 0.00000000000001);
                    double t = tDis(rGen);
                    double e = eDis(rGen);
                    std::this_thread::sleep_for(std::chrono::milliseconds((int)(t * 100)));
                    auto end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::milli> elapsed = end - start;
                    std::cout << "[Index 4]: use " << elapsed.count() << "ms" << std::endl;
                    return std::sqrt(x) + e;
                })};

        auto start = std::chrono::high_resolution_clock::now();
        double ave = HypAll(tasks, 8.0)
                         .then(
                             [](std::vector<double>& res)
                             {
                                 std::cout << "every result is: ";
                                 for (const auto& r : res)
                                 {
                                     std::cout << r << ", ";
                                 }
                                 double ave = (double)(std::accumulate(res.begin(), res.end(), 0.0) / res.size());
                                 return ave;
                             })
                         .get();
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        std::cout << "sqrt(8.0) = " << ave << " --> [" << elapsed.count() << "ms]" << std::endl;

        start = std::chrono::high_resolution_clock::now();
        double fast = HypAny(tasks, 8.0)
                          .then(
                              [](std::pair<size_t, double>& res)
                              {
                                  std::cout << "fast task index = " << (int)res.first << " and ";
                                  return res.second;
                              })
                          .get();
        end = std::chrono::high_resolution_clock::now();
        elapsed = end - start;
        std::cout << "sqrt(8.0) = " << fast << " --> [" << elapsed.count() << "ms]" << std::endl;

        constexpr double rel = 8.9442719099991587856366946749251;
        auto check = [&rel](double x) -> bool { return std::abs(x - rel) < 1e-5; };
        start = std::chrono::high_resolution_clock::now();
        double only = HypOnly(check, tasks, 80.0)
                          .then(
                              [](std::pair<size_t, double>& res)
                              {
                                  std::cout << "only task index = " << (int)res.first << " and ";
                                  return res.second;
                              })
                          .get();
        end = std::chrono::high_resolution_clock::now();
        elapsed = end - start;
        std::cout << "sqrt(80.0) = " << only << " --> [" << elapsed.count() << "ms]" << std::endl;
    }

    return 0;
}