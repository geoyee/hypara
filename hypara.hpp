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
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <condition_variable>

namespace hyp
{
/**
 * \brief A class representing a task that can be executed with specified arguments.
 */
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

    /**
     * \brief Constructs a Task with a callable.
     *
     * \tparam Fn The type of the callable object.
     * \param fn A callable object that takes Args... and returns Ret.
     */
    template<typename Fn, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Fn>, Task>>>
    explicit Task(Fn&& fn) : m_fn(std::forward<Fn>(fn))
    {
    }

    /**
     * \brief Runs the task with the given arguments.
     *
     * \param args The arguments to pass to the task.
     * \return A shared future that resolves to the result of the task.
     */
    std::shared_future<Ret> run(Args... args) const
    {
        return std::async(std::launch::async, m_fn, std::forward<Args>(args)...).share();
    }

    /**
     * \brief Runs the task with the given arguments and waits for it to finish.
     *
     * \param args The arguments to pass to the task.
     */
    void wait(Args... args) const
    {
        run(std::forward<Args>(args)...).wait();
    }

    /**
     * \brief Runs the task with the given arguments and returns the result.
     *
     * \param args The arguments to pass to the task.
     * \return The result of the task.
     */
    Ret get(Args... args) const
    {
        return run(std::forward<Args>(args)...).get();
    }

    /**
     * \brief Creates a new Task that runs the given function after this Task is finished.
     *
     * \tparam Func The type of the continuation function.
     * \param fn A callable object that takes the result of this Task and returns a new result.
     * \return A new Task that runs `fn` after this Task is finished.
     */
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
    function_type m_fn; ///< The function to run.
};

/**
 * \brief A class for managing a group of tasks with the same signature.
 */
template<typename Ret, typename... Args>
class TaskGroup
{
public:
    using TaskType = Task<Ret(Args...)>;
    using FunctionType = std::function<Ret(Args...)>;
    using FutureType = std::shared_future<Ret>;
    using ResultType = Ret;
    using ConditionType = std::function<bool(Ret)>;
    using ComparatorType = std::function<bool(Ret, Ret)>;

    /**
     * \brief Adds a function to the task group.
     *
     * \param name The name of the task.
     * \param fn The callable to add.
     */
    template<typename Fn>
    void add_function(const std::string& name, Fn&& fn)
    {
        tasks_.emplace_back(name, TaskType(std::forward<Fn>(fn)));
    }

    /**
     * \brief Adds a member function to the task group.
     *
     * \param name The name of the task.
     * \param mem_fn The member function.
     * \param obj The object on which to invoke the member function.
     */
    template<typename MemFn, typename Obj>
    void add_function(const std::string& name, MemFn mem_fn, Obj&& obj)
    {
        tasks_.emplace_back(name,
                            TaskType([mem_fn, obj = std::forward<Obj>(obj)](Args... args) -> Ret
                                     { return (obj->*mem_fn)(std::forward<Args>(args)...); }));
    }

