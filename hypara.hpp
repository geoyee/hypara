#pragma once

#ifndef _HYPARA_HPP_
#define _HYPARA_HPP_

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace hyp
{
/**
 * \brief Represents an asynchronous task that can be executed with specified arguments.
 * 
 * \tparam T Function signature type (e.g., int(double, float))
 */
template<typename T>
class Task;

/**
 * \brief Specialization of Task for function signatures.
 * 
 * \tparam Ret Return type of the task
 * \tparam Args Argument types that the task accepts
 */
template<typename Ret, typename... Args>
class Task<Ret(Args...)>
{
public:
    using return_type = Ret;
    using function_type = std::function<Ret(Args...)>;

    /**
     * \brief Constructs a Task from a callable object
     * 
     * \tparam Fn Type of the callable object
     * \param fn Callable object to be wrapped
     */
    template<typename Fn, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Fn>, Task>>>
    explicit Task(Fn&& fn) : m_fn(std::forward<Fn>(fn))
    {
    }

    /**
     * \brief Executes the task asynchronously
     * 
     * \param args Arguments to pass to the task
     * \return std::shared_future<Ret> Shared future representing the task result
     */
    std::shared_future<Ret> run(Args... args) const
    {
        auto task = std::make_shared<std::packaged_task<Ret(Args...)>>(m_fn);
        auto fut = task->get_future().share();

        std::thread(
            [task, args...]() mutable
            {
                try
                {
                    (*task)(std::forward<Args>(args)...);
                }
                catch (...)
                {
                } // Suppress exceptions
            })
            .detach();

        return fut;
    }

    /**
     * \brief Blocks until the task completes
     * 
     * \param args Arguments to pass to the task
     */
    void wait(Args... args) const
    {
        run(std::forward<Args>(args)...).wait();
    }

    /**
     * \brief Executes the task and returns the result
     * 
     * \param args Arguments to pass to the task
     * \return Ret Result of the task
     */
    Ret get(Args... args) const
    {
        return run(std::forward<Args>(args)...).get();
    }

    /**
     * \brief Chains another task to be executed after this one
     * 
     * \tparam Func Type of the continuation function
     * \param fn Continuation function
     * \return Task<NewRet(Args...)> New task representing the continuation
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
    function_type m_fn;
};

namespace aux
{
/**
 * \brief Type trait to extract the result type from futures
 * 
 * \tparam T Type to extract result from
 */
template<typename T>
struct range_trait
{
    using type = T;
};

/**
 * \brief Specialization for std::shared_future
 * 
 * \tparam Ret Result type of the future
 */
template<typename Ret>
struct range_trait<std::shared_future<Ret>>
{
    using type = Ret;
};

template<typename T>
using range_trait_t = typename range_trait<T>::type;

/**
 * \brief Transforms a range of tasks into a vector of futures
 * 
 * \tparam Range Type of the task range
 * \tparam Args Argument types for the tasks
 * \param range Container of tasks
 * \param tArgs Arguments to pass to each task
 * \return std::vector<std::shared_future<result_type>> Vector of futures
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
 * \brief Waits for any task to complete and returns the first valid result
 * 
 * \tparam Range Type of future container
 * \param funcs Container of futures
 * \param timeout Maximum duration to wait
 * \return std::pair<size_t, result_type> Index and result of the completed task
 */
template<typename Range>
auto getAnyResultPair(Range&& funcs, std::chrono::milliseconds timeout)
    -> std::pair<size_t, range_trait_t<typename std::decay_t<decltype(*std::begin(funcs))>>>
{
    using result_type = range_trait_t<typename std::decay_t<decltype(*std::begin(funcs))>>;
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
                resPro.set_exception(std::make_exception_ptr(std::runtime_error("All tasks failed or timed out")));
            }
        });

    monitor.detach();
    return resfut.get();
}

/**
 * \brief Waits for any task satisfying a condition to complete
 * 
 * \tparam Func Type of condition function
 * \tparam Range Type of future container
 * \param checkFun Condition function
 * \param funcs Container of futures
 * \param timeout Maximum duration to wait
 * \return std::pair<int, std::optional<result_type>> Index and result (if found)
 */
template<typename Func, typename Range>
auto getAnyWithResultPair(Func checkFun, Range&& funcs, std::chrono::milliseconds timeout)
    -> std::pair<int, std::optional<range_trait_t<typename std::decay_t<decltype(*std::begin(funcs))>>>>
{
    using result_type = range_trait_t<typename std::decay_t<decltype(*std::begin(funcs))>>;
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
                            if (checkFun(res) && !sharedData->exchange(true))
                            {
                                resPro.set_value(std::make_pair(static_cast<int>(i), std::move(res)));
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
                resPro.set_value(std::make_pair(-1, std::nullopt));
            }
        });

    monitor.detach();
    return resfut.get();
}

