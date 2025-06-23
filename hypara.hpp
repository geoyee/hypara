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
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

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
     * \tparam Fn The type of the callable.
     * \param fn The callable to add.
     */
    template<typename Fn>
    void add_function(Fn&& fn)
    {
        tasks_.emplace_back(std::forward<Fn>(fn));
    }

    /**
     * \brief Adds a member function to the task group.
     *
     * \tparam MemFn The member function type.
     * \tparam Obj The object type.
     * \param mem_fn The member function.
     * \param obj The object on which to invoke the member function.
     */
    template<typename MemFn, typename Obj>
    void add_function(MemFn mem_fn, Obj&& obj)
    {
        tasks_.emplace_back([mem_fn, obj = std::forward<Obj>(obj)](Args... args) -> Ret
                            { return (obj->*mem_fn)(std::forward<Args>(args)...); });
    }

    /**
     * \brief Executes the tasks with the Any strategy.
     *
     * \param args The arguments to pass to the tasks.
     * \param timeout Maximum duration to wait for a result.
     * \return The first result that is ready, or std::nullopt if timeout.
     */
    std::optional<Ret> execute_any(Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        if (tasks_.empty())
        {
            return std::nullopt;
        }

        std::vector<FutureType> futures;
        futures.reserve(tasks_.size());

        for (auto& task : tasks_)
        {
            futures.emplace_back(task.run(args...));
        }

        auto start = std::chrono::steady_clock::now();
        while (true)
        {
            auto now = std::chrono::steady_clock::now();
            if (timeout != std::chrono::seconds(0) && (now - start) > timeout)
            {
                return std::nullopt;
            }

            for (size_t i = 0; i < futures.size(); ++i)
            {
                auto status = futures[i].wait_for(std::chrono::milliseconds(1));
                if (status == std::future_status::ready)
                {
                    try
                    {
                        return futures[i].get();
                    }
                    catch (...)
                    {
                        // Skip failed tasks
                    }
                }
            }
        }
    }

    /**
     * \brief Executes the tasks with the AnyWith strategy.
     *
     * \param condition The condition function.
     * \param args The arguments to pass to the tasks.
     * \param timeout Maximum duration to wait for a result.
     * \return The first result that matches the condition, or std::nullopt if none match or timeout.
     */
    std::optional<Ret> execute_any_with(ConditionType condition,
                                        Args... args,
                                        std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        if (tasks_.empty())
        {
            return std::nullopt;
        }

        std::vector<FutureType> futures;
        futures.reserve(tasks_.size());

        for (auto& task : tasks_)
        {
            futures.emplace_back(task.run(args...));
        }

        std::vector<bool> completed(tasks_.size(), false);
        size_t count = tasks_.size();

        auto start = std::chrono::steady_clock::now();
        while (count > 0)
        {
            auto now = std::chrono::steady_clock::now();
            if (timeout != std::chrono::seconds(0) && (now - start) > timeout)
            {
                return std::nullopt;
            }

            for (size_t i = 0; i < futures.size(); ++i)
            {
                if (completed[i])
                {
                    continue;
                }

                auto status = futures[i].wait_for(std::chrono::milliseconds(1));
                if (status == std::future_status::ready)
                {
                    try
                    {
                        auto result = futures[i].get();
                        completed[i] = true;
                        count--;

                        if (condition(result))
                        {
                            return result;
                        }
                    }
                    catch (...)
                    {
                        completed[i] = true;
                        count--;
                    }
                }
            }
        }

        return std::nullopt;
    }

    /**
     * \brief Executes the tasks with the All strategy.
     *
     * \param args The arguments to pass to the tasks.
     * \param timeout Maximum duration to wait for all results.
     * \return A vector of all completed results (may be partial if timeout).
     */
    std::vector<Ret> execute_all(Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        std::vector<FutureType> futures;
        futures.reserve(tasks_.size());

        for (auto& task : tasks_)
        {
            futures.emplace_back(task.run(args...));
        }

        std::vector<Ret> results;
        results.reserve(futures.size());

        auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < futures.size(); ++i)
        {
            auto now = std::chrono::steady_clock::now();
            if (timeout != std::chrono::seconds(0) && (now - start) > timeout)
            {
                break; // Return partial results
            }

            try
            {
                if (timeout == std::chrono::seconds(0))
                {
                    results.emplace_back(futures[i].get());
                }
                else
                {
                    auto remaining = timeout - (now - start);
                    if (futures[i].wait_for(remaining) == std::future_status::ready)
                    {
                        results.emplace_back(futures[i].get());
                    }
                    else
                    {
                        break; // Timeout, return partial results
                    }
                }
            }
            catch (...)
            {
                // Skip failed tasks
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
     * \return The best result according to the comparator, or std::nullopt if no results.
     */
    std::optional<Ret> execute_best(ComparatorType comparator,
                                    Args... args,
                                    std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        auto results = execute_all(std::forward<Args>(args)..., timeout);

        if (results.empty())
        {
            return std::nullopt;
        }

        auto best = results[0];
        for (size_t i = 1; i < results.size(); ++i)
        {
            if (comparator(results[i], best))
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
     * \return The first result that matches the condition in order, or std::nullopt if none match or timeout.
     */
    std::optional<Ret> execute_order_with(ConditionType condition,
                                          Args... args,
                                          std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        if (tasks_.empty())
        {
            return std::nullopt;
        }

        auto start = std::chrono::steady_clock::now();
        for (auto& task : tasks_)
        {
            auto now = std::chrono::steady_clock::now();
            if (timeout != std::chrono::seconds(0) && (now - start) > timeout)
            {
                return std::nullopt;
            }

            try
            {
                auto fut = task.run(args...);
                std::future_status status;

                if (timeout == std::chrono::seconds(0))
                {
                    // 无限等待
                    fut.wait();
                    status = std::future_status::ready;
                }
                else
                {
                    // 有限等待
                    auto remaining = timeout - (now - start);
                    status = fut.wait_for(remaining);
                }

                if (status == std::future_status::ready)
                {
                    auto result = fut.get();
                    if (condition(result))
                    {
                        return result;
                    }
                }
                else
                {
                    // 超时
                    return std::nullopt;
                }
            }
            catch (...)
            {
                // Skip failed tasks
            }
        }

        return std::nullopt;
    }

private:
    std::vector<TaskType> tasks_;
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
    auto sharedData = std::make_shared<std::atomic<bool>>(false);
    const size_t count = funcs.size();
    std::vector<bool> isFinished(count, false);

    std::thread monitor(
        [funcs = std::forward<Range>(funcs),
         resPro = std::move(resPro),
         sharedData,
         isFinished = std::move(isFinished),
         count]() mutable
        {
            size_t completed = 0;
            while (completed < count)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    if (isFinished[i])
                    {
                        continue;
                    }
                    if (funcs[i].wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
                    {
                        try
                        {
                            auto res = funcs[i].get();
                            isFinished[i] = true;
                            ++completed;
                            if (!sharedData->exchange(true))
                            {
                                resPro.set_value(std::make_pair(i, std::move(res)));
                                return;
                            }
                        }
                        catch (...)
                        {
                            isFinished[i] = true;
                            ++completed;
                        }
                    }
                }
            }

            // If all tasks failed, set an exception
            if (!sharedData->exchange(true))
            {
                try
                {
                    throw std::runtime_error("All tasks failed or no task returned a valid result");
                }
                catch (...)
                {
                    resPro.set_exception(std::current_exception());
                }
            }
        });

    monitor.detach();
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
auto getAnyWithResultPair(Func checkFun, Range&& funcs)
    -> std::pair<int, std::optional<range_trait_t<typename Range::value_type>>>
{
    using result_type = range_trait_t<typename Range::value_type>;
    using result_pair = std::pair<int, std::optional<result_type>>;

    std::promise<result_pair> resPro;
    auto resfut = resPro.get_future();
    auto sharedData = std::make_shared<std::atomic<bool>>(false);
    const size_t count = funcs.size();
    std::vector<bool> isFinished(count, false);

    std::thread monitor(
        [funcs = std::forward<Range>(funcs),
         checkFun = std::move(checkFun),
         resPro = std::move(resPro),
         sharedData,
         isFinished = std::move(isFinished),
         count]() mutable
        {
            size_t completed = 0;
            while (completed < count)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    if (isFinished[i])
                    {
                        continue;
                    }
                    if (funcs[i].wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
                    {
                        try
                        {
                            auto res = funcs[i].get();
                            isFinished[i] = true;
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
                            isFinished[i] = true;
                            ++completed;
                        }
                    }
                }
            }

            // All tasks completed but none matched the condition
            if (!sharedData->exchange(true))
            {
                resPro.set_value(std::make_pair(-1, std::nullopt));
            }
        });

    monitor.detach();
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
    auto sharedData = std::make_shared<std::atomic<bool>>(false);

    std::thread monitor(
        [funcs = std::forward<Range>(funcs),
         checkFun = std::forward<Func>(checkFun),
         resPro = std::move(resPro),
         sharedData]() mutable
        {
            const size_t count = funcs.size();
            for (size_t i = 0; i < count; ++i)
            {
                try
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
                catch (...)
                {
                    // Skip failed tasks
                }
            }

            // No task matched the condition
            if (!sharedData->exchange(true))
            {
                resPro.set_value(std::make_pair(count, std::nullopt));
            }
        });

    monitor.detach();
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
inline auto AnyWith(Func fn, const Range& range, Args&&...args)
    -> Task<std::pair<int, std::optional<typename Range::value_type::return_type>>()>
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