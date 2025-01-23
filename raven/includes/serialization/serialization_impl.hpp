#pragma once

#include "utilities.hpp"
#include <optional>
#include <serialization/chunk.hpp>
#include <serialization/endianness.hpp>
#include <serialization/messages.hpp>
#include <serialization/quic_var_int.hpp>

namespace rvn::serialization::detail
{
///////////////////////////////////////////////////////////////////////////////////////////////
// Serialize and deserialize trivial types (std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t)

template <typename T>
concept UnsignedInteger =
std::is_same_v<T, std::uint8_t> || std::is_same_v<T, std::uint16_t> ||
std::is_same_v<T, std::uint32_t> || std::is_same_v<T, std::uint64_t>;

using serialize_return_t = std::uint64_t;

// By default we always want to serialize in network byte order
template <UnsignedInteger T, Endianness ToEndianness = NetworkEndian>
serialize_return_t
serialize_trivial(ds::chunk& c, const auto& value, ToEndianness = network_endian)
{
    auto requiredSize = c.size() + sizeof(T);
    c.reserve(requiredSize);

    T valueCastToT = value;
    T valueModifiedEndianness;

    if constexpr (std::is_same_v<ToEndianness, NativeEndian> || std::is_same_v<T, std::uint8_t>)
    {
        c.append(&valueCastToT, sizeof(T));
        return sizeof(T);
    }
    else if constexpr (std::is_same_v<ToEndianness, BigEndian>)
    {
        if constexpr (std::is_same_v<T, std::uint16_t>)
            valueModifiedEndianness = htobe16(valueCastToT);
        else if constexpr (std::is_same_v<T, std::uint32_t>)
            valueModifiedEndianness = htobe32(valueCastToT);
        else if constexpr (std::is_same_v<T, std::uint64_t>)
            valueModifiedEndianness = htobe64(valueCastToT);
        else
            static_assert(false, "Unsupported type, only 16, 32, 64 bit "
                                 "unsigned integers supported");
    }
    else if constexpr (std::is_same_v<ToEndianness, LittleEndian>)
    {
        if constexpr (std::is_same_v<T, std::uint16_t>)
            valueModifiedEndianness = htole16(valueCastToT);
        else if constexpr (std::is_same_v<T, std::uint32_t>)
            valueModifiedEndianness = htole32(valueCastToT);
        else if constexpr (std::is_same_v<T, std::uint64_t>)
            valueModifiedEndianness = htole64(valueCastToT);
        else
            static_assert(false, "Unsupported type, only 16, 32, 64 bit "
                                 "unsigned integers supported");
    }
    else
        static_assert(false, "Unsupported endianness");

    c.append(&valueModifiedEndianness, sizeof(T));

    return sizeof(T);
}

///////////////////////////////////////////////////////////////////////////////////////////////
/*
    x (i): -> quic_var_int
    Indicates that x holds an integer value using the variable-length encoding as described in ([RFC9000], Section 16)
*/
template <typename T, typename ToEndianess = NetworkEndian>
serialize_return_t
serialize(ds::chunk& c, ds::quic_var_int i, ToEndianess = network_endian)
{
    static_assert(std::is_same_v<T, ds::quic_var_int>);

    const auto chunkSize = i.size();
    switch (chunkSize)
    {
        case 1:
        {
            std::uint8_t value = i.value(); // 00xxxxxx
            return serialize_trivial<std::uint8_t>(c, value, ToEndianess{});
        }
        case 2:
        {
            // 01xxxxxx xxxxxxxx
            std::uint16_t value = (std::uint64_t(0b01) << 14) | i.value();
            return serialize_trivial<std::uint16_t>(c, value, ToEndianess{});
        }
        case 4:
        {
            // 10xxxxxx xxxxxxxx ...
            std::uint32_t value = (std::uint64_t(0b10) << 30) | i.value();
            return serialize_trivial<std::uint32_t>(c, value, ToEndianess{});
        }
        case 8:
        {
            // 10xxxxxx xxxxxxxx ...
            std::uint64_t value = (std::uint64_t(0b11) << 62) | i.value();
            return serialize_trivial<std::uint64_t>(c, value, ToEndianess{});
        }
    }
    assert(false);
    return 42;
}

template <typename T> serialize_return_t mock_serialize(ds::quic_var_int i)
{
    static_assert(std::is_same_v<T, ds::quic_var_int>);

    return i.size();
}

/*
    x (L): -> std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t
    Indicates that x is L bits long
*/
template <UnsignedInteger T, typename ToEndianess = NetworkEndian>
serialize_return_t serialize(ds::chunk& c, auto i, ToEndianess = network_endian)
{
    return serialize_trivial<T>(c, i, ToEndianess{});
}

template <UnsignedInteger T>
serialize_return_t mock_serialize(const auto& value)
{
    return sizeof(T);
}

///////////////////////////////////////////////////////////////////////////////////////////////
template <typename T, typename ToEndianess = NetworkEndian>
serialize_return_t
serialize_optional(ds::chunk& c, const std::optional<T>& i, ToEndianess = network_endian)
{
    if (i.has_value())
        return serialize<T>(c, i.value(), ToEndianess{});
    else
        return 0;
}

template <typename T>
serialize_return_t mock_serialize_optional(const std::optional<T>& i)
{
    if (i.has_value())
        return mock_serialize<T>(i.value());
    else
        return 0;
}


///////////////////////////////////////////////////////////////////////////////////////////////
// Message serialization
template <typename ToEndianess = NetworkEndian>
serialize_return_t
serialize(ds::chunk& c,
          const rvn::depracated::messages::ClientSetupMessage& clientSetupMessage,
          ToEndianess = network_endian)
{
    std::uint64_t msgLen = 0;
    // we need to find out length of the message we would be serializing
    {
        msgLen +=
        mock_serialize<ds::quic_var_int>(clientSetupMessage.supportedVersions_.size());
        for (const auto& version : clientSetupMessage.supportedVersions_)
            msgLen += mock_serialize<ds::quic_var_int>(version);

        msgLen +=
        mock_serialize<ds::quic_var_int>(clientSetupMessage.parameters_.size());
        for (const auto& parameter : clientSetupMessage.parameters_)
        {
            msgLen += mock_serialize<ds::quic_var_int>(
            static_cast<std::uint32_t>(parameter.parameterType_));
            msgLen += mock_serialize<ds::quic_var_int>(parameter.parameterValue_.size());
            // c.append(parameter.parameterValue_.data(), parameter.parameterValue_.size());
            msgLen += parameter.parameterValue_.size();
        }
    }

    std::uint64_t headerLen = 0;

    // Header
    headerLen +=
    serialize<ds::quic_var_int>(c, utils::to_underlying(depracated::messages::MoQtMessageType::CLIENT_SETUP),
                                ToEndianess{});
    headerLen += serialize<ds::quic_var_int>(c, msgLen, ToEndianess{});

    // Body
    serialize<ds::quic_var_int>(c, clientSetupMessage.supportedVersions_.size(),
                                ToEndianess{});
    for (const auto& version : clientSetupMessage.supportedVersions_)
        serialize<ds::quic_var_int>(c, version, ToEndianess{});

    serialize<ds::quic_var_int>(c, clientSetupMessage.parameters_.size(), ToEndianess{});
    for (const auto& parameter : clientSetupMessage.parameters_)
    {

        serialize<ds::quic_var_int>(c, utils::to_underlying(parameter.parameterType_),
                                    ToEndianess{});
        serialize<ds::quic_var_int>(c, parameter.parameterValue_.size(), ToEndianess{});
        c.append(parameter.parameterValue_.data(), parameter.parameterValue_.size());
    }

    return headerLen + msgLen;
}
///////////////////////////////////////////////////////////////////////////////////////////////
} // namespace rvn::serialization::detail