/**
 * \brief Waits for the first task satisfying a condition in order
 * 
 * \tparam Func Type of condition function
 * \tparam Range Type of future container
 * \param checkFun Condition function
 * \param funcs Container of futures
 * \param timeout Maximum duration to wait
 * \return std::pair<size_t, std::optional<result_type>> Index and result (if found)
 */
template<typename Func, typename Range>
auto getOrderWithResultPair(Func&& checkFun, Range&& funcs, std::chrono::milliseconds timeout)
    -> std::pair<size_t, std::optional<range_trait_t<typename std::decay_t<decltype(*std::begin(funcs))>>>>
{
    using result_type = range_trait_t<typename std::decay_t<decltype(*std::begin(funcs))>>;
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
                        if (checkFun(res) && !sharedData->exchange(true))
                        {
                            resPro.set_value(std::make_pair(i, std::move(res)));
                            return;
                        }
                    }
                }
                catch (...)
                {
                } // Ignore failed tasks
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

/**
 * \brief Executes all tasks and collects their results
 * 
 * \tparam Range Type of task container
 * \tparam Args Argument types for the tasks
 * \param range Container of tasks
 * \param timeout Maximum duration to wait
 * \param args Arguments to pass to tasks
 * \return Task<std::vector<result_type>()> Task producing the results
 */
template<typename Range, typename... Args>
inline auto All(const Range& range, std::chrono::milliseconds timeout, Args&&...args)
    -> Task<std::vector<typename Range::value_type::return_type>()>
{
    using result_type = typename Range::value_type::return_type;
    using vector_type = std::vector<result_type>;

    auto tArgs = std::make_tuple(std::forward<Args>(args)...);
    return Task<vector_type()>(
        [range, tArgs = std::move(tArgs), timeout]() mutable
        {
            auto funcs = aux::transform(range, tArgs);
            vector_type res;
            res.reserve(funcs.size());

            for (auto& fut : funcs)
            {
                if (timeout.count() > 0)
                {
                    if (fut.wait_for(timeout) != std::future_status::ready)
                    {
                        throw std::runtime_error("Task timed out");
                    }
                }
                else
                {
                    fut.wait();
                }
                res.emplace_back(fut.get());
            }
            return res;
        });
}

/**
 * \brief Executes all tasks and returns the best result according to a comparator
 * 
 * \tparam Func Type of comparator function
 * \tparam Range Type of task container
 * \tparam Args Argument types for the tasks
 * \param fn Comparator function
 * \param range Container of tasks
 * \param timeout Maximum duration to wait
 * \param args Arguments to pass to tasks
 * \return Task<result_type()> Task producing the best result
 */
