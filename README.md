# hypara

[![CMake on multiple platforms](https://github.com/geoyee/hypara/actions/workflows/cmake-multi-platform.yml/badge.svg?branch=main)](https://github.com/geoyee/hypara/actions/workflows/cmake-multi-platform.yml)
![Static Badge](https://img.shields.io/badge/C++-17-red) ![Static Badge](https://img.shields.io/badge/License-Apache2.0-blue)

基于《深入应用 C++11: 代码优化与工程级应用》中`TaskCpp`的`Task`和`When_All_Any`进行修改，以满足某些需求。

## 使用

仅头文件，将[hypara.hpp](./hypara.hpp)拷贝到目标项目，或添加为子模块，链接`hypara`即可。运行示例[main.cpp](./sample/main.cpp)可以使用 CMake 直接构建此项目。

## 示例

1. `HypAll`: 同时开始多个任务，完成后统一返回结果

   ```cpp
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
   ```

   控制台打印如下，耗时主要来自于`std::accumulate`

   ```text
   5^0 + 5^1 + 5^2 + 5^3 = 156
   This block finished and use 2 ms
   ```

2. `HypBest`: 同时开始多个任务，完成返回按需求排序最符合的结果

   ```cpp
   using TaskType = HypTask<double(int)>;
   std::vector<TaskType> tasks{TaskType([](int x) -> double { return std::pow(x, 2) + 0.1; }),
                               TaskType([](int x) -> double { return std::pow(x, 2) + 0.01; }),
                               TaskType([](int x) -> double { return std::pow(x, 2) + 0.001; }),
                               TaskType([](int x) -> double { return std::pow(x, 2) + 0.0001; })};
   auto comparison = [](const double& a, const double& b) { return a < b; };
   double res = HypBest(comparison, tasks, 5).get();
   std::cout << "5^2 = " << res << std::endl;
   ```

   控制台打印如下，按照比较规则，返回了误差最小的结果

   ```text
   5^2 = 25.0001
   This block finished and use 0 ms
   ```

3. `HypAny`: 同时开始多个任务，返回第一个完成任务的结果

   ```cpp
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
   ```

   控制台打印如下，第 2 个任务最先完成就返回了结果，不等待其他任务

   ```text
   Index = 2, and res = 25
   This block finished and use 137 ms
   ```

4. `HypAnyWith`: 同时开始多个任务，返回第一个完成任务且结果符合要求的结果，若所有结果都不满足，则返回第二个参数为默认值

   ```cpp
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
   ```

   控制台打印如下，虽然第 2 个任务最先完成，但由于需要的是正数结果，所以就等待下一个任务，也就是第 0 个任务完成，结果通过检查，返回

   ```text
   Index = 0, and res = 25
   This block finished and use 215 ms
   ```

5. `HypOrderWith`: 同时开始多个任务，按顺序返回第一个满足要求的结果，若都不满足，返回最后一个任务的结果

   ```cpp
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
   ```

   控制台打印如下，按顺序第 0 个任务结果优先返回，但由于不满足要求，则返回第 1 个任务的结果

   ```text
   Index = 1, and res = 25
   This block finished and use 308 ms
   ```
