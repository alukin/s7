//
// Created by al on 04.06.25.
//

#ifndef S7DATAVARIANT_HPP
#define S7DATAVARIANT_HPP

#include <utility>      // For std::move
#include <variant>      // For std::variant, std::get, std::visit
#include <complex>      // For std::complex<double>
#include <vector>       // For std::vector
#include <numeric>      // For std::inner_product
#include <string>       // For std::string messages, std::to_string
#include <iostream>     // For operator<< (ostream)
#include <stdexcept>    // For std::exception

namespace s7
{
    class DataVariantError : public std::exception {
    private:
        std::string message;
    public:
        explicit DataVariantError(const char* msg) : message(msg) {};
        explicit DataVariantError(const std::string& msg) : message(msg) {};
        [[nodiscard]] const char* what() const noexcept final
        {
            return message.c_str();
        }
    };

    // Enum to represent the underlying data type
    enum class DataType {
        NONE,           // Represents an uninitialized or empty state (std::monostate)
        DOUBLE,
        COMPLEX,
        VECTOR_DOUBLE,
        VECTOR_COMPLEX
    };

    // Helper function to get a string representation of DataType for error messages
    inline std::string dataTypeToString(const DataType type) {
        switch (type) {
            case DataType::NONE: return "NONE";
            case DataType::DOUBLE: return "DOUBLE";
            case DataType::COMPLEX: return "COMPLEX";
            case DataType::VECTOR_DOUBLE: return "VECTOR_DOUBLE";
            case DataType::VECTOR_COMPLEX: return "VECTOR_COMPLEX";
        }
        return "UNKNOWN_TYPE_ERROR"; // Should not be reached with proper enum handling
    }

    class DataVariant {
    private:
        std::variant<std::monostate, double, std::complex<double>, std::vector<double>, std::vector<std::complex<double>>> data_;
        DataType type_;

    public:
        // Declare the global operator<< as a friend so it can access private members
        friend std::ostream& operator<<(std::ostream& os, const DataVariant& dv);

        // --- Constructors ---
        // Default constructor now represents an empty/none state
        DataVariant() : data_(std::monostate{}), type_(DataType::NONE) {}

        explicit DataVariant(double val) : data_(val), type_(DataType::DOUBLE) {}
        explicit DataVariant(std::complex<double> val) : data_(val), type_(DataType::COMPLEX) {}
        explicit DataVariant(std::vector<double> val) : data_(std::move(val)), type_(DataType::VECTOR_DOUBLE) {} // Use std::move for vectors
        explicit DataVariant(std::vector<std::complex<double>> val) : data_(std::move(val)), type_(DataType::VECTOR_COMPLEX) {} // Use std::move for vectors

        // --- Getters (returning const references for vectors) ---
        [[nodiscard]] DataType getType() const {
            return type_;
        }

        [[nodiscard]] double asDouble() const {
            if (type_ == DataType::DOUBLE) {
                return std::get<double>(data_);
            }
            throw DataVariantError("DataVariant does not contain a double. Actual type: " + dataTypeToString(type_));
        }

        [[nodiscard]] std::complex<double> asComplex() const {
            if (type_ == DataType::COMPLEX) {
                return std::get<std::complex<double>>(data_);
            }
            throw DataVariantError("DataVariant does not contain a complex. Actual type: " + dataTypeToString(type_));
        }

        [[nodiscard]] const std::vector<double>& asVectorDouble() const { // Returns const reference
            if (type_ == DataType::VECTOR_DOUBLE) {
                return std::get<std::vector<double>>(data_);
            }
            throw DataVariantError("DataVariant does not contain a vector of doubles. Actual type: " + dataTypeToString(type_));
        }

        [[nodiscard]] const std::vector<std::complex<double>>& asVectorComplex() const { // Returns const reference
            if (type_ == DataType::VECTOR_COMPLEX) {
                return std::get<std::vector<std::complex<double>>>(data_);
            }
            throw DataVariantError("DataVariant does not contain a vector of complex. Actual type: " + dataTypeToString(type_));
        }