template<typename Func, typename Range, typename... Args>
inline auto Best(Func fn, const Range& range, std::chrono::milliseconds timeout, Args&&...args)
    -> Task<typename Range::value_type::return_type()>
{
    using result_type = typename Range::value_type::return_type;

    return All(range, timeout, std::forward<Args>(args)...)
        .then(
            [fn = std::move(fn)](std::vector<result_type> tmpRes)
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
 * \brief Executes tasks and returns the first completed result
 * 
 * \tparam Range Type of task container
 * \tparam Args Argument types for the tasks
 * \param range Container of tasks
 * \param timeout Maximum duration to wait
 * \param args Arguments to pass to tasks
 * \return Task<std::pair<size_t, result_type>()> Task producing the index and result
 */
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

/**
 * \brief Executes tasks and returns the first result satisfying a condition
 * 
 * \tparam Func Type of condition function
 * \tparam Range Type of task container
 * \tparam Args Argument types for the tasks
 * \param fn Condition function
 * \param range Container of tasks
 * \param timeout Maximum duration to wait
 * \param args Arguments to pass to tasks
 * \return Task<std::pair<int, std::optional<result_type>>()> Task producing the index and result
 */
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

/**
 * \brief Executes tasks in order and returns the first result satisfying a condition
 * 
 * \tparam Func Type of condition function
 * \tparam Range Type of task container
 * \tparam Args Argument types for the tasks
 * \param fn Condition function
 * \param range Container of tasks
 * \param timeout Maximum duration to wait
 * \param args Arguments to pass to tasks
 * \return Task<std::pair<size_t, std::optional<result_type>>()> Task producing the index and result
 */
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
 * \brief Manages and executes groups of tasks with various strategies
 * 
 * \tparam Ret Return type of the tasks
 * \tparam Args Argument types for the tasks
 */
template<typename Ret, typename... Args>
class Worker
{
public:
    using TaskType = Task<Ret(Args...)>;
    using ConditionType = std::function<bool(Ret)>;
    using ComparatorType = std::function<bool(Ret, Ret)>;

    /**
     * \brief Adds a function to the worker
     * 
     * \param name Name identifier for the function
     * \param fn Function to add
     */
    template<typename Fn>
    void add_function(const std::string& name, Fn&& fn)
    {
        tasks_.emplace_back(name, TaskType(std::forward<Fn>(fn)));
    }

    /**
     * \brief Adds a member function to the worker
     * 
     * \tparam MemFn Member function type
     * \tparam Obj Object type
     * \param name Name identifier for the function
     * \param mem_fn Pointer to member function
     * \param obj Object instance to bind
     */
    template<typename MemFn, typename Obj>
    void add_function(const std::string& name, MemFn mem_fn, Obj&& obj)
    {
        tasks_.emplace_back(name,
                            TaskType([mem_fn, obj = std::forward<Obj>(obj)](Args... args) -> Ret
                                     { return (obj->*mem_fn)(std::forward<Args>(args)...); }));
    }

    /**
     * \brief Executes any task and returns the first completed result
     * 
     * \param args Arguments for the tasks
     * \param timeout Maximum duration to wait
     * \return std::optional<std::pair<std::string, Ret>> Name and result of completed task
     */
    std::optional<std::pair<std::string, Ret>> execute_any(
        Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        if (tasks_.empty())
        {
            return std::nullopt;
        }

        auto ms_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
        auto any_task = Any(tasks_vector(), ms_timeout, std::forward<Args>(args)...);
        auto fut = any_task.run();

        try
        {
            auto [index, result] = fut.get();
            return std::make_pair(tasks_[index].first, result);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    /**
     * \brief Executes tasks and returns the first result satisfying a condition
     * 
     * \param condition Condition function
     * \param args Arguments for the tasks
     * \param timeout Maximum duration to wait
     * \return std::optional<std::pair<std::string, Ret>> Name and result of completed task
     */
    std::optional<std::pair<std::string, Ret>> execute_any_with(
        ConditionType condition, Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        if (tasks_.empty())
        {
            return std::nullopt;
        }

        auto ms_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
        auto any_with_task = AnyWith(condition, tasks_vector(), ms_timeout, std::forward<Args>(args)...);
        auto fut = any_with_task.run();

        auto [index, result_opt] = fut.get();
        if (index >= 0 && result_opt)
        {
            return std::make_pair(tasks_[index].first, *result_opt);
        }
        return std::nullopt;
    }

    /**
     * \brief Executes all tasks and returns their results
     * 
     * \param args Arguments for the tasks
     * \param timeout Maximum duration to wait
     * \return std::vector<std::pair<std::string, Ret>> Names and results of all tasks
     */
    std::vector<std::pair<std::string, Ret>> execute_all(
        Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        std::vector<std::pair<std::string, Ret>> results;
        if (tasks_.empty())
        {
            return results;
        }

        auto ms_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
        auto all_task = All(tasks_vector(), ms_timeout, std::forward<Args>(args)...);
        auto fut = all_task.run();

        try
        {
            auto task_results = fut.get();
            for (size_t i = 0; i < tasks_.size(); ++i)
            {
                results.emplace_back(tasks_[i].first, task_results[i]);
            }
        }
        catch (...)
        {
            // Return partial results on timeout
            for (size_t i = 0; i < results.size(); ++i)
            {
                results.emplace_back(tasks_[i].first, Ret{});
            }
        }
        return results;
    }

    /**
     * \brief Executes all tasks and returns the best result
     * 
     * \param comparator Comparator function
     * \param args Arguments for the tasks
     * \param timeout Maximum duration to wait
     * \return std::optional<std::pair<std::string, Ret>> Name and result of best task
     */
    std::optional<std::pair<std::string, Ret>> execute_best(
        ComparatorType comparator, Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        if (tasks_.empty())
        {
            return std::nullopt;
        }

        auto ms_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
        auto best_task = Best(comparator, tasks_vector(), ms_timeout, std::forward<Args>(args)...);
        auto fut = best_task.run();

        try
        {
            auto result = fut.get();
            // Find which task produced the best result
            for (size_t i = 0; i < tasks_.size(); ++i)
            {
                if (tasks_[i].second.get(args...) == result)
                {
                    return std::make_pair(tasks_[i].first, result);
                }
            }
            return std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    /**
     * \brief Executes tasks in order and returns the first result satisfying a condition
     * 
     * \param condition Condition function
     * \param args Arguments for the tasks
     * \param timeout Maximum duration to wait
     * \return std::optional<std::pair<std::string, Ret>> Name and result of completed task
     */
    std::optional<std::pair<std::string, Ret>> execute_order_with(
        ConditionType condition, Args... args, std::chrono::steady_clock::duration timeout = std::chrono::seconds(0))
    {
        if (tasks_.empty())
        {
            return std::nullopt;
        }

        auto ms_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
        auto order_with_task = OrderWith(condition, tasks_vector(), ms_timeout, std::forward<Args>(args)...);
        auto fut = order_with_task.run();

        auto [index, result_opt] = fut.get();
        if (result_opt && index < tasks_.size())
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