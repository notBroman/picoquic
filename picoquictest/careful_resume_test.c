//
// Created by Matthias Hofstätter on 06.03.24.
//

#include <stdlib.h>
#include <string.h>
#include "picoquictest_internal.h"
#include "picoquic.h"
#include "picoquic_internal.h"
#include "cc_common.h"
#include "autoqlog.h"


/** @name               cwnd_larger_than_jump
 *
 *  @brief              Checks if CR transitions to Normal phase when the congestion window (cwnd) is larger than the
 *                      jump window.
 *
 *  @assertation        cr_state transitions to CrState::Normal.
 *
 *  @phase              Reconnaissance
 *
 *  @prerequisites      • previous cwnd = 80,000 Bytes
 *                      • previous RTT = 50 ms
 *                      • cwnd = 45,000 Bytes
 *                      • the connection is not app limited
 *
 *  @results_analysis   When the pevious cwnd is 80,000 Bytes, the jump window becomes 40,000. As 45,000 < 40,000 the
 *                      jump never realizes and cr_state transitions to CrState::Normal.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT
 *                        of 50 ms
 *                      • Use the send_packet method from Resume to send a packet setting the cwnd to 45,000 Bytes;
 *                        ensure the rtt sample matches previous RTT and app_limited is false.
 */
int careful_resume_cwnd_larger_than_jump_test() {
    /* cnx */
    picoquic_cnx_t* cnx = (picoquic_cnx_t*)malloc(sizeof(picoquic_cnx_t));
    cnx->quic = (picoquic_quic_t*)malloc(sizeof(picoquic_quic_t));
    cnx->quic->use_careful_resume = 1;

    /* path */
    picoquic_path_t* path_x = (picoquic_path_t*)malloc(sizeof(picoquic_path_t));
    /* cwnd = 45,000 Bytes */
    path_x->cwin = 45000;
    /* the connection is not app limited */
    path_x->bytes_in_transit = path_x->cwin;

    /* careful resume */
    picoquic_cr_state_t* cr_state = (picoquic_cr_state_t*)malloc(sizeof(picoquic_cr_state_t));
    picoquic_cr_reset(cr_state, 0);
    /* Reconnaissance */
    cr_state->alg_state = picoquic_cr_alg_recon;
    /* previous cwnd = 80,000 Bytes */
    cr_state->saved_cwnd = 80000;
    /* previous RTT = 50 ms */
    cr_state->saved_rtt = 50;

    /* for printf TODO maybe remove this in production */
    cnx->client_mode = 0;
    cnx->start_time = 0;
    path_x->cnx = cnx;
    path_x->unique_path_id = 0;

    picoquic_per_ack_state_t ack_state = { 0 };
    ack_state.nb_bytes_acknowledged = 0; /* TODO */
    picoquic_cr_notify(cr_state, cnx, path_x, picoquic_congestion_notification_sent, &ack_state, 0);

    /* cr_state transitions to CrState::Normal. */
    if (cr_state->alg_state == picoquic_cr_alg_normal) {
        return 0;
    }

    return -1;
}

/** @name               rtt_less_than_half
 *
 *  @brief              Checks if CR transitions to Normal phase when the round-trip time (rtt_sample) is less than
 *                      half of the previous RTT.
 *
 *  @assertation        cr_state transitions to CrState::Normal.
 *
 *  @phase              Reconnaissance
 *
 *  @prerequisites      • previous cwnd = 80,000 Bytes
 *                      • previous RTT = 50 ms
 *                      • RTT sample = 10 ms
 *                      • the connection is not app limited
 *
 *  @results_analysis   The RTT sample of 10ms is smaller than half the previous RTT, or 50 ms/2. Therefore the jump
 *                      never realizes and cr_state transitions to CrState::Normal.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT
 *                        of 50 ms
 *                      • Use the send_packet method from Resume to send a packet setting the RTT sample to 10 ms;
 *                        ensure the cwnd is set smaller than 40,000 Bytes and app_limited is false.
 */