        // --- Assignment Operators ---
        DataVariant& operator=(double val) {
            data_ = val;
            type_ = DataType::DOUBLE;
            return *this;
        }
        DataVariant& operator=(std::complex<double> val) {
            data_ = val;
            type_ = DataType::COMPLEX;
            return *this;
        }
        DataVariant& operator=(std::vector<double> val) {
            data_ = std::move(val); // Use std::move
            type_ = DataType::VECTOR_DOUBLE;
            return *this;
        }
        DataVariant& operator=(std::vector<std::complex<double>> val) {
            data_ = std::move(val); // Use std::move
            type_ = DataType::VECTOR_COMPLEX;
            return *this;
        }

        // --- Arithmetic Operators ---

        // Operator+
        DataVariant operator+(const DataVariant& other) const {
            if (type_ == DataType::DOUBLE && other.type_ == DataType::DOUBLE) {
                return DataVariant(asDouble() + other.asDouble());
            } else if (type_ == DataType::COMPLEX && other.type_ == DataType::COMPLEX) {
                return DataVariant(asComplex() + other.asComplex());
            } else if (type_ == DataType::VECTOR_DOUBLE && other.type_ == DataType::VECTOR_DOUBLE) {
                const auto& vec1 = asVectorDouble();
                const auto& vec2 = other.asVectorDouble();
                if (vec1.size() != vec2.size()) {
                    throw DataVariantError("Vector sizes mismatch for addition: " +
                                           std::to_string(vec1.size()) + " != " + std::to_string(vec2.size()) + ".");
                }
                std::vector<double> result(vec1.size());
                for (size_t i = 0; i < vec1.size(); ++i) {
                    result[i] = vec1[i] + vec2[i];
                }
                return DataVariant(result);
            } else if (type_ == DataType::VECTOR_COMPLEX && other.type_ == DataType::VECTOR_COMPLEX) {
                const auto& vec1 = asVectorComplex();
                const auto& vec2 = other.asVectorComplex();
                if (vec1.size() != vec2.size()) {
                    throw DataVariantError("Vector sizes mismatch for addition: " +
                                           std::to_string(vec1.size()) + " != " + std::to_string(vec2.size()) + ".");
                }
                std::vector<std::complex<double>> result(vec1.size());
                for (size_t i = 0; i < vec1.size(); ++i) {
                    result[i] = vec1[i] + vec2[i];
                }
                return DataVariant(result);
            }
            throw DataVariantError("Unsupported DataVariant addition: " + dataTypeToString(type_) + " + " + dataTypeToString(other.type_));
        }

        // Operator-
        DataVariant operator-(const DataVariant& other) const {
            if (type_ == DataType::DOUBLE && other.type_ == DataType::DOUBLE) {
                return DataVariant(asDouble() - other.asDouble());
            } else if (type_ == DataType::COMPLEX && other.type_ == DataType::COMPLEX) {
                return DataVariant(asComplex() - other.asComplex());
            } else if (type_ == DataType::VECTOR_DOUBLE && other.type_ == DataType::VECTOR_DOUBLE) {
                const auto& vec1 = asVectorDouble();
                const auto& vec2 = other.asVectorDouble();
                if (vec1.size() != vec2.size()) {
                    throw DataVariantError("Vector sizes mismatch for subtraction: " +
                                           std::to_string(vec1.size()) + " != " + std::to_string(vec2.size()) + ".");
                }
                std::vector<double> result(vec1.size());
                for (size_t i = 0; i < vec1.size(); ++i) {
                    result[i] = vec1[i] - vec2[i];
                }
                return DataVariant(result);
            } else if (type_ == DataType::VECTOR_COMPLEX && other.type_ == DataType::VECTOR_COMPLEX) {
                const auto& vec1 = asVectorComplex();
                const auto& vec2 = other.asVectorComplex();
                if (vec1.size() != vec2.size()) {
                    throw DataVariantError(std::format("Vector sizes mismatch for subtraction: {}  != {}.", vec1.size(), vec2.size()));
                }
                std::vector<std::complex<double>> result(vec1.size());
                for (size_t i = 0; i < vec1.size(); ++i) {
                    result[i] = vec1[i] - vec2[i];

                }
                return DataVariant(result);
            }
            throw DataVariantError("Unsupported DataVariant subtraction: " + dataTypeToString(type_) + " - " + dataTypeToString(other.type_));
        }

