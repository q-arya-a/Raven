#pragma once
////////////////////////////////////////////
#include <msquic.h>
////////////////////////////////////////////
#include <cstdint>
#include <functional>
#include <sstream>
#include <unordered_map>
////////////////////////////////////////////
#include <contexts.hpp>
#include <protobuf_messages.hpp>
#include <serialization.hpp>
#include <utilities.hpp>
#include <wrappers.hpp>
////////////////////////////////////////////

#define DEFAULT_BUFFER_CAPACITY 1024

namespace rvn {

// context used when data is send on stream

class MOQT {

  public:
    using listener_cb_lamda_t = std::function<QUIC_STATUS(HQUIC, void *, QUIC_LISTENER_EVENT *)>;
    using connection_cb_lamda_t =
        std::function<QUIC_STATUS(HQUIC, void *, QUIC_CONNECTION_EVENT *)>;
    using stream_cb_lamda_t = std::function<QUIC_STATUS(HQUIC, void *, QUIC_STREAM_EVENT *)>;

    enum class SecondaryIndices {
        regConfig,
        listenerCb,
        connectionCb,
        AlpnBuffers,
        AlpnBufferCount,
        Settings,
        CredConfig,
        controlStreamCb,
        dataStreamCb
    };

    static constexpr std::uint64_t sec_index_to_val(SecondaryIndices idx) {
        auto intVal = rvn::utils::to_underlying(idx);

        return (1 << intVal);
    }

    std::uint32_t version;

    // PIMPL pattern for moqt related utilities
    MOQTUtilities *utils;

    // need to be able to get function pointor of this
    // function hence can not be member function

    rvn::unique_QUIC_API_TABLE tbl;
    rvn::unique_registration reg;
    rvn::unique_configuration configuration;

    // secondary variables => build into primary
    QUIC_REGISTRATION_CONFIG *regConfig;
    // Server will use listener and client will use connection
    listener_cb_lamda_t listener_cb_lamda;
    connection_cb_lamda_t connection_cb_lamda;
    stream_cb_lamda_t control_stream_cb_lamda;
    stream_cb_lamda_t data_stream_cb_lamda;
    QUIC_BUFFER *AlpnBuffers;
    uint32_t AlpnBufferCount;
    QUIC_SETTINGS *Settings;
    uint32_t SettingsSize; // set along with Settings
    QUIC_CREDENTIAL_CONFIG *CredConfig;

    std::uint64_t secondaryCounter;

    void add_to_secondary_counter(SecondaryIndices idx) {
        secondaryCounter |= sec_index_to_val(idx);
    }

    constexpr std::uint64_t full_sec_counter_value() {
        std::uint64_t value = 0;

        value |= sec_index_to_val(SecondaryIndices::regConfig);
        value |= sec_index_to_val(SecondaryIndices::listenerCb);
        value |= sec_index_to_val(SecondaryIndices::connectionCb);
        value |= sec_index_to_val(SecondaryIndices::AlpnBuffers);
        value |= sec_index_to_val(SecondaryIndices::AlpnBufferCount);
        value |= sec_index_to_val(SecondaryIndices::Settings);
        value |= sec_index_to_val(SecondaryIndices::CredConfig);
        value |= sec_index_to_val(SecondaryIndices::controlStreamCb);
        value |= sec_index_to_val(SecondaryIndices::dataStreamCb);

        return value;
    }

    // callback wrappers
    //////////////////////////////////////////////////////////////////////////
    static QUIC_STATUS listener_cb_wrapper(HQUIC reg, void *context, QUIC_LISTENER_EVENT *event) {
        MOQT *thisObject = static_cast<MOQT *>(context);
        return thisObject->listener_cb_lamda(reg, context, event);
    }

    static QUIC_STATUS connection_cb_wrapper(HQUIC reg, void *context,
                                             QUIC_CONNECTION_EVENT *event) {
        MOQT *thisObject = static_cast<MOQT *>(context);
        return thisObject->connection_cb_lamda(reg, context, event);
    }