    /**
     * \brief Executes the tasks with the Any strategy.
     *
     * \param args The arguments to pass to the tasks.
     * \param timeout Maximum duration to wait for a result.
     * \return A pair containing the task name and result, or std::nullopt if timeout.
     */
    std::optional<std::pair<std::string, Ret>> execute_any(
        Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        if (tasks_.empty())
        {
            return std::nullopt;
        }

        std::vector<FutureType> futures;
        futures.reserve(tasks_.size());

        for (auto& [name, task] : tasks_)
        {
            futures.push_back(task.run(args...));
        }

        // 使用when_any替代轮询
        std::promise<std::optional<size_t>> result_promise;
        auto result_future = result_promise.get_future();

        std::thread(
            [&]
            {
                std::vector<std::shared_future<void>> wait_futures;
                for (auto& fut : futures)
                {
                    wait_futures.push_back(std::async(std::launch::async, [&fut] { fut.wait(); }));
                }

                while (true)
                {
                    for (size_t i = 0; i < futures.size(); i++)
                    {
                        if (futures[i].wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                        {
                            result_promise.set_value(i);
                            return;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            })
            .detach();

        // 等待结果或超时
        if (timeout.count() > 0)
        {
            if (result_future.wait_for(timeout) == std::future_status::timeout)
            {
                return std::nullopt;
            }
        }
        else
        {
            result_future.wait();
        }

        if (result_future.valid() && result_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto index = result_future.get();
            if (index)
            {
                auto& [name, _] = tasks_[*index];
                return std::make_pair(name, futures[*index].get());
            }
        }

        return std::nullopt;
    }

    /**
     * \brief Executes the tasks with the AnyWith strategy.
     *
     * \param condition The condition function.
     * \param args The arguments to pass to the tasks.
     * \param timeout Maximum duration to wait for a result.
     * \return A pair containing the task name and result, or std::nullopt if none match or timeout.
     */
    std::optional<std::pair<std::string, Ret>> execute_any_with(
        ConditionType condition, Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        if (tasks_.empty())
        {
            return std::nullopt;
        }

        std::vector<FutureType> futures;
        futures.reserve(tasks_.size());

        for (auto& [name, task] : tasks_)
        {
            futures.push_back(task.run(args...));
        }

        // 使用条件变量高效等待
        std::mutex mutex;
        std::condition_variable cv;
        std::optional<size_t> result_index;
        bool completed = false;

        // 创建监控线程
        std::thread monitor(
            [&]
            {
                for (size_t i = 0; i < futures.size(); i++)
                {
                    try
                    {
                        auto result = futures[i].get();
                        if (condition(result))
                        {
                            std::lock_guard lock(mutex);
                            result_index = i;
                            completed = true;
                            cv.notify_one();
                            return;
                        }
                    }
                    catch (...)
                    {
                        // 忽略失败的任务
                    }
                }
            });

        // 等待结果或超时
        std::unique_lock lock(mutex);
        if (timeout.count() > 0)
        {
            if (!cv.wait_for(lock, timeout, [&] { return completed; }))
            {
                completed = true; // 通知监控线程退出
                monitor.detach();
                return std::nullopt;
            }
        }
        else
        {
            cv.wait(lock, [&] { return completed; });
        }

        if (monitor.joinable())
        {
            monitor.join();
        }

        if (result_index)
        {
            auto& [name, _] = tasks_[*result_index];
            return std::make_pair(name, futures[*result_index].get());
        }

        return std::nullopt;
    }

    /**
     * \brief Executes the tasks with the All strategy.
     *
     * \param args The arguments to pass to the tasks.
     * \param timeout Maximum duration to wait for all results.
     * \return A vector of all completed results with task names (may be partial if timeout).
     */
    std::vector<std::pair<std::string, Ret>> execute_all(
        Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        std::vector<std::pair<std::string, Ret>> results;
        if (tasks_.empty())
        {
            return results;
        }

        std::vector<FutureType> futures;
        futures.reserve(tasks_.size());

        // 启动所有任务
        for (auto& [name, task] : tasks_)
        {
            futures.push_back(task.run(args...));
        }

        // 计算截止时间
        auto deadline = std::chrono::steady_clock::now() + timeout;

        for (size_t i = 0; i < tasks_.size(); i++)
        {
            auto& [name, _] = tasks_[i];

            // 检查超时
            if (timeout.count() > 0 && std::chrono::steady_clock::now() >= deadline)
            {
                break;
            }

            try
            {
                // 使用非阻塞检查
                auto status = futures[i].wait_for(std::chrono::seconds(0));
                if (status == std::future_status::ready)
                {
                    results.emplace_back(name, futures[i].get());
                }
            }
            catch (...)
            {
                // 忽略失败的任务
            }
        }

        return results;
    }

    /**
     * \brief Executes the tasks with the Best strategy.
     *
     * \param comparator The comparator function.
     * \param args The arguments to pass to the tasks.
     * \param timeout Maximum duration to wait for results.
     * \return A pair containing the task name and best result, or std::nullopt if no results.
     */
    std::optional<std::pair<std::string, Ret>> execute_best(
        ComparatorType comparator, Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        auto results = execute_all(std::forward<Args>(args)..., timeout);

        if (results.empty())
        {
            return std::nullopt;
        }

        auto best = results[0];
        for (size_t i = 1; i < results.size(); ++i)
        {
            if (comparator(results[i].second, best.second))
            {
                best = results[i];
            }
        }

        return best;
    }

    /**
     * \brief Executes the tasks with the OrderWith strategy.
     *
     * \param condition The condition function.
     * \param args The arguments to pass to the tasks.
     * \param timeout Maximum duration to wait for a result.
     * \return A pair containing the task name and result, or std::nullopt if none match or timeout.
     */
    std::optional<std::pair<std::string, Ret>> execute_order_with(
        ConditionType condition, Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        if (tasks_.empty())
        {
            return std::nullopt;
        }

        auto start = std::chrono::steady_clock::now();
        for (auto& [name, task] : tasks_)
        {
            // 检查超时
            if (timeout.count() > 0)
            {
                auto now = std::chrono::steady_clock::now();
                if (now - start >= timeout)
                {
                    return std::nullopt;
                }
            }

            try
            {
                auto fut = task.run(args...);

                if (timeout.count() > 0)
                {
                    auto remaining = timeout - (std::chrono::steady_clock::now() - start);
                    if (fut.wait_for(remaining) != std::future_status::ready)
                    {
                        return std::nullopt;
                    }
                }
                else
                {
                    fut.wait();
                }

                auto result = fut.get();
                if (condition(result))
                {
                    return std::make_pair(name, result);
                }
            }
            catch (...)
            {
                // 忽略失败的任务
            }
        }

        return std::nullopt;
    }

private:
    std::vector<std::pair<std::string, TaskType>> tasks_;
};

namespace aux
{
/**
 * \brief A trait to extract the underlying type from a type.
 *
 * \tparam T The type to extract.
 */
template<typename T>
struct range_trait
{
    using type = T;
};

/**
 * \brief A trait to extract the underlying type from a shared_future.
 *
 * \tparam Ret The result type of the shared_future.
 */
template<typename Ret>
struct range_trait<std::shared_future<Ret>>
{
    using type = Ret;
};

/**
 * \brief An alias template for accessing the type extracted by the range_trait.
 *
 * \tparam T The type to extract the underlying type from.
 */
template<typename T>
using range_trait_t = typename range_trait<T>::type;

/**
 * \brief Transform a range of tasks into a vector of futures.
 *
 * \param range The range of tasks.
 * \param tArgs A tuple of arguments to pass to the tasks.
 * \return A vector of futures of the tasks.
 */
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

/**
 * \brief Get the first result of a task in the range that is ready.
 *
 * \param funcs The range of tasks.
 * \return A pair of the index and result of the first task that is ready.
 */
template<typename Range>
auto getAnyResultPair(Range&& funcs) -> std::pair<size_t, range_trait_t<typename Range::value_type>>
{
    using result_type = range_trait_t<typename Range::value_type>;
    using result_pair = std::pair<size_t, result_type>;

    std::promise<result_pair> resPro;
    auto resfut = resPro.get_future();
    std::atomic<bool> found{false};
    const size_t count = funcs.size();

    std::thread(
        [funcs = std::forward<Range>(funcs), resPro = std::move(resPro), &found, count]() mutable
        {
            for (size_t i = 0; i < count; i++)
            {
                try
                {
                    auto status = funcs[i].wait_for(std::chrono::milliseconds(1));
                    if (status == std::future_status::ready)
                    {
                        auto res = funcs[i].get();
                        if (!found.exchange(true))
                        {
                            resPro.set_value(std::make_pair(i, std::move(res)));
                            return;
                        }
                    }
                }
                catch (...)
                {
                    // 忽略失败的任务
                }
            }

            // 如果所有任务都失败
            if (!found.exchange(true))
            {
                resPro.set_exception(
                    std::make_exception_ptr(std::runtime_error("All tasks failed or no task returned a valid result")));
            }
        })
        .detach();

    return resfut.get();
}

/**
 * \brief Get the first result of a task in the range that matches the condition.
 *
 * \tparam Func The type of the condition function.
 * \tparam Range The type of the range of tasks.
 * \param checkFun The condition function.
 * \param funcs The range of tasks.
 * \return A pair of the index and result of the first task that matches the condition.
 *         If no task matches the condition, returns a pair of -1 and std::nullopt.
 */
template<typename Func,
         typename Range,
         std::enable_if_t<std::is_invocable_r_v<bool, Func, range_trait_t<typename Range::value_type>>, bool> = true>
auto getAnyWithResultPair(Func checkFun,
                          Range&& funcs) -> std::pair<int, std::optional<range_trait_t<typename Range::value_type>>>
{
    using result_type = range_trait_t<typename Range::value_type>;
    using result_pair = std::pair<int, std::optional<result_type>>;

    std::promise<result_pair> resPro;
    auto resfut = resPro.get_future();
    std::atomic<bool> found{false};
    const size_t count = funcs.size();

    std::thread(
        [funcs = std::forward<Range>(funcs),
         checkFun = std::move(checkFun),
         resPro = std::move(resPro),
         &found]() mutable
        {
            for (size_t i = 0; i < count; i++)
            {
                try
                {
                    auto status = funcs[i].wait_for(std::chrono::milliseconds(1));
                    if (status == std::future_status::ready)
                    {
                        auto res = funcs[i].get();
                        if (checkFun(res))
                        {
                            if (!found.exchange(true))
                            {
                                resPro.set_value(std::make_pair(static_cast<int>(i), std::move(res)));
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

            // 所有任务完成但没有匹配条件
            if (!found.exchange(true))
            {
                resPro.set_value(std::make_pair(-1, std::nullopt));
            }
        })
        .detach();

    return resfut.get();
}

/**
 * \brief Get the first result of a task in the range that matches the condition in order.
 *
 * \tparam Func The type of the condition function.
 * \tparam Range The type of the range of tasks.
 * \param checkFun The condition function.
 * \param funcs The range of tasks.
 * \return A pair of the index and result of the first task that matches the condition.
 *         If no task matches the condition, returns std::nullopt.
 */
template<typename Func,
         typename Range,
         std::enable_if_t<std::is_invocable_r_v<bool, Func, range_trait_t<typename Range::value_type>>, bool> = true>
auto getOrderWithResultPair(Func&& checkFun, Range&& funcs)
    -> std::pair<size_t, std::optional<range_trait_t<typename Range::value_type>>>
{
    using result_type = range_trait_t<typename Range::value_type>;
    using result_pair = std::pair<size_t, std::optional<result_type>>;

    std::promise<result_pair> resPro;
    auto resfut = resPro.get_future();
    std::atomic<bool> found{false};
    const size_t count = funcs.size();

    std::thread(
        [funcs = std::forward<Range>(funcs),
         checkFun = std::forward<Func>(checkFun),
         resPro = std::move(resPro),
         &found]() mutable
        {
            for (size_t i = 0; i < count; i++)
            {
                try
                {
                    auto res = funcs[i].get();
                    if (checkFun(res))
                    {
                        if (!found.exchange(true))
                        {
                            resPro.set_value(std::make_pair(i, std::move(res)));
                            return;
                        }
                    }
                }
                catch (...)
                {
                    // 忽略失败的任务
                }
            }

            // 没有任务匹配条件
            if (!found.exchange(true))
            {
                resPro.set_value(std::make_pair(count, std::nullopt));
            }
        })
        .detach();

    return resfut.get();
}
} // namespace aux

/**
 * \brief Creates a task that returns a vector of results of all tasks in the range.
 *
 * \tparam Range The type of the range of tasks.
 * \tparam Args The types of the arguments to pass to the tasks.
 * \param range The range of tasks.
 * \param args The arguments to pass to the tasks.
 * \return A task that returns a vector of results of all tasks in the range.
 */
template<typename Range, typename... Args>
inline auto All(const Range& range, Args&&...args) -> Task<std::vector<typename Range::value_type::return_type>()>
{
    using result_type = typename Range::value_type::return_type;
    using vector_type = std::vector<result_type>;

    auto tArgs = std::make_tuple(std::forward<Args>(args)...);
    return Task<vector_type()>(
        [range, tArgs = std::move(tArgs)]() mutable
        {
            std::vector<std::shared_future<result_type>> funcs;
            funcs.reserve(range.size());
            for (const auto& task : range)
            {
                funcs.emplace_back(std::apply(
                    [&task](auto&&...args) { return task.run(std::forward<decltype(args)>(args)...); }, tArgs));
            }

            vector_type res;
            res.reserve(funcs.size());
            for (auto& fut : funcs)
            {
                res.emplace_back(fut.get());
            }
            return res;
        });
}

/**
 * \brief Creates a task that returns the best result from all tasks in the range.
 *
 * \tparam Func The type of the comparison function.
 * \tparam Range The type of the range of tasks.
 * \tparam Args The types of the arguments to pass to the tasks.
 * \param fn The comparison function for results.
 * \param range The range of tasks.
 * \param args The arguments to pass to the tasks.
 * \return A task that returns the best result from all tasks in the range.
 */
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

/**
 * \brief Creates a task that returns the first result that is ready.
 *
 * \tparam Range The type of the range of tasks.
 * \tparam Args The types of the arguments to pass to the tasks.
 * \param range The range of tasks.
 * \param args The arguments to pass to the tasks.
 * \return A task that returns a pair of index and result of the first task that is ready.
 */
template<typename Range, typename... Args>
inline auto Any(const Range& range, Args&&...args) -> Task<std::pair<size_t, typename Range::value_type::return_type>()>
{
    using result_type = typename Range::value_type::return_type;
    using pair_type = std::pair<size_t, result_type>;

    return Task<pair_type()>(
        [range, tArgs = std::make_tuple(std::forward<Args>(args)...)]() mutable
        {
            auto funcs = aux::transform(range, tArgs);
            return aux::getAnyResultPair(std::move(funcs));
        });
}

/**
 * \brief Creates a task that returns the first result that matches the condition.
 *
 * \tparam Func The type of the condition function.
 * \tparam Range The type of the range of tasks.
 * \tparam Args The types of the arguments to pass to the tasks.
 * \param fn The condition function.
 * \param range The range of tasks.
 * \param args The arguments to pass to the tasks.
 * \return A task that returns a pair of index and result of the first task that matches the condition.
 */
template<typename Func, typename Range, typename... Args>
inline auto AnyWith(Func fn,
                    const Range& range,
                    Args&&...args) -> Task<std::pair<int, std::optional<typename Range::value_type::return_type>>()>
{
    using result_type = typename Range::value_type::return_type;
    using pair_type = std::pair<int, std::optional<result_type>>;

    return Task<pair_type()>(
        [range, fn = std::move(fn), tArgs = std::make_tuple(std::forward<Args>(args)...)]() mutable
        {
            auto funcs = aux::transform(range, tArgs);
            return aux::getAnyWithResultPair(std::move(fn), std::move(funcs));
        });
}

/**
 * \brief Creates a task that returns the first result that matches the condition in order.
 *
 * \tparam Func The type of the condition function.
 * \tparam Range The type of the range of tasks.
 * \tparam Args The types of the arguments to pass to the tasks.
 * \param fn The condition function.
 * \param range The range of tasks.
 * \param args The arguments to pass to the tasks.
 * \return A task that returns a pair of index and result of the first task that matches the condition.
 */
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
            return aux::getOrderWithResultPair(std::move(fn), std::move(funcs));
        });
}
} // namespace hyp

#endif // !_HYPARA_HPP_
