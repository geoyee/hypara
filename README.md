# hypara

[![CMake on multiple platforms](https://github.com/geoyee/hypara/actions/workflows/cmake-multi-platform.yml/badge.svg?branch=main)](https://github.com/geoyee/hypara/actions/workflows/cmake-multi-platform.yml)
![Static Badge](https://img.shields.io/badge/C++-17-blue)

基于《深入应用C++11：代码优化与工程级应用》中的`TaskCpp`进行修改，主要是增加了`All`和`Any`的参数输入，以及满足一定条件就完成的`Only`，仅头文件。

## 示例

```shell
Task sqrt(8.0) = 2.8263112
This block finished and use 383 ms
HypAll sqrt(8.0) = 2.8290545
This block finished and use 1956 ms
HypAny sqrt(8.0) = 2.8319722 and index = 0
This block finished and use 373 ms
HypOnly sqrt(16.0) = 4 and index = 2
This block finished and use 1501 ms
```

