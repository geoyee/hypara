#pragma once

#ifndef _HYPARA_HPP_
#define _HYPARA_HPP_

#pragma warning(disable : 4239)
#pragma warning(disable : 4996)

#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
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
 * \param Ret The return type of the task.
 * \param Args The argument types that the task accepts.
 */
template<typename Ret, typename... Args>
class Task<Ret(Args...)>
{
public:
    using return_type = Ret;

    /**
     * \brief Constructs a Task with a callable.
     *
     * \param fn A callable object that takes Args... and returns Ret.
     */
    Task(std::function<Ret(Args...)>&& fn) : m_fn(std::move(fn)) { }

    /**
     * \brief Constructs a Task with a callable.
     *
     * \param fn A callable object that takes Args... and returns Ret.
     */
    Task(std::function<const Ret(Args...)>& fn) : m_fn(fn) { }

    /**
     * \brief Destroys the Task object.
     */
    ~Task() = default;

    /**
     * \brief Runs the task with the given arguments.
     *
     * \param args The arguments to pass to the task.
     * \return A shared future that resolves to the result of the task.
     */
    std::shared_future<Ret> run(Args&&...args)
    {
        return std::async(m_fn, std::forward<Args>(args)...);
    }

    /**
     * \brief Runs the task with the given arguments and waits for it to finish.
     *
     * \param args The arguments to pass to the task.
     * \return void
     */
    void wait(Args&&...args)
    {
        std::async(m_fn, std::forward<Args>(args)...).wait();
    }

    /**
     * \brief Runs the task with the given arguments and returns the result.
     *
     * \param args The arguments to pass to the task.
     * \return The result of the task.
     */
    Ret get(Args&&...args)
    {
        return std::async(m_fn, std::forward<Args>(args)...).get();
    }

    /**
     * \brief Creates a new Task that runs the given function after this Task is finished.
     *
     * \param fn A callable object that takes the result of this Task and returns a new result.
     * \return A new Task that runs `fn` after this Task is finished.
     */
    template<typename Func>
    auto then(Func&& fn) -> Task<typename std::invoke_result_t<Func, Ret>(Args...)>
    {
        using result_type = typename std::invoke_result_t<Func, Ret>;

        auto func = std::move(m_fn);
        auto task = Task<result_type(Args...)>(
            [func, &fn](Args... args)
            {
                std::future<Ret> lastFunc = std::async(func, std::forward<Args>(args)...);
                return std::async(fn, lastFunc.get()).get();
            });

        return task;
    }

private:
    std::function<Ret(Args...)> m_fn; /// The function to run.
};

namespace aux
{
/**
 * \brief A trait to extract the type from a template parameter.
 *
 * \param Ret The type to extract.
 */
template<typename Ret>
struct range_trait
{
    using type = Ret;
};

/**
 * \brief A trait to extract the underlying type from a shared_future.
 *
 * \param Ret The result type of the shared_future.
 */
template<typename Ret>
struct range_trait<std::shared_future<Ret>>
{
    using type = Ret;
};

/**
 * \brief An alias template for accessing the type extracted by the range_trait
 *        struct.
 *
 * \param Ret The type to extract the underlying type from.
 */
template<typename Ret>
using range_trait_t = typename range_trait<Ret>::type;

/**
 * \brief Transform a range of tasks into a vector of futures.
 *
 * \param range The range of tasks.
 * \param tArgs A tuple of arguments to pass to the tasks.
 * \return A vector of futures of the tasks.
 */
template<typename Range, typename... Args>
auto transform(Range& range,
               std::tuple<Args...> tArgs) -> std::vector<std::shared_future<typename Range::value_type::return_type>>
{
    using result_type = typename Range::value_type::return_type;

    std::vector<std::shared_future<result_type>> funcs;
    for (auto& task : range)
    {
        funcs.emplace_back(std::apply([&task](auto&&...args) { return task.run(std::forward<Args>(args)...); }, tArgs));
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
auto getAnyResultPair(Range& funcs) -> std::pair<size_t, range_trait_t<typename Range::value_type>>
{
    using result_type = typename std::pair<size_t, range_trait_t<typename Range::value_type>>;

    std::promise<result_type> resPro;
    auto resfut = resPro.get_future();
    std::thread monitor(
        [funcs = std::move(funcs), &resPro]() mutable
        {
            size_t count = funcs.size();
            while (true)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    if (funcs[i].wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
                    {
                        resPro.set_value(std::make_pair(i, funcs[i].get()));
                        return;
                    }
                }
            }
        });
    monitor.detach();
    return resfut.get();
}

/**
 * \brief Get the first result of a task in the range that matches the condition.
 *
 * \param checkFun The condition function.
 * \param defVal The default value.
 * \param range The range of tasks.
 * \return A pair of the index and result of the first task that matches the condition.
 *         If no task matches the condition, returns a pair of -1 and the default value.
 */
template<typename Func,
         typename Ret,
         typename Range,
         std::enable_if_t<std::is_invocable_r_v<bool, Func, range_trait_t<typename Range::value_type>>, bool> = true,
         std::enable_if_t<std::is_same_v<typename std::decay_t<Ret>, range_trait_t<typename Range::value_type>>, bool> =
             true>
auto getOnlyResultPair(Func& checkFun,
                       Ret& defVal,
                       Range& funcs) -> std::pair<int, range_trait_t<typename Range::value_type>>
{
    using result_type = typename std::pair<int, range_trait_t<typename Range::value_type>>;

    std::promise<result_type> resPro;
    auto resfut = resPro.get_future();
    std::thread monitor(
        [checkFun = std::move(checkFun), defVal = std::move(defVal), funcs = std::move(funcs), &resPro]() mutable
        {
            size_t count = funcs.size();
            std::vector<bool> isFinished(count, false);
            while (true)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    if (funcs[i].wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
                    {
                        auto res = funcs[i].get();
                        isFinished[i] = true;
                        if (checkFun(res))
                        {
                            resPro.set_value(std::make_pair(static_cast<int>(i), std::move(res)));
                            return;
                        }
                        if (std::find(isFinished.cbegin(), isFinished.cend(), false) == isFinished.cend())
                        {
                            resPro.set_value(std::make_pair(-1, std::move(defVal)));
                            return;
                        }
                    }
                }
            }
        });
    monitor.detach();
    return resfut.get();
}
} // namespace aux

