#pragma once
////////////////////////////////////////////
#include <msquic.h>
///////////////////////////////////////////
#include <cstdint>
#include <iostream>
#include <ostream>
#include <source_location>
///////////////////////////////////////////
// Utils can not use any raven header file

#define LOGE(...)                                                                                  \
    rvn::utils::LOG(std::source_location::current(), "LOGGING UNEXPECTED STATE: ", __VA_ARGS__)

namespace rvn::utils {
template <typename E> constexpr typename std::underlying_type<E>::type to_underlying(E e) noexcept {
    return static_cast<typename std::underlying_type<E>::type>(e);
}

template <typename T> static void print(std::ostream &os, T value) { os << value << std::endl; }

template <typename T, typename... Args> static void print(std::ostream &os, T value, Args... args) {
    os << value << " ";
    print(os, args...);
}

template <typename... Args> static void ASSERT_LOG_THROW(bool assertion, Args... args) {
    if (!assertion) {
        print(std::cerr, args...);
        throw std::runtime_error("Assertion failed");
    }
}

template <typename... Args> static void LOG(const std::source_location location, Args... args) {
    std::clog << "file: " << location.file_name() << '(' << location.line() << ':'
              << location.column() << ") `" << location.function_name() << "`: ";
    print(std::cerr, args...);
}

template <typename... Args> QUIC_STATUS NoOpSuccess(Args...) { return QUIC_STATUS_SUCCESS; }

template <typename... Args> void NoOpVoid(Args...) { return; };

} // namespace rvn::utils

namespace rvn {
struct MOQTUtilities {
    static void check_setting_assertions(QUIC_SETTINGS *Settings_, uint32_t SettingsSize_) {
        utils::ASSERT_LOG_THROW(Settings_ != nullptr, "Settings_ is nullptr");
        utils::ASSERT_LOG_THROW(SettingsSize_ != 0, "SettingsSize_ is 0");
    }
};
} // namespace rvn
