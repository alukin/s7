//
// Created by al on 30.05.25.
//

#pragma once

#include <variant>
#include <complex>
#include <vector>
#include <iostream>

namespace s7
{
    enum class DataType
    {
        DOUBLE,
        COMPLEX,
        DOUBLE_VECTOR,
        COMPLEX_VECTOR
    };

    using UniValue = std::variant<
        double,
        std::complex<double>,
        std::vector<double>,
        std::vector<std::complex<double>>
    >;

    // Overload the stream insertion operator for UnifiedData
    std::ostream& operator<<(std::ostream& os, const UniValue& data)
    {
        // Use std::visit to handle each possible type within the variant
        std::visit([&os](auto&& arg)
        {
            // Capture os by reference
            using T = std::decay_t<decltype(arg)>; // Get the actual type without ref/const

            if constexpr (std::is_same_v<T, double>)
            {
                os << arg;
            }
            else if constexpr (std::is_same_v<T, std::complex<double>>)
            {
                os << arg; // std::complex has its own operator<<
            }
            else if constexpr (std::is_same_v<T, std::vector<double>>)
            {
                os << "[";
                for (size_t i = 0; i < arg.size(); ++i)
                {
                    os << arg[i];
                    if (i < arg.size() - 1)
                    {
                        os << ", ";
                    }
                }
                os << "]";
            }
            else if constexpr (std::is_same_v<T, std::vector<std::complex<double>>>)
            {
                os << "[";
                for (size_t i = 0; i < arg.size(); ++i)
                {
                    os << arg[i]; // std::complex has its own operator<<
                    if (i < arg.size() - 1)
                    {
                        os << ", ";
                    }
                }
                os << "]";
            }
            // If you were to add a new type to UnifiedData, you'd need to add another
            // else if constexpr branch here to handle its printing.
        }, data); // Apply the lambda to the UnifiedData object

        return os; // Return the ostream reference to allow chaining
    };
}