/**
 * \brief A task that returns a vector of results of all tasks in the range.
 *
 * \param range The range of tasks.
 * \param args The arguments to pass to the tasks.
 * \return A task that returns a vector of results of all tasks in the range.
 */
template<typename Range, typename... Args>
inline static auto All(Range& range, Args&&...args) -> Task<std::vector<typename Range::value_type::return_type>()>
{
    using result_type = typename Range::value_type::return_type;

    auto resFn = [&range, tArgs = std::tuple<Args...>(std::forward<Args>(args)...)]() mutable
    {
        std::vector<std::shared_future<result_type>> funcs;
        for (auto& task : range)
        {
            funcs.emplace_back(
                std::apply([&task](auto&&...args) { return task.run(std::forward<Args>(args)...); }, std::move(tArgs)));
        }
        size_t count = funcs.size();
        std::vector<result_type> res(count);
        for (size_t i = 0; i < count; ++i)
        {
            res[i] = funcs[i].get();
        }
        return res;
    };

    return static_cast<std::function<std::vector<result_type>()>>(resFn);
}

/**
 * \brief A task that returns the first result that is ready.
 *
 * \param range The range of tasks.
 * \param args The arguments to pass to the tasks.
 * \return A task that returns the first result that is ready.
 */
template<typename Range, typename... Args>
inline static auto Any(Range& range,
                       Args&&...args) -> Task<std::pair<size_t, typename Range::value_type::return_type>()>
{
    using result_type = typename Range::value_type::return_type;

    auto resFn = [&range, tArgs = std::tuple<Args...>(std::forward<Args>(args)...)]() mutable
    {
        auto transforms = aux::transform(range, tArgs);
        return aux::getAnyResultPair(transforms);
    };

    return static_cast<std::function<std::pair<size_t, result_type>()>>(resFn);
}

/**
 * \brief A task that returns the first result that matches the condition.
 *
 * \param fn The condition function.
 * \param def The default value.
 * \param range The range of tasks.
 * \param args The arguments to pass to the tasks.
 * \return A task that returns the first result that matches the condition.
 */
template<typename Func, typename Ret, typename Range, typename... Args>
inline static auto Only(Func&& fn, Ret&& def, Range& range, Args&&...args)
    -> Task<std::pair<int, typename Range::value_type::return_type>()>
{
    using result_type = typename Range::value_type::return_type;

    auto resFn =
        [&fn, &range, tDef = std::forward<Ret>(def), tArgs = std::tuple<Args...>(std::forward<Args>(args)...)]()
    {
        auto transforms = aux::transform(range, tArgs);
        return aux::getOnlyResultPair(fn, tDef, transforms);
    };

    return static_cast<std::function<std::pair<int, result_type>()>>(resFn);
}
} // namespace hyp

#define HypTask hyp::Task
#define HypAll  hyp::All
#define HypAny  hyp::Any
#define HypOnly hyp::Only

#endif // !_HYPARA_HPP_