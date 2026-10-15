/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Web panel server. */
#pragma once

#define WEB_PORT 8771
#ifndef WEB_SEND_TIMEOUT
#define WEB_SEND_TIMEOUT 5 /* seconds a whole reply may take: a client that stops reading is dropped */
#endif

int web_listen(void);
/* Serves one connection on the listening socket. */
void web_serve(int listen_fd);
/* Answers the request on a connected socket and closes it. */
void web_handle(int fd);
