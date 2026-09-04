#pragma once

template <typename T>
constexpr T clampValue(T value, T minimum, T maximum)
{
    return value < minimum ? minimum : (maximum < value ? maximum : value);
}