int careful_resume_rtt_less_than_half_test() {
    /* cnx */
    picoquic_cnx_t* cnx = (picoquic_cnx_t*)malloc(sizeof(picoquic_cnx_t));
    cnx->quic = (picoquic_quic_t*)malloc(sizeof(picoquic_quic_t));
    cnx->quic->use_careful_resume = 1;

    /* path */
    picoquic_path_t* path_x = (picoquic_path_t*)malloc(sizeof(picoquic_path_t));
    /* RTT sample = 10 ms */
    path_x->rtt_min = 10;
    /* the connection is not app limited */
    path_x->bytes_in_transit = path_x->cwin;

    /* careful resume */
    picoquic_cr_state_t* cr_state = (picoquic_cr_state_t*)malloc(sizeof(picoquic_cr_state_t));
    picoquic_cr_reset(cr_state, 0);
    /* Reconnaissance */
    cr_state->alg_state = picoquic_cr_alg_recon;
    /* previous cwnd = 80,000 Bytes */
    cr_state->saved_cwnd = 80000;
    /* previous RTT = 50 ms */
    cr_state->saved_rtt = 50;

    /* for printf TODO maybe remove this in production */
    cnx->client_mode = 0;
    cnx->start_time = 0;
    path_x->cnx = cnx;
    path_x->unique_path_id = 0;

    picoquic_per_ack_state_t ack_state = { 0 };
    ack_state.nb_bytes_acknowledged = 0; /* TODO */
    picoquic_cr_notify(cr_state, cnx, path_x, picoquic_congestion_notification_sent, &ack_state, 0);

    { /* cr_state transitions to CrState::Normal. */
    if (cr_state->alg_state == picoquic_cr_alg_normal)
        return 0;
    }

    return -1;
}

/** @name               rtt_greater_than_10
 *
 *  @brief              Checks if CR transitions to Normal phase when the RTT is greater than 10 times the previous RTT.
 *
 *  @assertation        cr_state transitions to CrState::Normal.
 *
 *  @phase              Reconnaissance
 *
 *  @prerequisites      • previous cwnd = 80,000 Bytes
 *                      • previous RTT = 50 ms
 *                      • RTT sample = 600 ms
 *                      • the connection is not app limited
 *
 *  @results_analysis   The sample RTT  of 600 ms is greater than 10 * previous RTT, or 10 * 50 = 500ms . Therefore the
 *                      jump never realizes and cr_state transitions to CrState::Normal.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT
 *                        of 50 ms
 *                      • Use the send_packet method from Resume to send a packet setting the RTT sample to 600 ms;
 *                        ensure the cwnd is set smaller than 40,000 Bytes and app_limited is false.
 */
int careful_resume_rtt_greater_than_10_test() {
    /* cnx */
    picoquic_cnx_t* cnx = (picoquic_cnx_t*)malloc(sizeof(picoquic_cnx_t));
    cnx->quic = (picoquic_quic_t*)malloc(sizeof(picoquic_quic_t));
    cnx->quic->use_careful_resume = 1;

    /* path */
    picoquic_path_t* path_x = (picoquic_path_t*)malloc(sizeof(picoquic_path_t));
    /* RTT sample = 600 ms */
    path_x->rtt_min = 600;
    /* the connection is not app limited */
    path_x->bytes_in_transit = path_x->cwin;

    path_x->cnx = cnx;

    /* careful resume */
    picoquic_cr_state_t* cr_state = (picoquic_cr_state_t*)malloc(sizeof(picoquic_cr_state_t));
    picoquic_cr_reset(cr_state, 0);
    /* Reconnaissance */
    cr_state->alg_state = picoquic_cr_alg_recon;
    /* previous cwnd = 80,000 Bytes */
    cr_state->saved_cwnd = 80000;
    /* previous RTT = 50 ms */
    cr_state->saved_rtt = 50;

    /* for printf TODO maybe remove this in production */
    cnx->client_mode = 0;
    cnx->start_time = 0;
    path_x->unique_path_id = 0;

    picoquic_per_ack_state_t ack_state = { 0 };
    ack_state.nb_bytes_acknowledged = 0; /* TODO */
    picoquic_cr_notify(cr_state, cnx, path_x, picoquic_congestion_notification_sent, &ack_state, 0);

    /* cr_state transitions to CrState::Normal. */
    if (cr_state->alg_state == picoquic_cr_alg_normal) {
        return 0;
    }

    return -1;
}

/** @name               valid_rtt
 *
 *  @brief              Tests the transition to Unvalidated phase with valid RTT conditions.
 *
 *  @assertation        • cr_state transitions to CrState::Unvalidated with the correct cr_mark value (20).
 *                      • the jump window is previous cwnd/2 - current cwnd = 19,500 Bytes
 *                      • pipesize is initialised to the current flightsize = 20,500 Bytes
 *
 *  @phase              Reconnaissance
 *
 *  @prerequisites      • previous cwnd = 80,000 Bytes
 *                      • previous RTT = 50 ms
 *                      • RTT sample = 60 ms
 *                      • cwnd = 20,500 Bytes
 *                      • the connection is not app limited
 *
 *  @results_analysis   The RTT sample of 60 ms is neither smaller than half the previous RTT (50ms/2), nor greater
 *                      than 10 * previous RTT = 10*50 = 500ms. As the connection is not app limited and the jump
 *                      window of 80,000/2 is greater than the cwnd of 20,500, all conditions are met and cr_state
 *                      transitions to CrState::Unvalidated.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT
 *                        of 50 ms
 *                      • Use the send_packet method from Resume to send a packet setting the RTT sample to 60 ms, the
 *                        cwnd to 20,500 Bytes, largest packet number sent to 20, and the app_limited parameter to
 *                        false.
 */
