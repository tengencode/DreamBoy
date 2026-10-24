/* rcheevos (third_party/rcheevos) builds and links into the PC tests (milestone 14). */
#include <string.h>

#include "rc_client.h"
#include "test.h"

static uint32_t no_memory(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *client) {
    (void)address;
    (void)buffer;
    (void)num_bytes;
    (void)client;
    return 0;
}

static void no_server(const rc_api_request_t *request, rc_client_server_callback_t callback, void *callback_data,
                      rc_client_t *client) {
    (void)request;
    (void)callback;
    (void)callback_data;
    (void)client;
}

TEST(rcheevos_client_is_created_with_hardcore_on_and_says_its_version) {
    rc_client_t *c = rc_client_create(no_memory, no_server);
    CHECK(c != NULL);
    CHECK_EQ(rc_client_get_hardcore_enabled(c), 1);
    char clause[64];
    rc_client_get_user_agent_clause(c, clause, sizeof clause);
    CHECK(strncmp(clause, "rcheevos/12.5", 13) == 0);
    rc_client_destroy(c);
}