    static QUIC_STATUS control_stream_cb_wrapper(HQUIC stream, void *context,
                                                 QUIC_STREAM_EVENT *event) {
        StreamContext *thisObject = static_cast<StreamContext *>(context);
        return thisObject->moqtObject->control_stream_cb_lamda(stream, context, event);
    }

    static QUIC_STATUS data_stream_cb_wrapper(HQUIC stream, void *context,
                                              QUIC_STREAM_EVENT *event) {
        StreamContext *thisObject = static_cast<StreamContext *>(context);
        return thisObject->moqtObject->data_stream_cb_lamda(stream, context, event);
    }

    // Setters
    //////////////////////////////////////////////////////////////////////////
    MOQT &set_regConfig(QUIC_REGISTRATION_CONFIG *regConfig_);
    MOQT &set_listenerCb(listener_cb_lamda_t listenerCb_);
    MOQT &set_connectionCb(connection_cb_lamda_t connectionCb_);

    // check  corectness here
    MOQT &set_AlpnBuffers(QUIC_BUFFER *AlpnBuffers_);

    MOQT &set_AlpnBufferCount(uint32_t AlpnBufferCount_);

    // sets settings and setting size
    MOQT &set_Settings(QUIC_SETTINGS *Settings_, uint32_t SettingsSize_);

    MOQT &set_CredConfig(QUIC_CREDENTIAL_CONFIG *CredConfig_);

    MOQT &set_controlStreamCb(stream_cb_lamda_t controlStreamCb_);
    MOQT &set_dataStreamCb(stream_cb_lamda_t dataStreamCb_);
    //////////////////////////////////////////////////////////////////////////

    const QUIC_API_TABLE *get_tbl();

    // map from connection HQUIC to connection state
    std::unordered_map<HQUIC, ConnectionState>
        connectionStateMap; // should have size 1 in case of client
    std::unordered_map<HQUIC, ConnectionState> &get_connectionStateMap() {
        return connectionStateMap;
    }

    template <typename... Args>
    QUIC_STATUS stream_send(StreamContext *streamContext, HQUIC stream, Args &&...args) {
        utils::LOG_EVENT(std::cout, args.DebugString()...);

        std::size_t requiredBufferSize = 0;

        // calculate required buffer size
        std::ostringstream oss;
        (google::protobuf::util::SerializeDelimitedToOstream(args, &oss), ...);

        std::string buffer = oss.str();
        void *sendBufferRaw = malloc(sizeof(QUIC_BUFFER) + buffer.size());
        utils::ASSERT_LOG_THROW(sendBufferRaw != nullptr, "Could not allocate memory for buffer");

        QUIC_BUFFER *sendBuffer = (QUIC_BUFFER *)sendBufferRaw;
        sendBuffer->Buffer = (uint8_t *)sendBufferRaw + sizeof(QUIC_BUFFER);
        sendBuffer->Length = buffer.size();

        std::memcpy(sendBuffer->Buffer, buffer.c_str(), buffer.size());

        QUIC_STATUS status =
            get_tbl()->StreamSend(stream, sendBuffer, 1, QUIC_SEND_FLAG_FIN, nullptr);

        return status;
    }

