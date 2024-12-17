# hypara

[![CMake on multiple platforms](https://github.com/geoyee/hypara/actions/workflows/cmake-multi-platform.yml/badge.svg?branch=main)](https://github.com/geoyee/hypara/actions/workflows/cmake-multi-platform.yml)
![Static Badge](https://img.shields.io/badge/C++-17-blue)

基于《深入应用C++11：代码优化与工程级应用》中的`TaskCpp`进行修改，主要是增加了`All`和`Any`的参数输入，以及新增了一些如`AnyWith`、`Best`以及`OrderWith`的操作。

## 示例

见[main.cpp](./main.cpp)，某出输出如下：

```shell
Task sqrt(8.0) = 2.8226333
This block finished and use 290 ms

HypAll sqrt(8.0) = 2.8306793
This block finished and use 1662 ms

HypBest sqrt(8.0) = 2.8284342
This block finished and use 1940 ms

HypAny sqrt(8.0) = 2.8310756 and index = 0
This block finished and use 247 ms

HypAnyWith sqrt(16.0) = 4 and index = 2
This block finished and use 1476 ms

HypOrderWith sqrt(16.0) = 4 and index = 2
This block finished and use 1101 ms
```
