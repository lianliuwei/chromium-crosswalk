// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_SPDY_WEBSOCKET_TEST_UTIL_H_
#define NET_SPDY_SPDY_WEBSOCKET_TEST_UTIL_H_

#include "net/base/request_priority.h"
#include "net/spdy/spdy_protocol.h"
#include "net/spdy/spdy_test_util_common.h"

namespace net {

class SpdyWebSocketTestUtil : public SpdyTestUtil {
 public:
  explicit SpdyWebSocketTestUtil(NextProto protocol);

  // Constructs a standard SPDY SYN_STREAM frame for WebSocket over
  // SPDY opening handshake.
  SpdyFrame* ConstructSpdyWebSocketSynStream(int stream_id,
                                             const char* path,
                                             const char* host,
                                             const char* origin);

  // Constructs a standard SPDY SYN_REPLY packet to match the
  // WebSocket over SPDY opening handshake.
  SpdyFrame* ConstructSpdyWebSocketSynReply(int stream_id);

  // Constructs a WebSocket over SPDY handshake request packet.
  SpdyFrame* ConstructSpdyWebSocketHandshakeRequestFrame(
      const char* const headers[],
      int header_count,
      SpdyStreamId stream_id,
      RequestPriority request_priority);

  // Constructs a WebSocket over SPDY handshake response packet.
  SpdyFrame* ConstructSpdyWebSocketHandshakeResponseFrame(
      const char* const headers[],
      int header_count,
      SpdyStreamId stream_id,
      RequestPriority request_priority);

  // Constructs a SPDY HEADERS frame for a WebSocket frame over SPDY.
  SpdyFrame* ConstructSpdyWebSocketHeadersFrame(int stream_id,
                                                const char* length,
                                                bool fin);

  // Constructs a WebSocket over SPDY data packet.
  SpdyFrame* ConstructSpdyWebSocketDataFrame(const char* data,
                                             int len,
                                             SpdyStreamId stream_id,
                                             bool fin);

  const char* GetWebSocketPathKey() const;
  const char* GetOriginKey() const;
  const char* GetOpcodeKey() const;
  const char* GetLengthKey() const;
  const char* GetFinKey() const;
};

}  // namespace net

#endif  // NET_SPDY_SPDY_WEBSOCKET_TEST_UTIL_H_