    /*
        // sending each object as an buffer, does not work because on the receive end they might be
       concatenated
        // MsQuic always receives <= 2 buffers
        template <typename... Args>
        QUIC_STATUS stream_send(StreamContext *streamContext, HQUIC stream, Args &&...args) {
            utils::LOG_EVENT(std::cout, args.DebugString()...);

            StreamSendContext *sendContext = new StreamSendContext(streamContext);

            // we need separate buffer for each argument, due to protobuf limitation
            sendContext->bufferCount = sizeof...(args);
            sendContext->buffers =
                static_cast<QUIC_BUFFER *>(malloc(sizeof(QUIC_BUFFER) * sendContext->bufferCount));

            std::size_t requiredBufferSize, i = 0;
            (..., (requiredBufferSize = args.ByteSizeLong(),
                   sendContext->buffers[i].Buffer = static_cast<uint8_t
                   *>(malloc(requiredBufferSize)), sendContext->buffers[i].Length =
                   args.ByteSizeLong(), args.SerializeToArray(sendContext->buffers[i].Buffer,
                   args.ByteSizeLong()), i++));

            QUIC_STATUS status =
                get_tbl()->StreamSend(stream, sendContext->buffers, sendContext->bufferCount,
                                      QUIC_SEND_FLAG_FIN, sendContext);

            return status;
        }
    */

    // auto is used as parameter because there is no named type for receive information
    // it is an anonymous structure
    /*
        type of recieve information is
        struct {
            uint64_t AbsoluteOffset;
            uint64_t TotalBufferLength;
            _Field_size_(BufferCount)
            const QUIC_BUFFER* Buffers;
            _Field_range_(0, UINT32_MAX)
            uint32_t BufferCount;
            QUIC_RECEIVE_FLAGS Flags;
        }
    */
    void interpret_control_message(ConnectionState &connectionState,
                                   const auto *receiveInformation) {
        utils::ASSERT_LOG_THROW(connectionState.controlStream.has_value(), "Trying to interpret control message without control stream");

        const QUIC_BUFFER *buffers = receiveInformation->Buffers;

        std::istringstream iStringStream(
            std::string(reinterpret_cast<const char *>(buffers[0].Buffer), buffers[0].Length));

        google::protobuf::io::IstreamInputStream istream(&iStringStream);

        protobuf_messages::ControlMessageHeader header =
            serialization::deserialize<protobuf_messages::ControlMessageHeader>(istream);

        /* TODO, split into client and server interpret functions helps reduce the number of
         * branches NOTE: This is the message received, which means that the client will interpret
         * the server's message and vice verse CLIENT_SETUP is received by server and SERVER_SETUP
         * is received by client
         */

        StreamContext *streamContext = connectionState.controlStream.value().streamContext.get();
        HQUIC stream = connectionState.controlStream.value().stream;
        switch (header.messagetype()) {
        case protobuf_messages::MoQtMessageType::CLIENT_SETUP: {
            // CLIENT sends to SERVER

            protobuf_messages::ClientSetupMessage clientSetupMessage =
                serialization::deserialize<protobuf_messages::ClientSetupMessage>(istream);

            auto &supportedversions = clientSetupMessage.supportedversions();
            auto matchingVersionIter =
                std::find(supportedversions.begin(), supportedversions.end(), version);

            if (matchingVersionIter == supportedversions.end()) {
                // TODO
                // destroy connection
                // connectionState.destroy_connection();
                return;
            }

            std::size_t iterIdx = std::distance(supportedversions.begin(), matchingVersionIter);
            auto &params = clientSetupMessage.parameters()[iterIdx];
            connectionState.path = std::move(params.path().path());
            connectionState.peerRole = params.role().role();

            utils::LOG_EVENT(std::cout,
                             "Client Setup Message received: \n", clientSetupMessage.DebugString());

            // send SERVER_SETUP message
            protobuf_messages::ControlMessageHeader serverSetupHeader;
            serverSetupHeader.set_messagetype(protobuf_messages::MoQtMessageType::SERVER_SETUP);

            protobuf_messages::ServerSetupMessage serverSetupMessage;
            serverSetupMessage.add_parameters()->mutable_role()->set_role(connectionState.peerRole);
            stream_send(streamContext, stream, serverSetupHeader, serverSetupMessage);

            break;
        }
        case protobuf_messages::MoQtMessageType::SERVER_SETUP: {
            // SERVER sends to CLIENT
            protobuf_messages::ServerSetupMessage serverSetupMessage =
                serialization::deserialize<protobuf_messages::ServerSetupMessage>(istream);

            utils::ASSERT_LOG_THROW(connectionState.path.size() == 0,
                                    "Server must now use the path parameter");
            utils::ASSERT_LOG_THROW(
                serverSetupMessage.parameters().size() > 0,
                "SERVER_SETUP sent no parameters, requires atleast role parameter");
            connectionState.peerRole = serverSetupMessage.parameters()[0].role().role();

            utils::LOG_EVENT(std::cout,
                             "Server Setup Message received: ", serverSetupMessage.DebugString());
            break;
        }
        default:
            LOGE("Unknown control message type", header.messagetype());
        }
    }