        // Operator* (Multiplication: Scalar * Scalar, Scalar * Vector, Vector * Vector (Dot Product))
        DataVariant operator*(const DataVariant& other) const {
            // Scalar * Scalar
            if (type_ == DataType::DOUBLE && other.type_ == DataType::DOUBLE) {
                return DataVariant(asDouble() * other.asDouble());
            } else if (type_ == DataType::COMPLEX && other.type_ == DataType::COMPLEX) {
                return DataVariant(asComplex() * other.asComplex());
            }
            // Scalar * Vector (or Vector * Scalar)
            else if (type_ == DataType::DOUBLE && other.type_ == DataType::VECTOR_DOUBLE) {
                double scalar = asDouble();
                const auto& vec = other.asVectorDouble();
                std::vector<double> result(vec.size());
                for (size_t i = 0; i < vec.size(); ++i) {
                    result[i] = scalar * vec[i];
                }
                return DataVariant(result);
            } else if (type_ == DataType::VECTOR_DOUBLE && other.type_ == DataType::DOUBLE) {
                const auto& vec = asVectorDouble();
                double scalar = other.asDouble();
                std::vector<double> result(vec.size());
                for (size_t i = 0; i < vec.size(); ++i) {
                    result[i] = vec[i] * scalar;
                }
                return DataVariant(result);
            } else if (type_ == DataType::COMPLEX && other.type_ == DataType::VECTOR_COMPLEX) {
                std::complex<double> scalar = asComplex();
                const auto& vec = other.asVectorComplex();
                std::vector<std::complex<double>> result(vec.size());
                for (size_t i = 0; i < vec.size(); ++i) {
                    result[i] = scalar * vec[i];
                }
                return DataVariant(result);
            } else if (type_ == DataType::VECTOR_COMPLEX && other.type_ == DataType::COMPLEX) {
                const auto& vec = asVectorComplex();
                std::complex<double> scalar = other.asComplex();
                std::vector<std::complex<double>> result(vec.size());
                for (size_t i = 0; i < vec.size(); ++i) {
                    result[i] = vec[i] * scalar;
                }
                return DataVariant(result);
            }
            // Vector * Vector (Dot Product)
            else if (type_ == DataType::VECTOR_DOUBLE && other.type_ == DataType::VECTOR_DOUBLE) {
                const auto& vec1 = asVectorDouble();
                const auto& vec2 = other.asVectorDouble();
                if (vec1.size() != vec2.size()) {
                    throw DataVariantError("Vector sizes mismatch for dot product: " +
                                           std::to_string(vec1.size()) + " != " + std::to_string(vec2.size()) + ".");
                }
                double dot_product = std::inner_product(vec1.begin(), vec1.end(), vec2.begin(), 0.0);
                return DataVariant(dot_product);
            } else if (type_ == DataType::VECTOR_COMPLEX && other.type_ == DataType::VECTOR_COMPLEX) {
                const auto& vec1 = asVectorComplex();
                const auto& vec2 = other.asVectorComplex();
                if (vec1.size() != vec2.size()) {
                    throw DataVariantError("Vector sizes mismatch for dot product: " +
                                           std::to_string(vec1.size()) + " != " + std::to_string(vec2.size()) + ".");
                }
                std::complex<double> dot_product = 0.0;
                for (size_t i = 0; i < vec1.size(); ++i) {
                    dot_product += vec1[i] * std::conj(vec2[i]); // Standard complex dot product often uses conjugate
                }
                return DataVariant(dot_product);
            }

            throw DataVariantError("Unsupported DataVariant multiplication: " + dataTypeToString(type_) + " * " + dataTypeToString(other.type_));
        }

