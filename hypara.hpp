#pragma once

#ifndef _HYPARA_HPP_
#define _HYPARA_HPP_

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace hyp
{
template<typename T>
class Task;

/**
 * \brief A class representing a task that can be executed with specified arguments.
 *
 * \tparam Ret The return type of the task.
 * \tparam Args The argument types that the task accepts.
 */
template<typename Ret, typename... Args>
class Task<Ret(Args...)>
{
public:
    using return_type = Ret;
    using function_type = std::function<Ret(Args...)>;

    template<typename Fn, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Fn>, Task>>>
    explicit Task(Fn&& fn) : m_fn(std::forward<Fn>(fn))
    {
    }

    std::shared_future<Ret> run(Args... args) const
    {
        return std::async(std::launch::async, m_fn, std::forward<Args>(args)...).share();
    }

    void wait(Args... args) const
    {
        run(std::forward<Args>(args)...).wait();
    }

    Ret get(Args... args) const
    {
        return run(std::forward<Args>(args)...).get();
    }

    template<typename Func>
    auto then(Func&& fn) const -> Task<typename std::invoke_result_t<Func, Ret>(Args...)>
    {
        using result_type = typename std::invoke_result_t<Func, Ret>;
        return Task<result_type(Args...)>(
            [func = m_fn, fn = std::forward<Func>(fn)](Args... args) mutable
            {
                auto fut = std::async(std::launch::async, func, std::forward<Args>(args)...);
                return fn(fut.get());
            });
    }

private:
    function_type m_fn;
};

namespace aux
{
template<typename T>
struct range_trait
{
    using type = T;
};

template<typename Ret>
struct range_trait<std::shared_future<Ret>>
{
    using type = Ret;
};

template<typename T>
using range_trait_t = typename range_trait<T>::type;

template<typename Range, typename... Args>
auto transform(const Range& range, const std::tuple<Args...>& tArgs)
    -> std::vector<std::shared_future<typename Range::value_type::return_type>>
{
    using result_type = typename Range::value_type::return_type;
    std::vector<std::shared_future<result_type>> funcs;
    funcs.reserve(range.size());

    for (const auto& task : range)
    {
        funcs.emplace_back(std::apply([&task](const auto&...args) { return task.run(args...); }, tArgs));
    }
    return funcs;
}

template<typename Range>
auto getAnyResultPair(Range&& funcs, std::chrono::milliseconds timeout)
    -> std::pair<size_t, range_trait_t<typename Range::value_type>>
{
    using result_type = range_trait_t<typename Range::value_type>;
    using result_pair = std::pair<size_t, result_type>;

    std::promise<result_pair> resPro;
    auto resfut = resPro.get_future();
    auto sharedData = std::make_shared<std::atomic<bool>>(false);
    const size_t count = funcs.size();
    auto isFinished = std::make_shared<std::vector<std::atomic<bool>>>(count);
    for (size_t i = 0; i < count; ++i)
    {
        (*isFinished)[i] = false;
    }

    auto start_time = std::chrono::steady_clock::now();

    std::thread monitor(
        [funcs = std::forward<Range>(funcs),
         resPro = std::move(resPro),
         sharedData,
         isFinished,
         count,
         timeout,
         start_time]() mutable
        {
            size_t completed = 0;
            while (completed < count && !sharedData->load())
            {
                // Check timeout
                if (timeout.count() > 0)
                {
                    auto current_time = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
                    if (elapsed >= timeout)
                    {
                        break;
                    }
                }

                for (size_t i = 0; i < count; ++i)
                {
                    if ((*isFinished)[i])
                    {
                        continue;
                    }

                    if (funcs[i].wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
                    {
                        try
                        {
                            auto res = funcs[i].get();
                            (*isFinished)[i] = true;
                            ++completed;
                            if (!sharedData->exchange(true))
                            {
                                resPro.set_value(std::make_pair(i, std::move(res)));
                                return;
                            }
                        }
                        catch (...)
                        {
                            (*isFinished)[i] = true;
                            ++completed;
                        }
                    }
                }
            }

            if (!sharedData->exchange(true))
            {
                resPro.set_exception(
                    std::make_exception_ptr(std::runtime_error("All tasks failed or no task returned a valid result")));
            }
        });

    monitor.detach();
    return resfut.get();
}

template<typename Func,
         typename Range,
         std::enable_if_t<std::is_invocable_r_v<bool, Func, range_trait_t<typename Range::value_type>>, bool> = true>
auto getAnyWithResultPair(Func checkFun, Range&& funcs, std::chrono::milliseconds timeout)
    -> std::pair<int, std::optional<range_trait_t<typename Range::value_type>>>
{
    using result_type = range_trait_t<typename Range::value_type>;
    using result_pair = std::pair<int, std::optional<result_type>>;

    std::promise<result_pair> resPro;
    auto resfut = resPro.get_future();
    auto sharedData = std::make_shared<std::atomic<bool>>(false);
    const size_t count = funcs.size();
    auto isFinished = std::make_shared<std::vector<std::atomic<bool>>>(count);
    for (size_t i = 0; i < count; ++i)
    {
        (*isFinished)[i] = false;
    }

    auto start_time = std::chrono::steady_clock::now();

    std::thread monitor(
        [funcs = std::forward<Range>(funcs),
         checkFun = std::move(checkFun),
         resPro = std::move(resPro),
         sharedData,
         isFinished,
         count,
         timeout,
         start_time]() mutable
        {
            size_t completed = 0;
            while (completed < count && !sharedData->load())
            {
                // Check timeout
                if (timeout.count() > 0)
                {
                    auto current_time = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
                    if (elapsed >= timeout)
                    {
                        break;
                    }
                }

                for (size_t i = 0; i < count; ++i)
                {
                    if ((*isFinished)[i])
                    {
                        continue;
                    }

                    if (funcs[i].wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
                    {
                        try
                        {
                            auto res = funcs[i].get();
                            (*isFinished)[i] = true;
                            ++completed;
                            if (checkFun(res))
                            {
                                if (!sharedData->exchange(true))
                                {
                                    resPro.set_value(std::make_pair(static_cast<int>(i), std::move(res)));
                                    return;
                                }
                            }
                        }
                        catch (...)
                        {
                            (*isFinished)[i] = true;
                            ++completed;
                        }
                    }
                }
            }

            if (!sharedData->exchange(true))
            {
                resPro.set_value(std::make_pair(-1, std::nullopt));
            }
        });

    monitor.detach();
    return resfut.get();
}

template<typename Func,
         typename Range,
         std::enable_if_t<std::is_invocable_r_v<bool, Func, range_trait_t<typename Range::value_type>>, bool> = true>
auto getOrderWithResultPair(Func&& checkFun, Range&& funcs, std::chrono::milliseconds timeout)
    -> std::pair<size_t, std::optional<range_trait_t<typename Range::value_type>>>
{
    using result_type = range_trait_t<typename Range::value_type>;
    using result_pair = std::pair<size_t, std::optional<result_type>>;

    std::promise<result_pair> resPro;
    auto resfut = resPro.get_future();
    auto sharedData = std::make_shared<std::atomic<bool>>(false);

    auto start_time = std::chrono::steady_clock::now();

    std::thread monitor(
        [funcs = std::forward<Range>(funcs),
         checkFun = std::forward<Func>(checkFun),
         resPro = std::move(resPro),
         sharedData,
         timeout,
         start_time]() mutable
        {
            const size_t count = funcs.size();
            for (size_t i = 0; i < count; ++i)
            {
                if (sharedData->load())
                {
                    break;
                }

                // Check timeout
                if (timeout.count() > 0)
                {
                    auto current_time = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
                    if (elapsed >= timeout)
                    {
                        break;
                    }
                }

                try
                {
                    if (funcs[i].wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
                    {
                        auto res = funcs[i].get();
                        if (checkFun(res))
                        {
                            if (!sharedData->exchange(true))
                            {
                                resPro.set_value(std::make_pair(i, std::move(res)));
                                return;
                            }
                        }
                    }
                }
                catch (...)
                {
                    // 忽略失败的任务
                }
            }

            if (!sharedData->exchange(true))
            {
                resPro.set_value(std::make_pair(count, std::nullopt));
            }
        });

    monitor.detach();
    return resfut.get();
}
} // namespace aux

template<typename Range, typename... Args>
inline auto All(const Range& range, Args&&...args) -> Task<std::vector<typename Range::value_type::return_type>()>
{
    using result_type = typename Range::value_type::return_type;
    using vector_type = std::vector<result_type>;

    auto tArgs = std::make_tuple(std::forward<Args>(args)...);
    return Task<vector_type()>(
        [range, tArgs = std::move(tArgs)]() mutable
        {
            auto funcs = aux::transform(range, tArgs);
            vector_type res;
            res.reserve(funcs.size());
            for (auto& fut : funcs)
            {
                res.emplace_back(fut.get());
            }
            return res;
        });
}

template<typename Func,
         typename Range,
         typename... Args,
         std::enable_if_t<std::is_invocable_r_v<bool,
                                                Func,
                                                typename Range::value_type::return_type,
                                                typename Range::value_type::return_type>,
                          bool> = true>
inline auto Best(Func fn, const Range& range, Args&&...args) -> Task<typename Range::value_type::return_type()>
{
    using result_type = typename Range::value_type::return_type;
    using vector_type = std::vector<result_type>;

    return All(range, std::forward<Args>(args)...)
        .then(
            [fn = std::move(fn)](vector_type tmpRes)
            {
                if (tmpRes.empty())
                {
                    throw std::runtime_error("No results to compare");
                }
                return *std::min_element(tmpRes.begin(),
                                         tmpRes.end(),
                                         [&fn](const result_type& a, const result_type& b) { return fn(a, b); });
            });
}

template<typename Range, typename... Args>
inline auto Any(const Range& range, Args&&...args) -> Task<std::pair<size_t, typename Range::value_type::return_type>()>
{
    using result_type = typename Range::value_type::return_type;
    using pair_type = std::pair<size_t, result_type>;

    return Task<pair_type()>(
        [range, tArgs = std::make_tuple(std::forward<Args>(args)...)]() mutable
        {
            auto funcs = aux::transform(range, tArgs);
            return aux::getAnyResultPair(std::move(funcs), std::chrono::milliseconds(0));
        });
}

template<typename Range, typename... Args>
inline auto Any(const Range& range, std::chrono::milliseconds timeout, Args&&...args)
    -> Task<std::pair<size_t, typename Range::value_type::return_type>()>
{
    using result_type = typename Range::value_type::return_type;
    using pair_type = std::pair<size_t, result_type>;

    return Task<pair_type()>(
        [range, timeout, tArgs = std::make_tuple(std::forward<Args>(args)...)]() mutable
        {
            auto funcs = aux::transform(range, tArgs);
            return aux::getAnyResultPair(std::move(funcs), timeout);
        });
}

template<typename Func, typename Range, typename... Args>
inline auto AnyWith(Func fn, const Range& range, Args&&...args)
    -> Task<std::pair<int, std::optional<typename Range::value_type::return_type>>()>
{
    using result_type = typename Range::value_type::return_type;
    using pair_type = std::pair<int, std::optional<result_type>>;

    return Task<pair_type()>(
        [range, fn = std::move(fn), tArgs = std::make_tuple(std::forward<Args>(args)...)]() mutable
        {
            auto funcs = aux::transform(range, tArgs);
            return aux::getAnyWithResultPair(std::move(fn), std::move(funcs), std::chrono::milliseconds(0));
        });
}

template<typename Func, typename Range, typename... Args>
inline auto AnyWith(Func fn, const Range& range, std::chrono::milliseconds timeout, Args&&...args)
    -> Task<std::pair<int, std::optional<typename Range::value_type::return_type>>()>
{
    using result_type = typename Range::value_type::return_type;
    using pair_type = std::pair<int, std::optional<result_type>>;

    return Task<pair_type()>(
        [range, fn = std::move(fn), timeout, tArgs = std::make_tuple(std::forward<Args>(args)...)]() mutable
        {
            auto funcs = aux::transform(range, tArgs);
            return aux::getAnyWithResultPair(std::move(fn), std::move(funcs), timeout);
        });
}

template<typename Func, typename Range, typename... Args>
inline auto OrderWith(Func fn, const Range& range, Args&&...args)
    -> Task<std::pair<size_t, std::optional<typename Range::value_type::return_type>>()>
{
    using result_type = typename Range::value_type::return_type;
    using pair_type = std::pair<size_t, std::optional<result_type>>;

    return Task<pair_type()>(
        [range, fn = std::move(fn), tArgs = std::make_tuple(std::forward<Args>(args)...)]() mutable
        {
            auto funcs = aux::transform(range, tArgs);
            return aux::getOrderWithResultPair(std::move(fn), std::move(funcs), std::chrono::milliseconds(0));
        });
}

template<typename Func, typename Range, typename... Args>
inline auto OrderWith(Func fn, const Range& range, std::chrono::milliseconds timeout, Args&&...args)
    -> Task<std::pair<size_t, std::optional<typename Range::value_type::return_type>>()>
{
    using result_type = typename Range::value_type::return_type;
    using pair_type = std::pair<size_t, std::optional<result_type>>;

    return Task<pair_type()>(
        [range, fn = std::move(fn), timeout, tArgs = std::make_tuple(std::forward<Args>(args)...)]() mutable
        {
            auto funcs = aux::transform(range, tArgs);
            return aux::getOrderWithResultPair(std::move(fn), std::move(funcs), timeout);
        });
}

/**
 * \brief A class for managing and executing groups of tasks.
 */
template<typename Ret, typename... Args>
class Worker
{
public:
    using TaskType = Task<Ret(Args...)>;
    using FutureType = std::shared_future<Ret>;
    using ConditionType = std::function<bool(Ret)>;
    using ComparatorType = std::function<bool(Ret, Ret)>;

    template<typename Fn>
    void add_function(const std::string& name, Fn&& fn)
    {
        tasks_.emplace_back(name, TaskType(std::forward<Fn>(fn)));
    }

    template<typename MemFn, typename Obj>
    void add_function(const std::string& name, MemFn mem_fn, Obj&& obj)
    {
        tasks_.emplace_back(name,
                            TaskType([mem_fn, obj = std::forward<Obj>(obj)](Args... args) -> Ret
                                     { return (obj->*mem_fn)(std::forward<Args>(args)...); }));
    }

    std::optional<std::pair<std::string, Ret>> execute_any(
        Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        if (tasks_.empty())
        {
            return std::nullopt;
        }

        // 将超时转换为毫秒
        auto ms_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);

        // 使用带超时的Any组合函数
        auto any_task = Any(tasks_vector(), ms_timeout, std::forward<Args>(args)...);
        auto fut = any_task.run();

        // 等待结果（这里不需要额外等待，因为任务内部已经处理超时）
        try
        {
            auto [index, result] = fut.get();
            return std::make_pair(tasks_[index].first, result);
        }
        catch (const std::runtime_error&)
        {
            return std::nullopt;
        }
    }

    std::optional<std::pair<std::string, Ret>> execute_any_with(
        ConditionType condition, Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        if (tasks_.empty())
        {
            return std::nullopt;
        }

        // 将超时转换为毫秒
        auto ms_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);

        // 使用带超时的AnyWith组合函数
        auto any_with_task = AnyWith(condition, tasks_vector(), ms_timeout, std::forward<Args>(args)...);
        auto fut = any_with_task.run();

        auto [index, result_opt] = fut.get();
        if (index >= 0 && result_opt)
        {
            return std::make_pair(tasks_[index].first, *result_opt);
        }
        return std::nullopt;
    }

    std::vector<std::pair<std::string, Ret>> execute_all(
        Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        std::vector<std::pair<std::string, Ret>> results;
        if (tasks_.empty())
        {
            return results;
        }

        auto tArgs = std::make_tuple(std::forward<Args>(args)...);
        auto funcs = aux::transform(tasks_vector(), tArgs);

        auto start_time = std::chrono::steady_clock::now();
        for (size_t i = 0; i < funcs.size(); ++i)
        {
            if (timeout.count() > 0)
            {
                auto current_time = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
                auto remaining = timeout - elapsed;

                if (remaining.count() <= 0)
                {
                    // 超时返回部分结果（但根据要求返回空结果）
                    return {};
                }

                if (funcs[i].wait_for(remaining) != std::future_status::ready)
                {
                    // 超时返回空结果
                    return {};
                }
            }
            else
            {
                funcs[i].wait();
            }

            try
            {
                results.emplace_back(tasks_[i].first, funcs[i].get());
            }
            catch (...)
            {
                // 处理异常，可以记录日志或跳过
            }
        }

        return results;
    }

    std::optional<std::pair<std::string, Ret>> execute_best(
        ComparatorType comparator, Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        if (tasks_.empty())
        {
            return std::nullopt;
        }

        // 获取所有结果（带超时）
        auto all_results = execute_all(std::forward<Args>(args)..., timeout);

        if (all_results.empty())
        {
            return std::nullopt;
        }

        // 找到最佳结果
        auto best_it =
            std::min_element(all_results.begin(),
                             all_results.end(),
                             [&comparator](const auto& a, const auto& b) { return comparator(a.second, b.second); });

        return *best_it;
    }

    std::optional<std::pair<std::string, Ret>> execute_order_with(
        ConditionType condition, Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        if (tasks_.empty())
        {
            return std::nullopt;
        }

        // 将超时转换为毫秒
        auto ms_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);

        // 使用带超时的OrderWith组合函数
        auto order_with_task = OrderWith(condition, tasks_vector(), ms_timeout, std::forward<Args>(args)...);
        auto fut = order_with_task.run();

        auto [index, result_opt] = fut.get();
        if (result_opt)
        {
            return std::make_pair(tasks_[index].first, *result_opt);
        }
        return std::nullopt;
    }

private:
    std::vector<TaskType> tasks_vector() const
    {
        std::vector<TaskType> tasks;
        for (const auto& [_, task] : tasks_)
        {
            tasks.push_back(task);
        }
        return tasks;
    }

    std::vector<std::pair<std::string, TaskType>> tasks_;
};
} // namespace hyp

#endif // !_HYPARA_HPP_