    void interpret_data_message(ConnectionState &connectionState, HQUIC dataStream,
                                const auto *receiveInformation) {
        if (!connectionState.controlStream.has_value())
            LOGE("Trying to interpret control message without control stream");
    }

  protected:
    MOQT();

  public:
    ~MOQT() { google::protobuf::ShutdownProtobufLibrary(); }
};

class MOQTServer : public MOQT {
    rvn::unique_listener listener;

  public:
    MOQTServer();

    void start_listener(QUIC_ADDR *LocalAddress);

    /*
        decltype(newConnectionInfo) is
        struct {
            const QUIC_NEW_CONNECTION_INFO* Info;
            HQUIC Connection;
        }
    */
    QUIC_STATUS register_new_connection(HQUIC listener, auto newConnectionInfo) {
        QUIC_STATUS status = QUIC_STATUS_NOT_SUPPORTED;
        HQUIC connection = newConnectionInfo.Connection;
        status = get_tbl()->ConnectionSetConfiguration(connection, configuration.get());

        if (QUIC_FAILED(status)) {
            return status;
        }

        get_tbl()->SetCallbackHandler(newConnectionInfo.Connection,
                                      (void *)(this->connection_cb_wrapper), (void *)(this));

        connectionStateMap[connection] = ConnectionState{connection};

        return status;
    }

    /*
        decltype(newStreamInfo) is
        struct {
            HQUIC Stream;
            QUIC_STREAM_OPEN_FLAGS Flags;
        }
    */
    QUIC_STATUS register_control_stream(HQUIC connection, auto newStreamInfo) {
        ConnectionState &connectionState = connectionStateMap.at(connection);
        utils::ASSERT_LOG_THROW(!connectionState.controlStream.has_value(),
                                "Control stream already registered by connection: ", connection);
        connectionState.controlStream = StreamState{newStreamInfo.Stream, DEFAULT_BUFFER_CAPACITY};

        StreamState &streamState = connectionState.controlStream.value();
        streamState.set_stream_context(std::make_unique<StreamContext>(this, connection));
        this->get_tbl()->SetCallbackHandler(newStreamInfo.Stream,
                                            (void *)MOQT::control_stream_cb_wrapper,
                                            (void *)streamState.streamContext.get());

        // registering stream can not fail
        return QUIC_STATUS_SUCCESS;
    }
};

class MOQTClient : public MOQT {
    rvn::unique_connection connection;

  public:
    MOQTClient();

    void start_connection(QUIC_ADDRESS_FAMILY Family, const char *ServerName, uint16_t ServerPort);

    protobuf_messages::ClientSetupMessage get_clientSetupMessage() {
        protobuf_messages::ClientSetupMessage clientSetupMessage;
        clientSetupMessage.set_numsupportedversions(1);
        clientSetupMessage.add_supportedversions(version);
        clientSetupMessage.add_numberofparameters(1);
        auto *param1 = clientSetupMessage.add_parameters();
        param1->mutable_path()->set_path("path");
        param1->mutable_role()->set_role(protobuf_messages::Role::Subscriber);

        return clientSetupMessage;
    }
};
} // namespace rvn
