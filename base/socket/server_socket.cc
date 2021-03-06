// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright (c) 2020 The Base Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/socket/server_socket.h"

#include "base/socket/ip_address.h"
#include "base/socket/ip_endpoint.h"
#include "base/socket/socket_errors.h"

namespace base {

ServerSocket::ServerSocket() = default;

ServerSocket::~ServerSocket() = default;

int ServerSocket::ListenWithAddressAndPort(const std::string& address_string,
                                           uint16_t port, int backlog) {
  IPAddress ip_address;
  if (!ip_address.AssignFromIPLiteral(address_string)) {
    return ERR_ADDRESS_INVALID;
  }

  return Listen(IPEndPoint(ip_address, port), backlog);
}

}  // namespace base
