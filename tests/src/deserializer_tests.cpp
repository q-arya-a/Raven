#include "serialization/chunk.hpp"
#include "wrappers.hpp"
#include <deserializer.hpp>
#include <initializer_list>
#include <memory>
#include <serialization/messages.hpp>
#include <serialization/serialization_impl.hpp>

using namespace rvn;
using namespace rvn::serialization;

inline auto QuicBufferDeleter = [](QUIC_BUFFER* buffer)
{ free(buffer); };

using UniqueQuicBuffer = std::unique_ptr<QUIC_BUFFER, decltype(QuicBufferDeleter)>;
using SharedQuicBuffer = std::shared_ptr<QUIC_BUFFER>;

UniqueQuicBuffer construct_quic_buffer(std::uint64_t lenBuffer)
{
    QUIC_BUFFER* totalQuicBuffer =
    static_cast<QUIC_BUFFER*>(malloc(sizeof(QUIC_BUFFER) + lenBuffer));
    totalQuicBuffer->Length = lenBuffer;
    totalQuicBuffer->Buffer =
    reinterpret_cast<uint8_t*>(totalQuicBuffer) + sizeof(QUIC_BUFFER);

    return UniqueQuicBuffer(totalQuicBuffer);
}

std::vector<::SharedQuicBuffer>
generate_quic_buffers(std::initializer_list<ds::chunk> chunks)
{
    std::uint64_t totalSize = 0;
    for (const auto& chunk : chunks)
        totalSize += chunk.size();

    std::vector<::SharedQuicBuffer> quicBuffers;
    quicBuffers.reserve(totalSize / 2);

    // To stress it we serialize to small chunks
    const auto serialize_chunk_into_multiple_quic_buffers = [&quicBuffers](auto&& messageChunk)
    {
        for (size_t i = 0; i != messageChunk.size();)
        {
            std::uint64_t quicBufferSize = (i % 3) + 1; // serialized to 1 or 2 bytes
            quicBufferSize =
            std::min(quicBufferSize, static_cast<std::uint64_t>(messageChunk.size() - i));
            quicBuffers.emplace_back(construct_quic_buffer(quicBufferSize));
            memcpy(quicBuffers.back().get()->Buffer, messageChunk.data() + i, quicBufferSize);
            i += quicBufferSize;
        }
    };

    for (const auto& chunk : chunks)
        serialize_chunk_into_multiple_quic_buffers(chunk);

    return quicBuffers;
}

template <class... Ts> struct overloads : Ts...
{
    using Ts::operator()...;
};

void test1()
{
    // Client Setup Message
    ClientSetupMessage clientSetupMessage;
    clientSetupMessage.supportedVersions_ = { 1, 2, 3 };
    ds::chunk clientSetupMessageChunk;
    serialization::detail::serialize(clientSetupMessageChunk, clientSetupMessage);

    // Server Setup Message
    ServerSetupMessage serverSetupMessage;
    serverSetupMessage.selectedVersion_ = 1;
    ds::chunk serverSetupMessageChunk;
    serialization::detail::serialize(serverSetupMessageChunk, serverSetupMessage);

    auto quicBuffers =
    generate_quic_buffers({ clientSetupMessageChunk, serverSetupMessageChunk });

    const auto visitor =
    overloads{ [](...) { std::cout << "Unexpected Message\n"; }, [](const ClientSetupMessage&)
               { std::cout << "Received ClientSetupMessage\n"; },
               [](const ServerSetupMessage&)
               { std::cout << "Received ServerSetupMessage\n"; } };

    Deserializer deserializer(true, visitor);
    for (const auto& quicBuffer : quicBuffers)
        deserializer.append_buffer(quicBuffer);

    return;
}


int main()
{
    test1();
    return 0;
}