int careful_resume_valid_rtt_test() {
    /* cnx */
    picoquic_cnx_t* cnx = (picoquic_cnx_t*)malloc(sizeof(picoquic_cnx_t));
    cnx->quic = (picoquic_quic_t*)malloc(sizeof(picoquic_quic_t));
    cnx->quic->use_careful_resume = 1;

    /* path */
    picoquic_path_t* path_x = (picoquic_path_t*)malloc(sizeof(picoquic_path_t));
    path_x->cwin = 20500; /* cwnd = 45,000 Bytes */
    path_x->rtt_min = 60; /* RTT sample = 600 ms */
    /* the connection is not app limited */
    /* 20 packets in flight */
    path_x->bytes_in_transit = 20500;

    path_x->cnx = cnx;

    /* careful resume */
    picoquic_cr_state_t* cr_state = (picoquic_cr_state_t*)malloc(sizeof(picoquic_cr_state_t));
    picoquic_cr_reset(cr_state, 0);
    cr_state->alg_state = picoquic_cr_alg_recon; /* Reconnaissance */
    cr_state->saved_cwnd = 80000; /* previous cwnd = 80,000 Bytes */
    cr_state->saved_rtt = 50; /* previous RTT = 50 ms */

    /* for printf TODO maybe remove this in production */
    cnx->client_mode = 0;
    cnx->start_time = 0;
    path_x->unique_path_id = 0;

    picoquic_per_ack_state_t ack_state = { 0 };
    ack_state.nb_bytes_acknowledged = 0; /* TODO */
    picoquic_cr_notify(cr_state, cnx, path_x, picoquic_congestion_notification_sent, &ack_state, 0);

    /* cr_state transitions to CrState::Unvalidated with the correct cr_mark value (20). */
    /* the jump window is previous cwnd/2 - current cwnd = 19,500 Bytes */
    /* pipesize is initialised to the current flightsize = 20,500 Bytes */
    if (cr_state->alg_state == picoquic_cr_alg_unval && cr_state->unval_mark == 20500 && (cr_state->val_mark - cr_state->unval_mark) == 19500 && cr_state->pipesize == 20500) {
        return 0;
    }

    return -1;
}

/** @name               packet_loss_recon
 *
 *  @brief        Checks if CR transitions to Normal phase when a packet loss is encountered in the
 *                      Reconnaissance phase.
 *
 *  @assertation        cr_state transitions to CrState::Normal.
 *
 *  @phase              Reconnaissance
 *
 *  @prerequisites      None
 *
 *  @results_analysis   The congestion_event method signals a loss. Therefore cr_state transitions to CrState::Normal.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT
 *                        of 50 ms
 *                      • Call the congestion_event method from Resume, setting the largest packet number sent to 20.
 */
int careful_resume_packet_loss_recon_test() {
    /* cnx */
    picoquic_cnx_t* cnx = (picoquic_cnx_t*)malloc(sizeof(picoquic_cnx_t));
    cnx->quic = (picoquic_quic_t*)malloc(sizeof(picoquic_quic_t));
    cnx->quic->use_careful_resume = 1;

    /* path */
    picoquic_path_t* path_x = (picoquic_path_t*)malloc(sizeof(picoquic_path_t));

    path_x->cnx = cnx;

    /* careful resume */
    picoquic_cr_state_t* cr_state = (picoquic_cr_state_t*)malloc(sizeof(picoquic_cr_state_t));
    picoquic_cr_reset(cr_state, 0);
    cr_state->alg_state = picoquic_cr_alg_recon; /* Reconnaissance */
    cr_state->saved_cwnd = 80000;
    cr_state->saved_rtt = 50;

    /* for printf TODO maybe remove this in production */
    cnx->client_mode = 0;
    cnx->start_time = 0;
    path_x->unique_path_id = 0;

    picoquic_per_ack_state_t ack_state = { 0 };
    ack_state.nb_bytes_acknowledged = 0; /* TODO */
    picoquic_cr_notify(cr_state, cnx, path_x, picoquic_congestion_notification_timeout, &ack_state, 0);

    if (cr_state->alg_state == picoquic_cr_alg_normal) { /* cr_state transitions to CrState::Normal. */
        return 0;
    }

    return -1;
}