        // Operator/ (Division: Scalar / Scalar, Vector / Scalar)
        DataVariant operator/(const DataVariant& other) const {
            // Scalar / Scalar
            if (type_ == DataType::DOUBLE && other.type_ == DataType::DOUBLE) {
                if (other.asDouble() == 0.0) {
                    throw DataVariantError("Division by zero for double.");
                }
                return DataVariant(asDouble() / other.asDouble());
            } else if (type_ == DataType::COMPLEX && other.type_ == DataType::COMPLEX) {
                if (std::abs(other.asComplex()) == 0.0) { // Check magnitude for complex zero
                    throw DataVariantError("Division by zero for complex.");
                }
                return DataVariant(asComplex() / other.asComplex());
            }
            // Vector / Scalar
            else if (type_ == DataType::VECTOR_DOUBLE && other.type_ == DataType::DOUBLE) {
                double scalar = other.asDouble();
                if (scalar == 0.0) {
                    throw DataVariantError("Division by zero scalar for vector of doubles.");
                }
                const auto& vec = asVectorDouble();
                std::vector<double> result(vec.size());
                for (size_t i = 0; i < vec.size(); ++i) {
                    result[i] = vec[i] / scalar;
                }
                return DataVariant(result);
            } else if (type_ == DataType::VECTOR_COMPLEX && other.type_ == DataType::COMPLEX) {
                std::complex<double> scalar = other.asComplex();
                if (std::abs(scalar) == 0.0) {
                    throw DataVariantError("Division by zero scalar for vector of complex.");
                }
                const auto& vec = asVectorComplex();
                std::vector<std::complex<double>> result(vec.size());
                for (size_t i = 0; i < vec.size(); ++i) {
                    result[i] = vec[i] / scalar;
                }
                return DataVariant(result);
            }
            throw DataVariantError("Unsupported DataVariant division: " + dataTypeToString(type_) + " / " + dataTypeToString(other.type_));
        }

        // Unary minus operator
        DataVariant operator-() const {
            if (type_ == DataType::DOUBLE) {
                return DataVariant(-asDouble());
            } else if (type_ == DataType::COMPLEX) {
                return DataVariant(-asComplex());
            } else if (type_ == DataType::VECTOR_DOUBLE) {
                const auto& vec = asVectorDouble();
                std::vector<double> result(vec.size());
                for (size_t i = 0; i < vec.size(); ++i) {
                    result[i] = -vec[i];
                }
                return DataVariant(result);
            } else if (type_ == DataType::VECTOR_COMPLEX) {
                const auto& vec = asVectorComplex();
                std::vector<std::complex<double>> result(vec.size());
                for (size_t i = 0; i < vec.size(); ++i) {
                    result[i] = -vec[i];
                }
                return DataVariant(result);
            }
            throw DataVariantError("Unsupported unary minus for type: " + dataTypeToString(type_));
        }
    };

    // Global operator<< for DataVariant
    inline std::ostream& operator<<(std::ostream& os, const DataVariant& dv) {
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                os << "DataVariant(NONE)";
            } else if constexpr (std::is_same_v<T, double>) {
                os << arg;
            } else if constexpr (std::is_same_v<T, std::complex<double>>) {
                os << arg;
            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                os << "[";
                for (size_t i = 0; i < arg.size(); ++i) {
                    os << arg[i] << (i == arg.size() - 1 ? "" : ", ");
                }
                os << "]";
            } else if constexpr (std::is_same_v<T, std::vector<std::complex<double>>>) {
                os << "[";
                for (size_t i = 0; i < arg.size(); ++i) {
                    os << arg[i] << (i == arg.size() - 1 ? "" : ", ");
                }
                os << "]";
            }
        }, dv.data_); // data_ is private, so we need to make operator<< a friend or provide a public helper.
                      // For simplicity in this example, we'll assume direct access in the same namespace
                      // or that DataVariant.data_ could be publicly accessible for visit.
                      // The more common way is to add a private visitor or a public helper method.
        return os;
    }

} // namespace s7

#endif //S7DATAVARIANT_HPP