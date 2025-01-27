#pragma once


// TODO: automate this instead of manual includes
#include <message_type.pb.h>
#include <object_messages.pb.h>
#include <setup_messages.pb.h>
#include <subscribe_messages.pb.h>


namespace rvn::messages
{

// (b) type which is encoded as a variable-length integer followed by that many
// bytes of data.
using BinaryBufferData = std::string;
using iType = std::uint64_t; // variable size integer, check QUIC RFC
using MOQTVersionT = iType;

enum class MoQtMessageType
{
    OBJECT_STREAM = 0x0,
    OBJECT_DATAGRAM = 0x1,
    SUBSCRIBE_UPDATE = 0x2,
    SUBSCRIBE = 0x3,
    SUBSCRIBE_OK = 0x4,
    SUBSCRIBE_ERROR = 0x5,
    ANNOUNCE = 0x6,
    ANNOUNCE_OK = 0x7,
    ANNOUNCE_ERROR = 0x8,
    UNANNOUNCE = 0x9,
    UNSUBSCRIBE = 0xA,
    SUBSCRIBE_DONE = 0xB,
    ANNOUNCE_CANCEL = 0xC,
    TRACK_STATUS_REQUEST = 0xD,
    TRACK_STATUS = 0xE,
    GOAWAY = 0x10,
    CLIENT_SETUP = 0x40,
    SERVER_SETUP = 0x41,
    STREAM_HEADER_TRACK = 0x50,
    STREAM_HEADER_GROUP = 0x51
};

using namespace protobuf_messages;

}; // namespace rvn::messages