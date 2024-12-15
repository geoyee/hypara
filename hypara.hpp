#pragma once

#ifndef _HYPARA_HPP_
#define _HYPARA_HPP_

#pragma warning(disable : 4239)
#pragma warning(disable : 4996)

#include <chrono>
#include <functional>
#include <future>
#include <type_traits>
#include <utility>
#include <vector>

namespace hyp
{
template<typename T>
class Task;

template<typename Ret, typename... Args>
class Task<Ret(Args...)>
{
public:
    using return_type = Ret;

    Task(std::function<Ret(Args...)>&& fn) : m_fn(std::move(fn)) { }

    Task(std::function<const Ret(Args...)>& fn) : m_fn(fn) { }

    ~Task() = default;

    std::shared_future<Ret> run(Args&&...args)
    {
        return std::async(m_fn, std::forward<Args>(args)...);
    }

    void wait(Args&&...args)
    {
        std::async(m_fn, std::forward<Args>(args)...).wait();
    }

    Ret get(Args&&...args)
    {
        return std::async(m_fn, std::forward<Args>(args)...).get();
    }

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
    std::function<Ret(Args...)> m_fn;
};

namespace aux
{
template<typename Ret>
struct range_trait
{
    using type = Ret;
};

template<typename Ret>
struct range_trait<std::shared_future<Ret>>
{
    using type = Ret;
};

template<typename Ret>
using range_trait_t = typename range_trait<Ret>::type;

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

template<typename Range>
auto getAnyResultPair(Range& funcs) -> std::pair<size_t, range_trait_t<typename Range::value_type>>
{
    size_t count = funcs.size();
    while (true)
    {
        for (size_t i = 0; i < count; ++i)
        {
            if (funcs[i].wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
            {
                return std::make_pair(i, funcs[i].get());
            }
        }
    }
}

template<typename Func,
         typename Ret,
         typename Range,
         std::enable_if_t<std::is_invocable_r_v<bool, Func, range_trait_t<typename Range::value_type>>, bool> = true,
         std::enable_if_t<std::is_same_v<typename std::decay_t<Ret>, range_trait_t<typename Range::value_type>>, bool> =
             true>
auto getOnlyResultPair(Func& checkFun, Ret& defVal, Range& funcs) -> std::pair<int, Ret>
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
                    return std::make_pair(static_cast<int>(i), std::move(res));
                }
                if (std::find(isFinished.cbegin(), isFinished.cend(), false) == isFinished.cend())
                {
                    return std::make_pair(-1, std::move(defVal));
                }
            }
        }
    }
}
} // namespace aux

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

template<typename Func, typename Ret, typename Range, typename... Args>
inline static auto Only(Func&& fn, Ret&& def, Range& range, Args&&...args)
    -> Task<std::pair<int, typename Range::value_type::return_type>()>
{
    using result_type = typename Range::value_type::return_type;

    auto resFn =
        [&fn, tDef = std::forward<Ret>(def), &range, tArgs = std::tuple<Args...>(std::forward<Args>(args)...)]()
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