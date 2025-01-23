#include "utilities.hpp"
#include <serialization/chunk.hpp>
#include <serialization/deserialization_impl.hpp>
#include <serialization/messages.hpp>
#include <serialization/serialization_impl.hpp>

using namespace rvn;
using namespace rvn::serialization;

/*
    We represent serialized data in bits (rather than hex) as it is easier to see the structure of the data.
    We want to convert the string of 0s and 1s to a vector of bytes (bit representation).
    Unline the hex representation ("\x0a\x0b"), we can not simply have a binary string
    Hence we use this helpter function

    The function ignores all charecters apart from the the charecters '0' and '1'
*/
std::vector<uint8_t> binary_string_to_vector(const std::string& str)
{
    std::vector<uint8_t> vec;
    vec.reserve((str.size() + 8) / 8);

    std::uint8_t currByte = 0;
    std::uint8_t bitCount = 0;
    for (char c : str)
    {
        if (c == '0')
        {
            currByte = currByte << 1;
            bitCount++;
        }
        else if (c == '1')
        {
            currByte = (currByte << 1) | 1;
            bitCount++;
        }

        if (bitCount == 8)
        {
            vec.push_back(currByte);
            currByte = 0;
            bitCount = 0;
        }
    }

    utils::ASSERT_LOG_THROW(bitCount == 0, "Invalid binary string, not multiple of 8\n",
                            "Trailing bit count: ", bitCount, "\n",
                            "Binary String: \n", str);
    return vec;
}


void test1()
{
    depracated::messages::ClientSetupMessage msg;
    msg.supportedVersions_.push_back(0x12345678);
    msg.supportedVersions_.push_back(0x87654321);

    ds::chunk c;
    detail::serialize(c, msg);

    // clang-format off
    //  [ 01000000 01000000 ]      00001110          00000010            [ 10010010 00110100 01010110 01111000 ] [ 11000000 00000000 00000000 00000000 10000111 01100101 01000011 00100001 ]     00000000
    // ( quic_msg_type: 0x40 )  (msglen = 14)  (num supported versions)            (supported version 1)                              (supported version 2)                                   (num parameters)
    std::string expectedSerializationString = "[01000000 01000000] [00001110] [00000010] [10010010 00110100 01010110 01111000] [11000000 00000000 00000000 00000000 10000111 01100101 01000011 00100001] [00000000]";
    // clang-format on

    auto expectedSerialization = binary_string_to_vector(expectedSerializationString);
    utils::ASSERT_LOG_THROW(c.size() == expectedSerialization.size(), "Size mismatch\n",
                            "Expected size: ", expectedSerialization.size(),
                            "\n", "Actual size: ", c.size(), "\n");
    for (std::size_t i = 0; i < c.size(); i++)
        utils::ASSERT_LOG_THROW(c[i] == expectedSerialization[i], "Mismatch at index: ", i,
                                "\n", "Expected: ", expectedSerialization[i],
                                "\n", "Actual: ", c[i], "\n");
    ds::ChunkSpan span(c);


    depracated::messages::ControlMessageHeader header;
    detail::deserialize(header, span);

    utils::ASSERT_LOG_THROW(header.messageType_ ==
                            rvn::depracated::messages::MoQtMessageType::CLIENT_SETUP,
                            "Header type mismatch\n", "Expected: ",
                            utils::to_underlying(rvn::depracated::messages::MoQtMessageType::CLIENT_SETUP),
                            "\n", "Actual: ", utils::to_underlying(header.messageType_),
                            "\n");

    depracated::messages::ClientSetupMessage deserialized;
    detail::deserialize(deserialized, span);

    utils::ASSERT_LOG_THROW(msg == deserialized, "Deserialization failed", "\n",
                            "Expected: ", msg, "\n", "Actual: ", deserialized, "\n");
}

void tests()
{
    try
    {
        test1();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Test failed\n";
        std::cerr << e.what() << std::endl;
    }
}


int main()
{
    tests();
    return 0;
}