/* TODO implement */
int careful_resume_valid_rtt_full_reno_test() {
    return -1;
}

/** @name               valid_rtt_full_cubic
 *
 *  @brief              Checks if the cwnd has the expected value when transitioning to CrState::Unvalidated under
 *                      valid RTT conditions with Reno and Hystart++ is enabled. This is an integration test relying on
 *                      methods from Recovery to simulate sending packets and receiving acknowledgements.
 *
 *  @assertation        • cr_state transitions to CrState::Unvalidated with the correct cr_mark value (20)
 *                      • cwnd = 40,000
 *                      • pipesize is initialised to the current flightsize = 15,000 Bytes
 *
 *  @phase              Reconnaisance - Integration
 *
 *  @prerequisites      • previous cwnd = 80,000 Bytes
 *                      • previous RTT = 50 ms
 *                      • Cubic CC enabled
 *                      • Hystart++ enabled
 *
 *  @results_analysis   5 packets are sent and acknowledged after 50ms, providing a sample RTT 50 ms. 16 packets of
 *                      1000 Bytes in size are further sent to make flightsize = 16 *1000 = 16,000 Bytes. The cwnd is
 *                      12 000 (initial) + 4000 = 16,000. 16,000 < 80,000/2  and therefore the jump is realised,
 *                      cr_state transitions to CrState::Unvalidated, storing the largest packet number sent, 20. cwnd
 *                      becomes 80,000/2 = 40,000 Bytes. Flightsize of 16,000 is recorded in pipesize.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT
 *                        of 50 ms.
 *                      • Initialise a Recovery instance using Cubic CC, Hystart++ enabled, and Resume enabled.
 *                      • Call the on_packet_sent and on_ack_received methods from Recovery to simulate sending packets
 *                        with numbers 0 through 4 and insert acknowledgements for these packets after 50 miliseconds.
 *                      • Simulate the sending of packet with numbers 5-20, each of 1000 Bytes in size.
 */
/* TODO use hystart++ */
/* TODO implement */
int careful_resume_valid_rtt_full_cubic_test() {
    return -1;
    picoquic_quic_t* quic = (picoquic_quic_t*)malloc(sizeof(picoquic_quic_t));
    picoquic_cnx_t* cnx = (picoquic_cnx_t*)malloc(sizeof(picoquic_cnx_t));
    /* path */
    picoquic_path_t* path_x = (picoquic_path_t*)malloc(sizeof(picoquic_path_t));
    /* careful resume */
    picoquic_cr_state_t* cr_state = (picoquic_cr_state_t*)malloc(sizeof(picoquic_cr_state_t));

    /* cnx */

    /* init cubic on path */
    cnx->congestion_alg = picoquic_cubic_algorithm;
    cnx->congestion_alg->alg_init(cnx, path_x, 0);
    /* enable careful resume */
    cnx->quic = quic;
    cnx->quic->use_careful_resume = 1;

    /* Reconnaissance */
    cr_state->alg_state = picoquic_cr_alg_recon;
    /* previous cwnd = 80,000 Bytes */
    cr_state->saved_cwnd = 80000;
    /* previous RTT = 50 ms */
    cr_state->saved_rtt = 50;

    /* only for printf TODO maybe remove this in production */
    cnx->client_mode = 0;
    cnx->start_time = 0;
    path_x->cnx = cnx;
    path_x->unique_path_id = 0;

    for (int i = 0; i < 5; i++) {
        picoquic_per_ack_state_t ack_state = { 0 };
        ack_state.nb_bytes_acknowledged = 1000;
        ack_state.rtt_measurement = 50; /* providing a sample RTT 50 ms */
        cnx->congestion_alg->alg_notify(cnx, path_x, picoquic_congestion_notification_acknowledgement, &ack_state, 50);
        path_x->bytes_in_transit += ack_state.nb_bytes_acknowledged;
    }

    { /* cr_state transitions to CrState::Normal. */
        if (cr_state->alg_state == picoquic_cr_alg_normal)
            return 0;
    }

    return -1;
}

/* TODO implement */
int careful_resume_packet_loss_recon_full_reno_test() {
    return -1;
}

