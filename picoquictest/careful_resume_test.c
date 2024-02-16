#include "picoquic_internal.h"
#include "picoquic_utils.h"
#include "tls_api.h"
#include "picoquictest_internal.h"
#ifdef _WINDOWS
#include "wincompat.h"
#endif
#include <picotls.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "picoquic_binlog.h"
#include "csv.h"
#include "qlog.h"
#include "autoqlog.h"
#include "picoquic_logger.h"
#include "performance_log.h"
#include "picoquictest.h"

static int careful_resume_test_one(picoquic_congestion_algorithm_t* ccalgo, size_t data_size, uint64_t max_completion_time,
    uint64_t mbps_up, uint64_t mbps_down, uint64_t jitter, int has_loss, int bdp_option)
{
    int ret = 0;
    uint64_t loss_mask = has_loss ? 0x10000000 : 0;
    uint64_t simulated_time = 0;

    picoquic_test_tls_api_ctx_t* test_ctx = NULL;
    char const* ticket_seed_store = "careful_resume_ticket_store.bin";

    /* Create cid */
    picoquic_connection_id_t initial_cid = { {0xcc, 0xaa, 0, 0, 0, 0, 0, 0}, 8 };
    initial_cid.id[2] = ccalgo->congestion_algorithm_number;
    initial_cid.id[3] = (mbps_up > 0xff) ? 0xff : (uint8_t)mbps_up;
    initial_cid.id[4] = (mbps_down > 0xff) ? 0xff : (uint8_t)mbps_down;
    initial_cid.id[5] = (300000ull > 2550000) ? 0xff : (uint8_t)(300000ull / 10000);
    initial_cid.id[6] = (jitter > 255000) ? 0xff : (uint8_t)(jitter / 1000);
    initial_cid.id[7] = (has_loss) ? 0x30 : 0x00;
    if (has_loss) {
        initial_cid.id[7] |= 0x20;

    }

    test_api_stream_desc_t test_scenario_careful_resume[] = {
        { 4, 0, 0, data_size }
    };

    /* Initialize an empty ticket store */
    ret = picoquic_save_tickets(NULL, simulated_time, ticket_seed_store);

    /* Initialize transport parameters */
    picoquic_tp_t client_parameters;
    picoquic_tp_t server_parameters;
    memset(&client_parameters, 0, sizeof(picoquic_tp_t));
    picoquic_init_transport_parameters(&client_parameters, 1);
    client_parameters.enable_time_stamp = 3;
    memset(&server_parameters, 0, sizeof(picoquic_tp_t));
    picoquic_init_transport_parameters(&server_parameters, 0);
    server_parameters.enable_time_stamp = 3;

    /* For the flow control parameters to a small value */
    // TODO switch bdps?
    uint64_t bdp_s = (mbps_up * 300000ull * 2) / 8 * 2;
    uint64_t bdp_c = (mbps_down * 300000ull * 2) / 8 * 2;

    bdp_s += bdp_s;
    bdp_c += bdp_c;

    // TODO clean up
    server_parameters.initial_max_data = bdp_s;
    server_parameters.initial_max_stream_data_bidi_local = bdp_s;
    server_parameters.initial_max_stream_data_bidi_remote = bdp_s;
    server_parameters.initial_max_stream_data_uni = bdp_s;
    client_parameters.initial_max_data = bdp_c;
    client_parameters.initial_max_stream_data_bidi_local = bdp_c;
    client_parameters.initial_max_stream_data_bidi_remote = bdp_c;
    client_parameters.initial_max_stream_data_uni = bdp_c;


    /* Prepare a first connection */
    if (ret == 0)
    {
        /* Init test context and set delayed client start */
        /* ret = tls_api_init_ctx(&test_ctx, PICOQUIC_INTERNAL_TEST_VERSION_1,
                               PICOQUIC_TEST_SNI, PICOQUIC_TEST_ALPN, &simulated_time, ticket_seed_store, NULL, 0, 1, 0); */
        ret = tls_api_init_ctx_ex2(&test_ctx, PICOQUIC_INTERNAL_TEST_VERSION_1, PICOQUIC_TEST_SNI, PICOQUIC_TEST_ALPN, &simulated_time,
                                    ticket_seed_store, NULL, 0, 1, 0, &initial_cid, 8, 0, 0, 0);

        /* Set transport parameters */
        if (ret == 0) {
            picoquic_set_transport_parameters(test_ctx->cnx_client, &client_parameters);
        }

        if (ret == 0) {
            ret = picoquic_set_default_tp(test_ctx->qserver, &server_parameters);
            //if (server_parameters.prefered_address.ipv4Port != 0 ||
            //    server_parameters.prefered_address.ipv6Port != 0) {
            //    /* If testing server migration, disable address check */
            //    test_ctx->server_use_multiple_addresses = 1;
            //}
        }

        /* Enable qlog */
        picoquic_set_qlog(test_ctx->qclient, ".");
        picoquic_set_qlog(test_ctx->qserver, ".");

        /* Set cc algo */
        picoquic_set_default_congestion_algorithm(test_ctx->qserver, ccalgo);
        picoquic_set_congestion_algorithm(test_ctx->cnx_client, ccalgo);

        /* Set BDP frame option */
        //picoquic_set_default_bdp_frame_option(test_ctx->qclient, bdp_option);
        //picoquic_set_default_bdp_frame_option(test_ctx->qserver, bdp_option);

        picoquic_cnx_set_pmtud_required(test_ctx->cnx_client, 1); // TODO

        /* Configure links between client and server */
        test_ctx->c_to_s_link->jitter = jitter;
        test_ctx->c_to_s_link->microsec_latency = 300000ull;
        test_ctx->c_to_s_link->picosec_per_byte = (1000000ull * 8) / mbps_up;
        test_ctx->s_to_c_link->jitter = 0;
        test_ctx->s_to_c_link->microsec_latency = 300000ull;
        test_ctx->s_to_c_link->picosec_per_byte = (1000000ull * 8) / mbps_down;
        //test_ctx->stream0_flow_release = 1;
        test_ctx->immediate_exit = 1;
    }

    /* Start first connection */
    if (ret == 0)
    {
        /* Start client */
        ret = picoquic_start_client_cnx(test_ctx->cnx_client);

        /* Connect to server */
        if (ret == 0) {
            ret = tls_api_connection_loop(test_ctx, &loss_mask, 0, &simulated_time);
        }

        /* Prepare to send data */
        if (ret == 0) {
            ret = test_api_init_send_recv_scenario(test_ctx, test_scenario_careful_resume, sizeof(test_scenario_careful_resume));
        }

        /* Perform a data sending loop */
        if (ret == 0) {
            ret = tls_api_data_sending_loop(test_ctx, &loss_mask, &simulated_time, 0);
        }

        /* Verify scenario */
        if (ret == 0) {
            ret = tls_api_one_scenario_body_verify(test_ctx, &simulated_time, max_completion_time);
        }

        printf("bytes sent: %" PRIu64 ", max datarate: %" PRIu64 "\n", test_ctx->cnx_server->path[0]->bytes_sent, test_ctx->cnx_server->path[0]->bandwidth_estimate);
    }

    /* Check the ticket store at the client. */
    /*if (ret == 0) {
        picoquic_stored_ticket_t* client_ticket;
        client_ticket = picoquic_get_stored_ticket(test_ctx->qclient,
                                                   PICOQUIC_TEST_SNI, (uint16_t)strlen(PICOQUIC_TEST_SNI),
                                                   PICOQUIC_TEST_ALPN, (uint16_t)strlen(PICOQUIC_TEST_ALPN),
                                                   0, 0, test_ctx->cnx_client->issued_ticket_id);

        if (client_ticket == NULL) {
            DBG_PRINTF("%s", "No ticket found for client.");
            ret = -1;
        }
        else {
            client_ticket_id = test_ctx->cnx_client->issued_ticket_id;

            if (client_ticket->tp_0rtt[picoquic_tp_0rtt_rtt_local] == 0) {
                DBG_PRINTF("%s", "RTT not set for client ticket.");
                ret = -1;
            }
            if (client_ticket->tp_0rtt[picoquic_tp_0rtt_cwin_local] == 0) {
                DBG_PRINTF("%s", "CWIN not set for client ticket.");
                ret = -1;
            }
        }

        printf("Stored client ticket={rtt=%" PRIu64 ", cwin=%" PRIu64 "}\n", client_ticket->tp_0rtt[picoquic_tp_0rtt_rtt_local], client_ticket->tp_0rtt[picoquic_tp_0rtt_cwin_local]);
    }*/

    /* Check the issued tickets list at the server. */
    /*if (ret == 0) {
        picoquic_issued_ticket_t* server_ticket;

        if (test_ctx->cnx_server == NULL) {
            server_ticket = test_ctx->qserver->table_issued_tickets_first;
        }
        else {
            server_ticket = picoquic_retrieve_issued_ticket(test_ctx->qserver,
                                                            test_ctx->cnx_server->issued_ticket_id);
        }
        if (server_ticket == NULL) {
            DBG_PRINTF("%s", "No ticket found for server.");
            ret = -1;
        }
        else {
            server_ticket_id = server_ticket->ticket_id;

            if (server_ticket->rtt == 0) {
                DBG_PRINTF("%s", "RTT not set for server ticket.");
                ret = -1;
            }
            if (server_ticket->cwin == 0) {
                DBG_PRINTF("%s", "CWIN not set for server ticket.");
                ret = -1;
            }
        }

        printf("Stored server ticket={rtt=%" PRIu64 ", cwin=%" PRIu64 "}\n", server_ticket->rtt, server_ticket->cwin);
    }*/

    /* Now we remove the client connection and create a new one. */
    if (ret == 0) {
        picoquic_delete_cnx(test_ctx->cnx_client);
        if (test_ctx->cnx_server != NULL) {
            picoquic_delete_cnx(test_ctx->cnx_server);
            test_ctx->cnx_server = NULL;
        }

        /* Clean the data allocated to test the streams */
        test_api_delete_test_streams(test_ctx);

        printf("---------------- 0-RTT connection ----------------\n");

        /* Enable careful resume */
        test_ctx->qserver->careful_resume_enabled = 1;
        test_ctx->qclient->careful_resume_enabled = 1;

        initial_cid.id[1] = 0xbb;

        printf("current_time: %" PRIu64"\n", simulated_time);

        test_ctx->cnx_client = picoquic_create_cnx(test_ctx->qclient,
            initial_cid, picoquic_null_connection_id,
            (struct sockaddr*) & test_ctx->server_addr, simulated_time,
            PICOQUIC_INTERNAL_TEST_VERSION_1, PICOQUIC_TEST_SNI, PICOQUIC_TEST_ALPN, 1);

        if (test_ctx->cnx_client == NULL) {
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* Set transport parameters */
        if (ret == 0) {
            picoquic_set_transport_parameters(test_ctx->cnx_client, &client_parameters);
        }

        picoquic_set_congestion_algorithm(test_ctx->cnx_client, ccalgo);

        /* Set BDP frame option */
        //picoquic_set_default_bdp_frame_option(test_ctx->qserver, bdp_option);

        picoquic_cnx_set_pmtud_required(test_ctx->cnx_client, 1); // TODO

        /* Start client */
        ret = picoquic_start_client_cnx(test_ctx->cnx_client);

        /* Connect to server */
        if (ret == 0) {
            ret = tls_api_connection_loop(test_ctx, &loss_mask, 0, &simulated_time);
        }

        /* Prepare to send second batch of data */
        if (ret == 0) {
            ret = test_api_init_send_recv_scenario(test_ctx, test_scenario_careful_resume, sizeof(test_scenario_careful_resume));
        }

        /* Perform a data sending loop */
        if (ret == 0) {
            ret = tls_api_data_sending_loop(test_ctx, &loss_mask, &simulated_time, 0);
        }

        /* Verify scenario */
        if (ret == 0) {
            ret = tls_api_one_scenario_body_verify(test_ctx, &simulated_time, max_completion_time);
        }
    }

    /* verify that the client resume ticket id is the same as the previous one */
    /*if (ret == 0) {
        if (test_ctx->cnx_client->resumed_ticket_id != client_ticket_id) {
            DBG_PRINTF("Client ticket id = 0x%" PRIx64 ", expected 0x%" PRIx64,
                test_ctx->cnx_client->resumed_ticket_id, client_ticket_id);
            ret = -1;
        }
        if (test_ctx->cnx_client->seed_rtt_min == 0) {
            DBG_PRINTF("%s", "RTT not set for client ticket.");
            ret = -1;
        }
        if (test_ctx->cnx_client->seed_cwin == 0) {
            DBG_PRINTF("%s", "CWIN not set for client ticket.");
            ret = -1;
        }

        printf("Restored client ticket={rtt=%" PRIu64 ", cwin=%" PRIu64 "}\n", test_ctx->cnx_client->seed_rtt_min, test_ctx->cnx_client->seed_cwin);
    }*/

    /* verify that the server resume ticket id is the same as the previous one */
    /*if (ret == 0) {
        if (test_ctx->cnx_server != NULL && test_ctx->cnx_server->resumed_ticket_id != server_ticket_id) {
            DBG_PRINTF("Server ticket id = 0x%" PRIx64 ", expected 0x%" PRIx64,
                test_ctx->cnx_server->resumed_ticket_id, server_ticket_id);
            ret = -1;
        }
        if (test_ctx->cnx_client->seed_rtt_min == 0) {
            DBG_PRINTF("%s", "RTT not set for server ticket.");
            ret = -1;
        }
        if (test_ctx->cnx_client->seed_cwin == 0) {
            DBG_PRINTF("%s", "CWIN not set for server ticket.");
            ret = -1;
        }
    }*/

    printf("bytes sent: %" PRIu64 ", max datarate: %" PRIu64 "\n", test_ctx->cnx_server->path[0]->bytes_sent, test_ctx->cnx_server->path[0]->bandwidth_estimate);

    /* Clean up test context */
    if (test_ctx != NULL) {
        tls_api_delete_ctx(test_ctx);
        test_ctx = NULL;
    }

    return ret;
}

int careful_resume_test()
{
    // data_size = 100M
    return careful_resume_test_one(picoquic_cubic_algorithm, 25000000, 300000000, 6, 50, 0, 0, 1);
}

int careful_resume_loss_test()
{
    // data_size = 100M
    return careful_resume_test_one(picoquic_cubic_algorithm, 50000000, 300000000, 6, 50, 0, 1, 1);
}
