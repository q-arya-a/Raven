#include "msquic.h"
#include "utilities.hpp"
#include <contexts.hpp>
#include <moqt.hpp>
#include <stdexcept>

namespace rvn
{
StreamState& ConnectionState::create_data_stream()
{
    HQUIC connectionHandle = connection_.get();
    StreamContext* streamContext = new StreamContext(moqtObject, connectionHandle);

    auto stream =
    rvn::unique_stream(moqtObject->get_tbl(),
                       { connectionHandle, QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL,
                         moqtObject->data_stream_cb_wrapper, streamContext },
                       { QUIC_STREAM_START_FLAG_FAIL_BLOCKED |
                         QUIC_STREAM_START_FLAG_SHUTDOWN_ON_FAIL });

    dataStreams.emplace_back(std::move(stream));

    StreamState& streamState = dataStreams.back();
    streamState.set_stream_context(std::unique_ptr<StreamContext>(streamContext));

    return streamState;
}

void ConnectionState::send_data_buffer()
{
    if (dataBuffersToSend.empty())
    {
        auto iter = SubscriptionManagerHandle
        {
            } -> subscriptionStates[this].begin();
        if (iter == SubscriptionManagerHandle {} -> subscriptionStates[this].end())
            return;

        return SubscriptionManagerHandle
        {
            } -> update_subscription_state(this, iter);
    }

    QUIC_BUFFER* buffer = dataBuffersToSend.front();
    dataBuffersToSend.pop();

    StreamState& streamState = create_data_stream();
    HQUIC streamHandle = streamState.stream.get();

    StreamSendContext* streamSendContext =
    new StreamSendContext(buffer, 1, streamState.streamContext.get(),
                          [this, streamHandle](StreamSendContext* context)
                          {
                              HQUIC connectionHandle = context->streamContext->connection;
                              ConnectionState& connectionState =
                              *moqtObject->get_connectionState(connectionHandle);
                              connectionState.delete_data_stream(streamHandle);
                          });

    QUIC_STATUS status =
    moqtObject->get_tbl()->StreamSend(streamHandle, buffer, 1,
                                      QUIC_SEND_FLAG_FIN, streamSendContext);

    if (QUIC_FAILED(status))
        throw std::runtime_error("Failed to send data buffer");
}

void ConnectionState::delete_data_stream(HQUIC streamHandle)
{
    auto streamIter =
    std::find_if(dataStreams.begin(), dataStreams.end(),
                 [streamHandle](const StreamState& streamState)
                 { return streamState.stream.get() == streamHandle; });

    utils::ASSERT_LOG_THROW(streamIter != dataStreams.end(),
                            "Attempting to delete stream which does not "
                            "exist in busy stream queue");

    dataStreams.erase(streamIter);

    if (dataStreams.size() < MAX_DATA_STREAMS)
        send_data_buffer();
}

void ConnectionState::enqueue_data_buffer(QUIC_BUFFER* buffer)
{
    utils::LOG_EVENT(std::cout, "Enqueueing data buffer of size: ", buffer->Length);
    dataBuffersToSend.push(buffer);
    if (dataStreams.size() < MAX_DATA_STREAMS)
        send_data_buffer();
}

void ConnectionState::enqueue_control_buffer(QUIC_BUFFER* buffer)
{
    controlBuffersToSend.push(buffer);

    // TODO: protect: multiple control streams from being established
    send_control_buffer();
}

void ConnectionState::send_control_buffer()
{
    utils::ASSERT_LOG_THROW(dataBuffersToSend.empty(), __FUNCTION__,
                            "called with no buffers to send");

    auto buffer = controlBuffersToSend.front();
    controlBuffersToSend.pop();

    StreamState* streamState = &controlStream.value();
    HQUIC streamHandle = streamState->stream.get();

    StreamSendContext* streamSendContext =
    new StreamSendContext(buffer, 1, streamState->streamContext.get());

    QUIC_STATUS status =
    moqtObject->get_tbl()->StreamSend(streamHandle, buffer, 1,
                                      QUIC_SEND_FLAG_NONE, streamSendContext);
    if (QUIC_FAILED(status))
        throw std::runtime_error("Failed to send control message");
}


bool ConnectionState::check_subscription(const protobuf_messages::SubscribeMessage& subscribeMessage)
{
    // TODO: check if subscription data exists and if we are authenticated
    // to get it
    return true;
}

const std::vector<StreamState>& ConnectionState::get_data_streams() const
{
    return dataStreams;
}

std::vector<StreamState>& ConnectionState::get_data_streams()
{
    return dataStreams;
}

const std::optional<StreamState>& ConnectionState::get_control_stream() const
{
    return controlStream;
}

std::optional<StreamState>& ConnectionState::get_control_stream()
{
    return controlStream;
}


QUIC_STATUS ConnectionState::accept_data_stream(HQUIC streamHandle)
{
    auto& dataStreams = get_data_streams();

    // register new data stream into connectionState object
    dataStreams.emplace_back(rvn::unique_stream(moqtObject->get_tbl(), streamHandle));

    // set stream context for stream
    StreamState& streamState = dataStreams.back();
    streamState.set_stream_context(
    std::make_unique<StreamContext>(moqtObject, connection_.get()));

    // set callback handler fot the stream (MOQT sets internally)
    moqtObject->get_tbl()->SetCallbackHandler(streamHandle, (void*)MOQT::data_stream_cb_wrapper,
                                              (void*)streamState.streamContext.get());

    // registering stream can not fail
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS ConnectionState::accept_control_stream(HQUIC controlStreamHandle)
{
    StreamState controlStreamState{ rvn::unique_stream(moqtObject->get_tbl(), controlStreamHandle) };

    controlStreamState.set_stream_context(
    std::make_unique<StreamContext>(moqtObject, connection_.get()));

    this->controlStream = std::move(controlStreamState);

    moqtObject->get_tbl()
    ->SetCallbackHandler(controlStreamHandle, (void*)MOQT::control_stream_cb_wrapper,
                         (void*)controlStream.value().streamContext.get());


    return QUIC_STATUS_SUCCESS;
}

StreamState& ConnectionState::establish_control_stream()
{
    static int numTimesFunctionExecuted = 0;
    if (++numTimesFunctionExecuted != 1)
        utils::ASSERT_LOG_THROW(false, "establish control stream should be "
                                       "called only once in a connection");
    StreamContext* streamContext = new StreamContext(moqtObject, connection_.get());
    StreamState controlStreamState(
    rvn::unique_stream(moqtObject->get_tbl(),
                       { connection_.get(), QUIC_STREAM_OPEN_FLAG_0_RTT,
                         MOQT::control_stream_cb_wrapper, streamContext },
                       { QUIC_STREAM_START_FLAG_PRIORITY_WORK }));

    controlStreamState.set_stream_context(std::unique_ptr<StreamContext>(streamContext));

    this->controlStream = std::move(controlStreamState);

    return this->controlStream.value();
}

} // namespace rvn