/** @name               packet_loss_recon_full_cubic
 *
 *  @brief              Checks if CR transitions to Normal phase when a packet loss is encountered in the
 *                      Reconnaissance phase, with Cubic and Hystart++  enabled. This is an  integration test relying
 *                      on methods from Recovery to simulate sending packets and receiving acknowledgements.
 *
 *  @assertation        • cr_state transitions to CrState::Normal
 *
 *  @phase              Reconnaisance - Integration
 *
 *  @prerequisites      • Cubic CC enabled
 *                      • Hystart++ enabled
 *
 *  @results_analysis   10 packets are sent in total before an RTT sample is calculated from any acknowledgements. An
 *                      acknowledgement comes missing Packet Number 5. This signals a loss, therefore cr_state
 *                      transitions to CrState::Normal.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT
 *                        of 50 ms.
 *                      • Initialise a Recovery instance using Cubic CC, Hystart++ enabled, and Resume enabled.
 *                      • Call the on_packet_sent and on_ack_received methods from Recovery to simulate sending packets
 *                        with numbers 0 through 9 and insert acknowledgements for packets 0-4 and 6-9 after 50 ms.
 */
/* TODO implement */
int careful_resume_packet_loss_recon_full_cubic_test() {
    return -1;
}

/* TODO implement */
int careful_resume_cwnd_larger_than_jump_full_test() {
    return -1;
}

/* TODO implement */
int careful_resume_invalid_rtt_full_test() {
    return -1;
}

/* TODO implement */
int careful_resume_pipesize_update_unval_test() {
    return -1;
}

/* TODO implement */
int careful_resume_packet_loss_unval_test() {
    return -1;
}

/* TODO implement */
int careful_resume_pipesize_update_unval_full_reno_hystart_test() {
    return -1;
}

/* TODO implement */
int careful_resume_pipesize_update_unval_full_reno_no_hystart_test() {
    return -1;
}

/* TODO implement */
int careful_resume_pipesize_update_unval_full_rcubic_no_hystart_test() {
    return -1;
}

/* TODO implement */
int careful_resume_packet_loss_unval_full_rto_test() {
    return -1;
}

/* TODO implement */
int careful_resume_packet_loss_unval_full_gap_reno_test() {
    return -1;
}

/* TODO implement */
int careful_resume_packet_loss_unval_full_gap_cubic_test() {
    return -1;
}

/* TODO implement */
int careful_resume_packet_loss_unval_full_gap_small_pipe_test() {
    return -1;
}

/* TODO implement */
int careful_resume_packet_loss_validating_test() {
    return -1;
}


int careful_resume_unit_test() {
    int ret = 0;

    ret = careful_resume_cwnd_larger_than_jump_test();
    if (ret == 0) {
        ret = careful_resume_rtt_less_than_half_test();
    }
    if (ret == 0) {
        ret = careful_resume_rtt_greater_than_10_test();
    }
    if (ret == 0) {
        ret = careful_resume_valid_rtt_test();
    }
    if (ret == 0) {
        ret = careful_resume_packet_loss_recon_test();
    }

    return ret;
}

static uint64_t careful_resume_run(picoquic_test_tls_api_ctx_t* test_ctx, uint64_t data_size, uint64_t loss_mask,
                                   uint64_t* simulated_time, uint64_t max_completion_time) {
    int ret = 0;

    /* Define test scenario. */
    test_api_stream_desc_t test_scenario[] = {
        {4, 0, 0, data_size}
    };

    /* Start client */
    ret = picoquic_start_client_cnx(test_ctx->cnx_client);

    /* Connect to server */
    if (ret == 0) {
        ret = tls_api_connection_loop(test_ctx, (loss_mask == 0x0) ? NULL : &loss_mask, 0, simulated_time);
    }

    /* Prepare to send data */
    if (ret == 0) {
        ret = test_api_init_send_recv_scenario(test_ctx, test_scenario, sizeof(test_scenario));
    }

    /* Perform a data sending loop */
    if (ret == 0) {
        ret = tls_api_data_sending_loop(test_ctx, (loss_mask == 0x0) ? NULL : &loss_mask, simulated_time, 0);
        // no loss
    }

    /* Verify scenario */
    if (ret == 0) {
        ret = tls_api_one_scenario_body_verify(test_ctx, simulated_time, max_completion_time);
    }

    printf("bytes sent: %" PRIu64 ", max datarate: %" PRIu64 "\n", test_ctx->cnx_server->path[0]->bytes_sent,
           test_ctx->cnx_server->path[0]->bandwidth_estimate);

    return ret;
}

