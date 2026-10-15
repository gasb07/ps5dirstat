/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * The panel on the host, to try the pages in a desktop browser: build/test/devserver <dir> */
#include <signal.h>
#include <stdio.h>

#include "state.h"
#include "web.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <dir>\n", argv[0]);
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    ds_config_t cfg = {0};
    cfg.root = argv[1];
    ds_init(&cfg);
    int s = web_listen();
    if (s < 0) {
        perror("listen");
        return 1;
    }
    printf("http://127.0.0.1:%d/\n", WEB_PORT);
    for (;;)
        web_serve(s);
}
