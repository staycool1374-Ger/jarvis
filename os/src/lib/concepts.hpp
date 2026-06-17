#pragma once

namespace kernel {

template<typename T>
concept Integral = __is_integral(T);

template<typename T>
concept TriviallyCopiable = __is_trivially_copyable(T);

template<typename T>
concept ValueType = !__is_reference(T);

template<typename T>
concept ErrorEnum = __is_enum(T);

template<typename T>
concept Lockable = requires(T t) {
    { t.lock() };
    { t.unlock() };
};

} // namespace kernel