static int careful_resume_test_one_ex(picoquic_congestion_algorithm_t* ccalgo,
                                      size_t data_size_1rtt, uint64_t mbps_up_1rtt, uint64_t mbps_down_1rtt,
                                      uint64_t latency_1rtt, uint64_t jitter_1rtt, uint64_t loss_mask_1rtt,
                                      size_t data_size_0rtt, uint64_t mbps_up_0rtt, uint64_t mbps_down_0rtt,
                                      uint64_t latency_0rtt, uint64_t jitter_0rtt, uint64_t loss_mask_0rtt,
                                      int bdp_option, uint64_t max_completion_time) {
    int ret = 0;

    /* Global variables */
    uint64_t simulated_time = 0;
    uint64_t server_ticket_id = 0;
    uint64_t client_ticket_id = 0;
    picoquic_test_tls_api_ctx_t* test_ctx = NULL;
    char const* ticket_seed_store = "careful_resume_ticket_store.bin";

    /* Initialize an empty ticket store */
    ret = picoquic_save_tickets(NULL, simulated_time, ticket_seed_store);

    /* Create cid for 1-RTT connection */
    picoquic_connection_id_t initial_cid_1rtt = {{0xcc, 0xaa, 0, 0, 0, 0, 0, 0}, 8};
    initial_cid_1rtt.id[2] = ccalgo->congestion_algorithm_number;
    initial_cid_1rtt.id[3] = (mbps_up_1rtt > 0xff) ? 0xff : (uint8_t)mbps_up_1rtt;
    initial_cid_1rtt.id[4] = (mbps_down_1rtt > 0xff) ? 0xff : (uint8_t)mbps_down_1rtt;
    initial_cid_1rtt.id[5] = (latency_1rtt > 2550000) ? 0xff : (uint8_t)(latency_1rtt / 10000);
    initial_cid_1rtt.id[6] = (jitter_1rtt > 255000) ? 0xff : (uint8_t)(jitter_1rtt / 1000);
    initial_cid_1rtt.id[7] = (loss_mask_1rtt) ? 0x30 : 0x00;
    if (loss_mask_1rtt) {
        initial_cid_1rtt.id[7] |= 0x20;
    }

    /* Create cid for 0-RTT connection */
    picoquic_connection_id_t initial_cid_0rtt = {{0xcc, 0xaa, 0, 0, 0, 0, 0, 0}, 8};
    initial_cid_0rtt.id[1] = 0xbb;
    initial_cid_0rtt.id[2] = ccalgo->congestion_algorithm_number;
    initial_cid_0rtt.id[3] = (mbps_up_0rtt > 0xff) ? 0xff : (uint8_t)mbps_up_0rtt;
    initial_cid_0rtt.id[4] = (mbps_down_0rtt > 0xff) ? 0xff : (uint8_t)mbps_down_0rtt;
    initial_cid_0rtt.id[5] = (latency_0rtt > 2550000) ? 0xff : (uint8_t)(latency_0rtt / 10000);
    initial_cid_0rtt.id[6] = (jitter_0rtt > 255000) ? 0xff : (uint8_t)(jitter_0rtt / 1000);
    initial_cid_0rtt.id[7] = (loss_mask_0rtt) ? 0x30 : 0x00;
    if (loss_mask_0rtt) {
        initial_cid_0rtt.id[7] |= 0x20;
    }

    /* Define max transport parameters. */
    picoquic_tp_t transport_parameters_max;
    memset(&transport_parameters_max, 0, sizeof(picoquic_tp_t));
    picoquic_init_transport_parameters(&transport_parameters_max, 0);
    transport_parameters_max.initial_max_data = UINT64_MAX;
    transport_parameters_max.initial_max_stream_data_bidi_local = UINT64_MAX;
    transport_parameters_max.initial_max_stream_data_bidi_remote = UINT64_MAX;
    transport_parameters_max.initial_max_stream_data_uni = UINT64_MAX;
    transport_parameters_max.enable_time_stamp = 3; /* TODO move somewhere else */

    /* Prepare a 1-RTT connection */
    if (ret == 0) // TODO remove ret always 0
    {
        printf("---------------- 1-RTT connection ----------------\n");

        /* Init test context and set delayed client start */
        ret = tls_api_init_ctx_ex2(&test_ctx, PICOQUIC_INTERNAL_TEST_VERSION_1, PICOQUIC_TEST_SNI, PICOQUIC_TEST_ALPN,
                                   &simulated_time,
                                   ticket_seed_store, NULL, 0, 1, 0, &initial_cid_1rtt, 8, 0, 0, 0);

        /* Set transport parameters */
        if (ret == 0) {
            picoquic_set_transport_parameters(test_ctx->cnx_client, &transport_parameters_max);
        }

        if (ret == 0) {
            ret = picoquic_set_default_tp(test_ctx->qserver, &transport_parameters_max);
        }

        /* Enable qlog */
        if (ret == 0) {
            picoquic_set_qlog(test_ctx->qclient, ".");
            picoquic_set_qlog(test_ctx->qserver, ".");
        }

        /* Set cc algo */
        if (ret == 0) {
            picoquic_set_default_congestion_algorithm(test_ctx->qserver, ccalgo);
            picoquic_set_congestion_algorithm(test_ctx->cnx_client, ccalgo);
        }

        /* Set BDP frame option */
        /*if (ret == 0)
        {
            picoquic_set_default_bdp_frame_option(test_ctx->qclient, bdp_option);
            picoquic_set_default_bdp_frame_option(test_ctx->qserver, bdp_option);
        }*/

        if (ret == 0) {
            picoquic_cnx_set_pmtud_required(test_ctx->cnx_client, 1); // TODO
        }

        /* Configure links between client and server */
        if (ret == 0) {
            test_ctx->c_to_s_link->jitter = jitter_1rtt;
            test_ctx->c_to_s_link->microsec_latency = latency_1rtt;
            test_ctx->c_to_s_link->picosec_per_byte = (1000000ull * 8) / mbps_up_1rtt;
            //test_ctx->c_to_s_link->loss_mask = &loss_mask_1rtt; // NOTE would be overriden in tls_api_connection_loop() and tls_api_data_sending_loop
            test_ctx->s_to_c_link->jitter = 0;
            test_ctx->s_to_c_link->microsec_latency = latency_1rtt;
            test_ctx->s_to_c_link->picosec_per_byte = (1000000ull * 8) / mbps_down_1rtt;
            //test_ctx->s_to_c_link->loss_mask = &loss_mask_1rtt; // NOTE would be overriden in tls_api_connection_loop() and tls_api_data_sending_loop
            //test_ctx->stream0_flow_release = 1;
            //test_ctx->immediate_exit = 1;
        }
    }

    /* Start first connection */
    if (ret == 0) {
        ret = careful_resume_run(test_ctx, data_size_1rtt, loss_mask_1rtt, &simulated_time, max_completion_time);
    }

    /* Check the issued tickets list at the server. */
    if (ret == 0) {
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
    }

    /* Now we remove the client connection and prepare 0-RTT connection. */
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
        picoquic_set_careful_resume(test_ctx->qclient, 1);
        picoquic_set_careful_resume(test_ctx->qserver, 1);

        /* Configure links between client and server */
        test_ctx->c_to_s_link->jitter = jitter_0rtt;
        test_ctx->c_to_s_link->microsec_latency = latency_0rtt;
        test_ctx->c_to_s_link->picosec_per_byte = (1000000ull * 8) / mbps_up_0rtt;
        //test_ctx->c_to_s_link->loss_mask = &loss_mask_1rtt; // NOTE would be overriden in tls_api_connection_loop() and tls_api_data_sending_loop
        test_ctx->s_to_c_link->jitter = 0;
        test_ctx->s_to_c_link->microsec_latency = latency_0rtt;
        test_ctx->s_to_c_link->picosec_per_byte = (1000000ull * 8) / mbps_down_0rtt;

        /* Create new client context. */
        test_ctx->cnx_client = picoquic_create_cnx(test_ctx->qclient,
                                                   initial_cid_0rtt, picoquic_null_connection_id,
                                                   (struct sockaddr*)&test_ctx->server_addr, simulated_time,
                                                   PICOQUIC_INTERNAL_TEST_VERSION_1, PICOQUIC_TEST_SNI,
                                                   PICOQUIC_TEST_ALPN, 1);

        if (test_ctx->cnx_client == NULL) {
            ret = -1;
        }

        if (ret == 0) {
            /* Set transport parameters for client. */
            if (ret == 0) {
                picoquic_set_transport_parameters(test_ctx->cnx_client, &transport_parameters_max);
            }

            picoquic_set_congestion_algorithm(test_ctx->cnx_client, ccalgo);

            /* Set BDP frame option */
            //picoquic_set_default_bdp_frame_option(test_ctx->qserver, bdp_option);

            picoquic_cnx_set_pmtud_required(test_ctx->cnx_client, 1); // TODO

            if (ret == 0) {
                ret = careful_resume_run(test_ctx, data_size_0rtt, loss_mask_0rtt, &simulated_time, max_completion_time);
            }
        }
    }

    /* Clean up test context */
    if (test_ctx != NULL) {
        tls_api_delete_ctx(test_ctx);
        test_ctx = NULL;
    }

    return ret;
}

