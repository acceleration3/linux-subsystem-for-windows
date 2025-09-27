#pragma once

#include <utility>

template <typename FunctionT>
class deferred {
public:
    explicit deferred(FunctionT&& function) : m_function(std::forward<FunctionT>(function)) {}

    deferred(const deferred&) = default;
    deferred& operator=(const deferred&) = default;

    deferred(deferred&&) = default;
    deferred& operator=(deferred&&) = default;

    ~deferred() {
        m_function();
    }

private:
    FunctionT m_function;
};

template <typename FunctionT>
auto make_deferred(FunctionT&& function) {
    return deferred<FunctionT>(std::forward<FunctionT>(function));
}

#define UNIQUE_VAR_NAME(prefix) UNIQUE_VAR_NAME_IMPL(prefix, __COUNTER__)
#define UNIQUE_VAR_NAME_IMPL(prefix, counter) prefix##counter

#define DEFER_IMPL(varname, content) auto varname = make_deferred([&]() { content })
#define DEFER(content) DEFER_IMPL(UNIQUE_VAR_NAME(__deferred_holder_), content)