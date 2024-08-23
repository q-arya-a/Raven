#include <msquic.h>

#include <cstring>
#include <iostream>
#include <memory>
#include <moqt.hpp>
#include <contexts.hpp>

using namespace rvn;

// Data Stream Open Flags = QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL
// Data Stream Start flags = QUIC_STREAM_START_FLAG_FAIL_BLOCKED |
// QUIC_STREAM_START_FLAG_SHUTDOWN_ON_FAIL
auto DataStreamCallBack = [](HQUIC Stream, void *Context, QUIC_STREAM_EVENT *Event) {
    const QUIC_API_TABLE *MsQuic = static_cast<StreamContext *>(Context)->moqtObject->get_tbl();
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        free(Event->SEND_COMPLETE.ClientContext);
        break;
    case QUIC_STREAM_EVENT_RECEIVE:
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        MsQuic->StreamClose(Stream);
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
};

// Control Stream Open Flags = QUIC_STREAM_OPEN_FLAG_NONE | QUIC_STREAM_OPEN_FLAG_0_RTT
// Control Stream Start flags = QUIC_STREAM_START_FLAG_PRIORITY_WORK
auto ControlStreamCallback = [](HQUIC ControlStream, void *Context, QUIC_STREAM_EVENT *Event) {
    StreamContext *streamContext = static_cast<StreamContext *>(Context);
    HQUIC connection = streamContext->connection;
    MOQT *moqtObject = streamContext->moqtObject;
    const QUIC_API_TABLE *MsQuic = streamContext->moqtObject->get_tbl();

    switch (Event->Type) {
    case QUIC_STREAM_EVENT_START_COMPLETE: {
        // called when attempting to setup stream connection
        LOGE("Server should never start BiDirectionalControlStream");
        break;
    }
    case QUIC_STREAM_EVENT_RECEIVE: {
        // Received Control Message
        // auto receiveInformation = Event->RECEIVE;
        ConnectionState &connectionState = moqtObject->connectionStateMap[connection];
        moqtObject->interpret_control_message(connectionState, &(Event->RECEIVE));
        break;
    }
    case QUIC_STREAM_EVENT_SEND_COMPLETE: {
        // Buffer has been sent
        // auto [quicBuffers, numBuffers] =
        // get_sent_buffers(Event->SEND_COMPLETE.ClientContext);
        // destroy_buffers(quicBuffers, numBuffers);
        break;
    }
    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN: {
        // set in QuicStreamReceiveComplete
        // We have delivered all the payload that needs to be delivered. Deliver
        // the graceful close event now.
        // tries to shutdown stream and ends up calling
        // `QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE`
        break;
    }
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        //
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:

        break;
    case QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE:
        // stream send componenent has been succesfully shut down
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        // stream has been shutdown
        break;
    case QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE:
        // uint64_t idealBufferSize = Event->IDEAL_SEND_BUFFER_SIZE.ByteCount;
        break;
    case QUIC_STREAM_EVENT_PEER_ACCEPTED:

        break;
    case QUIC_STREAM_EVENT_CANCEL_ON_LOSS:

        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
};

auto ServerConnectionCallback = [](HQUIC Connection, void *Context, QUIC_CONNECTION_EVENT *Event) {
    const QUIC_API_TABLE *MsQuic = static_cast<MOQT *>(Context)->get_tbl();

    StreamContext *streamContext = NULL;

    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        MsQuic->ConnectionSendResumptionTicket(Connection, QUIC_SEND_RESUMPTION_FLAG_NONE, 0, NULL);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        if (Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status == QUIC_STATUS_CONNECTION_IDLE) {
        } else {
        }
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        MsQuic->ConnectionClose(Connection);
        break;
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        /*
           Should receive bidirectional stream from user
           and then start transport of media
        */

        streamContext = new StreamContext(static_cast<MOQT *>(Context), Connection);

        MsQuic->SetCallbackHandler(Event->PEER_STREAM_STARTED.Stream,
                                   (void *)MOQT::control_stream_cb_wrapper, streamContext);
        break;
    case QUIC_CONNECTION_EVENT_RESUMED:
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
};

QUIC_STATUS
ServerListenerCallback(HQUIC, void *Context, QUIC_LISTENER_EVENT *Event) {
    const QUIC_API_TABLE *MsQuic = static_cast<MOQT *>(Context)->get_tbl();

    auto moqtObject = static_cast<MOQT *>(Context);

    QUIC_STATUS Status = QUIC_STATUS_NOT_SUPPORTED;
    switch (Event->Type) {
    case QUIC_LISTENER_EVENT_NEW_CONNECTION:
        MsQuic->SetCallbackHandler(Event->NEW_CONNECTION.Connection,
                                   (void *)(moqtObject->connection_cb_wrapper), Context);
        Status = MsQuic->ConnectionSetConfiguration(Event->NEW_CONNECTION.Connection,
                                                    moqtObject->configuration.get());
        break;
    default:
        break;
    }
    return Status;
}

typedef struct QUIC_CREDENTIAL_CONFIG_HELPER {
    QUIC_CREDENTIAL_CONFIG CredConfig;
    union {
        QUIC_CERTIFICATE_HASH CertHash;
        QUIC_CERTIFICATE_HASH_STORE CertHashStore;
        QUIC_CERTIFICATE_FILE CertFile;
        QUIC_CERTIFICATE_FILE_PROTECTED CertFileProtected;
    };
} QUIC_CREDENTIAL_CONFIG_HELPER;

QUIC_CREDENTIAL_CONFIG *get_cred_config() {
    const char *CertFile = "/home/hhn/cs/raven/server.cert";
    const char *KeyFile = "/home/hhn/cs/raven/server.key";
    QUIC_CREDENTIAL_CONFIG_HELPER *Config =
        (QUIC_CREDENTIAL_CONFIG_HELPER *)malloc(sizeof(QUIC_CREDENTIAL_CONFIG_HELPER));

    memset(Config, 0, sizeof(QUIC_CREDENTIAL_CONFIG_HELPER));

    Config->CredConfig.Flags = QUIC_CREDENTIAL_FLAG_NONE;
    Config->CertFile.CertificateFile = (char *)CertFile;
    Config->CertFile.PrivateKeyFile = (char *)KeyFile;
    Config->CredConfig.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
    Config->CredConfig.CertificateFile = &Config->CertFile;

    return &(Config->CredConfig);
}

int main() {
    std::unique_ptr<MOQTServer> moqtServer = std::make_unique<MOQTServer>();

    QUIC_REGISTRATION_CONFIG RegConfig = {"quicsample", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    moqtServer->set_regConfig(&RegConfig);

    moqtServer->set_listenerCb(ServerListenerCallback);
    moqtServer->set_connectionCb(ServerConnectionCallback);

    QUIC_BUFFER AlpnBuffer = {sizeof("sample") - 1, (uint8_t *)"sample"};
    moqtServer->set_AlpnBuffers(&AlpnBuffer);

    moqtServer->set_AlpnBufferCount(1);

    const uint64_t IdleTimeoutMs = 1000;
    QUIC_SETTINGS Settings;
    std::memset(&Settings, 0, sizeof(Settings));
    Settings.IdleTimeoutMs = IdleTimeoutMs;
    Settings.IsSet.IdleTimeoutMs = TRUE;
    Settings.ServerResumptionLevel = QUIC_SERVER_RESUME_AND_ZERORTT;
    Settings.IsSet.ServerResumptionLevel = TRUE;
    Settings.PeerBidiStreamCount = 1;
    Settings.IsSet.PeerBidiStreamCount = TRUE;

    moqtServer->set_Settings(&Settings, sizeof(Settings));
    moqtServer->set_CredConfig(get_cred_config());

    QUIC_ADDR Address;
    std::memset(&Address, 0, sizeof(Address));
    QuicAddrSetFamily(&Address, QUIC_ADDRESS_FAMILY_UNSPEC);
    const uint16_t UdpPort = 4567;
    QuicAddrSetPort(&Address, UdpPort);

    moqtServer->start_listener(&Address);

    {
        char c;
        while (1)
            std::cin >> c;
    }
}