static int careful_resume_test_one(picoquic_congestion_algorithm_t* ccalgo,
                                   size_t data_size, uint64_t mbps_up, uint64_t mbps_down, uint64_t latency,
                                   uint64_t jitter, uint64_t loss_mask,
                                   int bdp_option, uint64_t max_completion_time) {
    return careful_resume_test_one_ex(ccalgo,
                                      data_size, mbps_up, mbps_down, latency, jitter, loss_mask,
                                      data_size, mbps_up, mbps_down, latency, jitter, loss_mask,
                                      bdp_option, max_completion_time);
}

/** @name               default
 *
 *  @brief              Simple jump.
 */
int careful_resume_test() {
    // data_size = 25M
    return careful_resume_test_one(picoquic_cubic_algorithm, 50000000, 6, 50, 300000ull, 0, 0x0, 0, 30000000000);
}

/** @name               loss
 *
 *  @brief              Simple jump with packet loss.
 */
/* TODO discuss if we should ignore single losses */
int careful_resume_loss_test() {
    // data_size = 25M
    return careful_resume_test_one(picoquic_cubic_algorithm, 50000000, 6, 50, 300000ull, 0, 0x1001001, 0, 30000000000);
}

/** @name               data_limited_less_than_cwnd
 *
 *  @brief              Sender is data limited with data less than cwnd.
 */
int careful_resume_data_limited_less_than_cwnd_test() {
    return careful_resume_test_one_ex(picoquic_cubic_algorithm,
                                      25000000, 6, 50, 300000ull, 0, 0x0,
                                      10000, 6, 50, 300000ull, 0, 0x0,
                                      0, 3000000000);
}

/** @name               data_limited_in_validate
 *
 *  @brief              Sender is data limited in validate phase.
 */
int careful_resume_data_limited_in_validate_test() {
    return -1; /* TODO implement */
    return careful_resume_test_one_ex(picoquic_cubic_algorithm,
                                      25000000, 6, 50, 300000ull, 0, 0x0,
                                      1000000, 6, 50, 300000ull, 0, 0x0,
                                      0, 3000000000);
}

/** @name               loss_in_recon
 *
 *  @brief              Packet loss in recon phase.
 */
int careful_resume_loss_in_recon_test() {
    return -1; /* TODO implement */
    return careful_resume_test_one_ex(picoquic_cubic_algorithm,
                                      25000000, 6, 50, 300000ull, 0, 0x0,
                                      25000000, 6, 50, 300000ull, 0, 0x4, // packet 2 lost
                                      0, 3000000000);
}

/** @name               path_changed
 *
 *  @brief              Packet loss in validate phase.
 */
int careful_resume_loss_in_validate_test() {
    return -1; /* TODO disabled until we discussed single losses */
    return careful_resume_test_one_ex(picoquic_cubic_algorithm,
                                      25000000, 6, 50, 300000ull, 0, 0x0,
                                      25000000, 6, 50, 300000ull, 0, 0x10000000, // packet 25 lost
                                      0, 3000000000);
}

/** @name               loss_in_unval
 *
 *  @brief              Packet loss in unval phase.
 */
int careful_resume_loss_in_unval_test() {
    return -1; /* TODO disabled until we discussed single losses */
    return careful_resume_test_one_ex(picoquic_cubic_algorithm,
                                      25000000, 6, 50, 300000ull, 0, 0x0,
                                      25000000, 6, 50, 300000ull, 0, 0x10, // packet 13 lost
                                      0, 3000000000);
}

/** @name               path_changed
 *
 *  @brief              rtt_min of 0-RTT connection changes to saved_rtt / 2 or saved_rtt * 10
 */
int careful_resume_path_changed_test() {
    int ret = 0;
    /* RTT less than saved_rtt / 2 */
    ret = careful_resume_test_one_ex(picoquic_cubic_algorithm,
                                     25000000, 6, 50, 300000ull, 0, 0x0,
                                     25000000, 6, 50, 50000ull, 0, 0x0,
                                     0, 3000000000);

    if (ret == 0) {
        /* RTT greater than or equal to saved_rtt * 10 */
        return careful_resume_test_one_ex(picoquic_cubic_algorithm,
                                          25000000, 6, 50, 20000ull, 0, 0x0,
                                          25000000, 6, 50, 400000ull, 0, 0x0,
                                          0, 3000000000);
    }

    return ret;
}