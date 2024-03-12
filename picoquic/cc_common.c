/*
* Author: Christian Huitema
* Copyright (c) 2019, Private Octopus, Inc.
* All rights reserved.
*
* Permission to use, copy, modify, and distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL Private Octopus, Inc. BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "picoquic_internal.h"
#include <stdlib.h>
#include <string.h>
#include "cc_common.h"

uint64_t picoquic_cc_get_sequence_number(picoquic_cnx_t* cnx, picoquic_path_t* path_x) {
    uint64_t ret = path_x->path_packet_number;

    return ret;
}

uint64_t picoquic_cc_get_ack_number(picoquic_cnx_t* cnx, picoquic_path_t* path_x) {
    uint64_t ret = path_x->path_packet_acked_number;

    return ret;
}

uint64_t picoquic_cc_get_ack_sent_time(picoquic_cnx_t* cnx, picoquic_path_t* path_x) {
    uint64_t ret = path_x->path_packet_acked_time_sent;
    return ret;
}


void picoquic_filter_rtt_min_max(picoquic_min_max_rtt_t* rtt_track, uint64_t rtt) {
    int x = rtt_track->sample_current;
    int x_max;


    rtt_track->samples[x] = rtt;

    rtt_track->sample_current = x + 1;
    if (rtt_track->sample_current >= PICOQUIC_MIN_MAX_RTT_SCOPE) {
        rtt_track->is_init = 1;
        rtt_track->sample_current = 0;
    }

    x_max = (rtt_track->is_init) ? PICOQUIC_MIN_MAX_RTT_SCOPE : x + 1;

    rtt_track->sample_min = rtt_track->samples[0];
    rtt_track->sample_max = rtt_track->samples[0];

    for (int i = 1; i < x_max; i++) {
        if (rtt_track->samples[i] < rtt_track->sample_min) {
            rtt_track->sample_min = rtt_track->samples[i];
        }
        else if (rtt_track->samples[i] > rtt_track->sample_max) {
            rtt_track->sample_max = rtt_track->samples[i];
        }
    }
}

int picoquic_hystart_loss_test(picoquic_min_max_rtt_t* rtt_track, picoquic_congestion_notification_t event,
                               uint64_t lost_packet_number, double error_rate_max) {
    int ret = 0;
    uint64_t next_number = rtt_track->last_lost_packet_number;

    if (lost_packet_number > next_number) {
        if (next_number + PICOQUIC_SMOOTHED_LOSS_SCOPE < lost_packet_number) {
            next_number = lost_packet_number - PICOQUIC_SMOOTHED_LOSS_SCOPE;
        }

        while (next_number < lost_packet_number) {
            rtt_track->smoothed_drop_rate *= (1.0 - PICOQUIC_SMOOTHED_LOSS_FACTOR);
            next_number++;
        }

        rtt_track->smoothed_drop_rate += (1.0 - rtt_track->smoothed_drop_rate) * PICOQUIC_SMOOTHED_LOSS_FACTOR;
        rtt_track->last_lost_packet_number = lost_packet_number;

        switch (event) {
        case picoquic_congestion_notification_repeat:
            ret = rtt_track->smoothed_drop_rate > error_rate_max;
            break;
        case picoquic_congestion_notification_timeout:
            ret = 1;
        default:
            break;
        }
    }

    return ret;
}

int picoquic_hystart_loss_volume_test(picoquic_min_max_rtt_t* rtt_track, picoquic_congestion_notification_t event,
                                      uint64_t nb_bytes_newly_acked, uint64_t nb_bytes_newly_lost) {
    int ret = 0;

    rtt_track->smoothed_bytes_lost_16 -= rtt_track->smoothed_bytes_lost_16 / 16;
    rtt_track->smoothed_bytes_lost_16 += nb_bytes_newly_lost;
    rtt_track->smoothed_bytes_sent_16 -= rtt_track->smoothed_bytes_sent_16 / 16;
    rtt_track->smoothed_bytes_sent_16 += nb_bytes_newly_acked + nb_bytes_newly_lost;

    if (rtt_track->smoothed_bytes_sent_16 > 0) {
        rtt_track->smoothed_drop_rate = ((double)rtt_track->smoothed_bytes_lost_16) / ((double)rtt_track->
            smoothed_bytes_sent_16);
    }
    else {
        rtt_track->smoothed_drop_rate = 0;
    }

    switch (event) {
    case picoquic_congestion_notification_acknowledgement:
        ret = rtt_track->smoothed_drop_rate > PICOQUIC_SMOOTHED_LOSS_THRESHOLD;
        break;
    case picoquic_congestion_notification_timeout:
        ret = 1;
    default:
        break;
    }

    return ret;
}

int picoquic_hystart_test(picoquic_min_max_rtt_t* rtt_track, uint64_t rtt_measurement, uint64_t packet_time,
                          uint64_t current_time, int is_one_way_delay_enabled) {
    int ret = 0;

    if (current_time > rtt_track->last_rtt_sample_time + 1000) {
        picoquic_filter_rtt_min_max(rtt_track, rtt_measurement);
        rtt_track->last_rtt_sample_time = current_time;

        if (rtt_track->is_init) {
            uint64_t delta_max;

            if (rtt_track->rtt_filtered_min == 0 ||
                rtt_track->rtt_filtered_min > rtt_track->sample_max) {
                rtt_track->rtt_filtered_min = rtt_track->sample_max;
            }
            delta_max = rtt_track->rtt_filtered_min / 4;

            if (rtt_track->sample_min > rtt_track->rtt_filtered_min) {
                if (rtt_track->sample_min > rtt_track->rtt_filtered_min + delta_max) {
                    rtt_track->nb_rtt_excess++;
                    if (rtt_track->nb_rtt_excess >= PICOQUIC_MIN_MAX_RTT_SCOPE) {
                        /* RTT increased too much, get out of slow start! */
                        ret = 1;
                    }
                }
            }
            else {
                rtt_track->nb_rtt_excess = 0;
            }
        }
    }

    return ret;
}

void picoquic_hystart_increase(picoquic_path_t* path_x, picoquic_min_max_rtt_t* rtt_filter, uint64_t nb_delivered) {
    path_x->cwin += nb_delivered;
}

/* Reset careful resume context. */
void picoquic_cr_reset(picoquic_cr_state_t* cr_state, uint64_t current_time) {
    printf("\033[0;35m%-37" PRIu64 "picoquic_cr_reset()\033[0m\n", current_time);
    memset(cr_state, 0, sizeof(picoquic_cr_state_t));
    /* Starting in recon phase. */
    cr_state->alg_state = picoquic_cr_alg_recon;

    cr_state->saved_cwnd = 0;
    cr_state->saved_rtt = 0;

    cr_state->unval_mark = 0;
    cr_state->val_mark = 0;

    cr_state->pipesize = 0;

    cr_state->start_of_epoch = current_time;
    cr_state->previous_start_of_epoch = 0;
}

/* Notify careful resume context. */
void picoquic_cr_notify(
    picoquic_cr_state_t* cr_state,
    picoquic_cnx_t* cnx,
    picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t* ack_state,
    uint64_t current_time) {

    /* Process notification only if careful resume enabled and path seeded. */
    if (!cnx->quic->use_careful_resume || !cr_state->saved_cwnd || !cr_state->saved_rtt) {
        return;
    }

    switch (notification) {
        case picoquic_congestion_notification_acknowledgement:
            switch (cr_state->alg_state) {
                case picoquic_cr_alg_unval:
                    /* UNVAL, VALIDATE, RETREAT: PS+=ACked */
                    /* *Unvalidated Phase (Receiving acknowledgements for reconnaisance
                        packets): The variable PipeSize if increased by the amount of
                        data that is acknowledged by each acknowledgment (in bytes). This
                        indicated a previously unvalidated packet has been succesfuly
                        sent over the path. */
                    /* *Validating Phase (Receiving acknowledgements for unvalidated
                        packets): The variable PipeSize if increased upon each
                        acknowledgment that indicates a packet has been successfuly sent
                        over the path. This records the validated PipeSize in bytes. */
                    /* *Safe Retreat Phase (Tracking PipeSize): The sender continues to
                        update the PipeSize after processing each ACK. This value is used
                        to reset the ssthresh when leaving this phase, it does not modify
                        CWND. */
                    cr_state->pipesize += ack_state->nb_bytes_acknowledged;

                    /*  UNVAL If( >1 RTT has passed or FS=CWND or first unvalidated packet is ACKed), enter Validating */
                    /* *Unvalidated Phase (Receiving acknowledgements for an unvalidated
                        packet): The sender enters the Validating Phase when the first
                        acknowledgement is received for the first packet number (or
                        higher) that was sent in the Unvalidated Phase. */
                    /*  first unvalidated packet is ACKed */
                    /*  ... sender initialises the PipeSize to to the CWND (the same as the flight size, 29 packets) ...
                        ... When the first unvalidated packet is acknowledged (packet number 30) the sender enters the Validating Phase. */
                    /* TODO (current_time - cr_state->start_of_epoch) > path_x->rtt_min */
                    if (path_x->delivered > cr_state->unval_mark) {
                        printf("\033[0;35m%37sdelivered=%" PRIu64 " > unval_mark=%" PRIu64 ", first unvalidated packet "
                            "ACKed\033[0m\n", "", path_x->delivered, cr_state->unval_mark);
                        picoquic_cr_enter_validate(cr_state, path_x, current_time);
                    }
                    break;
                case picoquic_cr_alg_validate:
                    /* UNVAL, VALIDATE, RETREAT: PS+=ACked */
                    /* *Unvalidated Phase (Receiving acknowledgements for reconnaisance
                        packets): The variable PipeSize if increased by the amount of
                        data that is acknowledged by each acknowledgment (in bytes). This
                        indicated a previously unvalidated packet has been succesfuly
                        sent over the path. */
                    /* *Validating Phase (Receiving acknowledgements for unvalidated
                        packets): The variable PipeSize if increased upon each
                        acknowledgment that indicates a packet has been successfuly sent
                        over the path. This records the validated PipeSize in bytes. */
                    /* *Safe Retreat Phase (Tracking PipeSize): The sender continues to
                        update the PipeSize after processing each ACK. This value is used
                        to reset the ssthresh when leaving this phase, it does not modify
                        CWND. */
                    cr_state->pipesize += ack_state->nb_bytes_acknowledged;

                    /* VALIDATE: If (last unvalidated packet is ACKed) enter Normal */
                    /* *Validating Phase (Receiving acknowledgement for all unvalidated
                        packets): The sender enters the Normal Phase when an
                        acknowledgement is received for the last packet number (or
                        higher) that was sent in the Unvalidated Phase. */
                    if (path_x->delivered >= cr_state->val_mark) {
                        printf("\033[0;35m%37sdelivered=%" PRIu64 " > val_mark=%" PRIu64 ", last packet that was sent in the "
                            "unvalidated phase ACKed\033[0m\n", "", path_x->delivered, cr_state->val_mark);
                        picoquic_cr_enter_normal(cr_state, path_x, current_time);
                    }
                    break;
                case picoquic_cr_alg_retreat:
                    /* UNVAL, VALIDATE, RETREAT: PS+=ACked */
                    /* *Unvalidated Phase (Receiving acknowledgements for reconnaisance
                        packets): The variable PipeSize if increased by the amount of
                        data that is acknowledged by each acknowledgment (in bytes). This
                        indicated a previously unvalidated packet has been succesfuly
                        sent over the path. */
                    /* *Validating Phase (Receiving acknowledgements for unvalidated
                        packets): The variable PipeSize if increased upon each
                        acknowledgment that indicates a packet has been successfuly sent
                        over the path. This records the validated PipeSize in bytes. */
                    /* *Safe Retreat Phase (Tracking PipeSize): The sender continues to
                        update the PipeSize after processing each ACK. This value is used
                        to reset the ssthresh when leaving this phase, it does not modify
                        CWND. */
                    cr_state->pipesize += ack_state->nb_bytes_acknowledged;

                    /* RETREAT: if (last unvalidated packet is ACKed), ssthresh=PS and then enter Normal */
                    /* *Safe Retreat Phase (Receiving acknowledgement for all unvalidated
                        packets): The sender enters Normal Phase when the last packet (or
                        later) sent during the Unvalidated Phase has been acknowledged.
                        The sender MUST set the ssthresh to no morethan the PipeSize. */
                    /*  The sender leaves the Safe Retreat Phase when the last packet number
                        (or higher) sent in the Unvalidated Phase is acknowledged. If the
                        last packet number is not cumulatively acknowledged, then additional
                        packets might need to be retransmitted. */
                    if (path_x->delivered >= cr_state->val_mark) {
                        printf("\033[0;35m%37sdelivered=%" PRIu64 " > val_mark=%" PRIu64
                            ", last packet that was sent in the unvalidated phase ACKed\033[0m\n", "", path_x->delivered,
                            cr_state->val_mark);
                        picoquic_cr_enter_normal(cr_state, path_x, current_time);
                        /* ssthresh is set in congestion control algorithm (e.g. cubic.c, newreno.c) */
                    }
                    break;
                default:
                    break;
            }
            break;
        case picoquic_congestion_notification_sent:
            switch (cr_state->alg_state) {
                case picoquic_cr_alg_recon:
                    /* RECON: If (FS=CWND and CR confirmed), enter Unvaliding else enter Normal */
                    /*  continues in a subsequent RTT to send more packets until the sender becomes CWND-limited (i.e., flight size=CWND) */
                    /* *Reconnaissance Phase (Data-limited sender): If the sender is
                        data-limited [RFC7661], it might send insufficient data to be
                        able to validate transmission at the higher rate. Careful Resume
                        is allowed to remain in the Reconnaissance Phase and to not
                        transition to the Unvalidated Phase until the sender has more
                        data ready to send in the transmission buffer than is permitted
                        by the current CWND. In some implementations, the decision to
                        enter the Unvalidated Phase could require coordination with the
                        management of buffers in the interface to the higher layers. */
                    /*  When a sender confirms the path and it receives an acknowledgement
                        for the initial data without reported congestion, it MAY then enter
                        the Unvalidated Phase. This transition occurs when a sender has more
                        data than permitted by the current CWND. */
                    if (path_x->bytes_in_transit >= path_x->cwin) //  FS=CWND
                    {
                        printf("\033[0;35m%37sbytes_in_transit=%" PRIu64 " >= cwin=%" PRIu64 ", sender has more data than "
                            "permitted by the current CWND\033[0m\n", "", path_x->bytes_in_transit, path_x->cwin);
                        /* if saved_cwnd smaller than CWND avoid careful resume -> enter normal */
                        if (path_x->cwin > cr_state->saved_cwnd / 2) {
                            printf("\033[0;35m%37scwin=%" PRIu64 " >= saved_cwnd=%" PRIu64 ", current CWIN greater than "
                                "or equal to saved_cwnd\033[0m\n", "", path_x->cwin, cr_state->saved_cwnd);
                            picoquic_cr_enter_normal(cr_state, path_x, current_time);
                        } else

                        /*  Unvalidated Phase (Confirming the path on entry): The calculation
                            of a sending rate from a saved_cwnd is directly impacted by the
                            RTT, therefore a significant change in the RTT is a strong
                            indication that the previously observed CC parameters may not be
                            valid for the current path. If the RTT measurement is not
                            confirmed, i.e., the current_rtt is greater than or equal to
                            (saved_rtt / 2) and the current_rtt is less than or equal to
                            (saved_rtt x 10) Section 4.2.1), the sender MUST enter the Normal
                            Phase. */
                        /*  In the Reconnaissance Phase a sender initiates a connection and
                            starts sending initial data. This measures the current minimum RTT.
                            If a decision is made to use Careful Resume, this is used to confirm
                            the path. */
                        if (path_x->rtt_min < cr_state->saved_rtt / 2 || path_x->rtt_min >= cr_state->saved_rtt * 10)
                        {
                            printf("\033[0;35m%37srtt_min=%" PRIu64 " < saved_rtt/2=%" PRIu64 " || "
                                    "rtt_min=%" PRIu64 " >= saved_rtt*10=%" PRIu64 ", the current_rtt is greater than or "
                                    "equal to (saved_rtt / 2) and the current_rtt is less than or equal to "
                                    "(saved_rtt x 10)\033[0m\n", "", path_x->rtt_min, cr_state->saved_rtt / 2,
                                    path_x->rtt_min, cr_state->saved_rtt * 10);
                            picoquic_cr_enter_normal(cr_state, path_x, current_time);
                        } else {
                            picoquic_cr_enter_unval(cr_state, path_x, current_time);
                        }
                    }
                    break;
                case picoquic_cr_alg_unval:
                    /* UNVAL: If( >1 RTT has passed or FS=CWND or first unvalidated packet is ACKed), enter Validating */
                    if ((current_time - cr_state->start_of_epoch) > path_x->rtt_min || path_x->bytes_in_transit >= path_x->cwin)
                    {
                        printf("\033[0;35m%37s(current_time - start_of_epoch)=%" PRIu64 " > rtt_min=%" PRIu64
                            " || bytes_in_transit=%" PRIu64 " >= cwin=%" PRIu64 ", >1 RTT has passed or FS=CWND\033[0m\n",
                            "", current_time - cr_state->start_of_epoch, path_x->rtt_min, path_x->bytes_in_transit,
                            path_x->cwin);
                        picoquic_cr_enter_validate(cr_state, path_x, current_time);
                    }
                    break;
                default:
                    break;
            }
            break;
        case picoquic_congestion_notification_repeat:
        case picoquic_congestion_notification_ecn_ec:
        case picoquic_congestion_notification_timeout:
            switch (cr_state->alg_state) {
                case picoquic_cr_alg_recon:
                    /* RECON: Normal CC method CR is not allowed */
                    /* *Reconnaissance Phase (Detected congestion): If the sender detects
                        congestion (e.g., packet loss or ECN-CE marking), the sender does
                        not use the Careful Resume method and MUST enter the Normal Phase
                        to respond to the detected congestion. */
                    picoquic_cr_enter_normal(cr_state, path_x, current_time);
                    break;
                case picoquic_cr_alg_unval:
                case picoquic_cr_alg_validate:
                    /* UNVAL, VALIDATE: Enter Safe Retreat */
                    /* *Validating Phase (Congestion indication): If a sender determines
                        that congestion was experienced (e.g., packet loss or ECN-CE
                        marking), Careful Resume enters the Safe Retreat Phase. */
                    picoquic_cr_enter_retreat(cr_state, path_x, current_time);
                    break;
                default:
                    break;
            }
            break;
        case picoquic_congestion_notification_spurious_repeat:
            /* TODO resume careful resume if congestion notification was spurious? */
            break;
        case picoquic_congestion_notification_rtt_measurement:
            break;
        case picoquic_congestion_notification_cwin_blocked:
            break;
        case picoquic_congestion_notification_reset:
            /* careful resume state resets with congestion control algorithm. */
            break;
        case picoquic_congestion_notification_seed_cwin:
            break;
        default:
            break;
    }
}

/* Enter RECON phase. */
void picoquic_cr_enter_recon(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x,
                                 uint64_t current_time) {
    printf("\033[0;35m%37scwin=%" PRIu64 ", rtt_min=%" PRIu64 ", saved_cwnd=%" PRIu64 ", saved_rtt=%" PRIu64
        ",\n%37sunval_mark=%" PRIu64 ", val_mark=%" PRIu64 ", pipesize=%" PRIu64 "\033[0m\n",
        "", path_x->cwin, path_x->rtt_min, cr_state->saved_cwnd, cr_state->saved_rtt, "",
        cr_state->unval_mark, cr_state->val_mark, cr_state->pipesize);
    printf("\033[0;35m%-15" PRIu64 "%-15" PRIu64 "%-7spicoquic_resume_enter_recon(unique_path_id=%" PRIu64 ")\033[0m\n",
            current_time, current_time - path_x->cnx->start_time,(path_x->cnx->client_mode) ? "CLIENT" : "SERVER",
            path_x->unique_path_id);
    cr_state->alg_state = picoquic_cr_alg_recon;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    /* RECON: CWND=IW */
    path_x->cwin = PICOQUIC_CWIN_INITIAL;

    printf("\033[0;35m%37scwin=%" PRIu64 ", rtt_min=%" PRIu64 ", saved_cwnd=%" PRIu64 ", saved_rtt=%" PRIu64
        ",\n%37sunval_mark=%" PRIu64 ", val_mark=%" PRIu64 ", pipesize=%" PRIu64 "\033[0m\n",
        "", path_x->cwin, path_x->rtt_min, cr_state->saved_cwnd, cr_state->saved_rtt, "",
        cr_state->unval_mark, cr_state->val_mark, cr_state->pipesize);
}

/* Enter UNVAL phase. */
void picoquic_cr_enter_unval(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x,
                             uint64_t current_time) {
    printf("\033[0;35m%37scwin=%" PRIu64 ", rtt_min=%" PRIu64 ", saved_cwnd=%" PRIu64 ", saved_rtt=%" PRIu64
        ",\n%37sunval_mark=%" PRIu64 ", val_mark=%" PRIu64 ", pipesize=%" PRIu64 "\033[0m\n",
        "", path_x->cwin, path_x->rtt_min, cr_state->saved_cwnd, cr_state->saved_rtt, "",
        cr_state->unval_mark, cr_state->val_mark, cr_state->pipesize);
    printf("\033[0;35m%-15" PRIu64 "%-15" PRIu64 "%-7spicoquic_cr_enter_unval(unique_path_id=%" PRIu64 ")\033[0m\n",
           current_time, current_time - path_x->cnx->start_time, (path_x->cnx->client_mode) ? "CLIENT" : "SERVER",
           path_x->unique_path_id);
    cr_state->alg_state = picoquic_cr_alg_unval;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    /* Mark lower and upper bound of the jumpwindow/unvalidated packets in bytes. */
    cr_state->unval_mark = path_x->delivered + path_x->bytes_in_transit;
    cr_state->val_mark = path_x->delivered + ((cr_state->saved_cwnd / 2));

    /* UNVAL: PS=CWND */
    /* *Unvalidated Phase (Initialising PipeSize): The variable PipeSize
        if initialised to CWND on entry to the Unvalidated Phase. This
        records the value before the jump is applied. */
    cr_state->pipesize = path_x->bytes_in_transit; /* Should be the naerly the same as CWIN */

    /* UNVAL: CWND=jump_cwnd */
    /* *Unvalidated Phase (Setting the jump_cwnd): To avoid starving
        other flows that could have either started or increased their
        used capacity after the Observation Phase, the jump_cwnd MUST be
        no more than half of the saved_cwnd. Hence, jump_cwnd is less
        than or equal to the (saved_cwnd/2). CWND = jump_cwnd. */
    //uint64_t jump_cwnd = resume_state->saved_cwnd / 2;
    path_x->cwin = cr_state->saved_cwnd / 2;

    printf("\033[0;35m%37scwin=%" PRIu64 ", rtt_min=%" PRIu64 ", saved_cwnd=%" PRIu64 ", saved_rtt=%" PRIu64
        ",\n%37sunval_mark=%" PRIu64 ", val_mark=%" PRIu64 ", pipesize=%" PRIu64 "\033[0m\n",
        "", path_x->cwin, path_x->rtt_min, cr_state->saved_cwnd, cr_state->saved_rtt, "",
        cr_state->unval_mark, cr_state->val_mark, cr_state->pipesize);
}

/* Enter VALIDATE phase. */
void picoquic_cr_enter_validate(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x,
                                uint64_t current_time) {
    printf("\033[0;35m%37scwin=%" PRIu64 ", rtt_min=%" PRIu64 ", saved_cwnd=%" PRIu64 ", saved_rtt=%" PRIu64
        ",\n%37sunval_mark=%" PRIu64 ", val_mark=%" PRIu64 ", pipesize=%" PRIu64 "\033[0m\n",
        "", path_x->cwin, path_x->rtt_min, cr_state->saved_cwnd, cr_state->saved_rtt, "",
        cr_state->unval_mark, cr_state->val_mark, cr_state->pipesize);
    printf("\033[0;35m%-15" PRIu64 "%-15" PRIu64 "%-7spicoquic_cr_enter_validate(unique_path_id=%" PRIu64 ")\033[0m\n",
           current_time, current_time - path_x->cnx->start_time, (path_x->cnx->client_mode) ? "CLIENT" : "SERVER",
           path_x->unique_path_id);
    cr_state->alg_state = picoquic_cr_alg_validate;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    /* VALIDATE: CWND=FS */
    /* *Validating Phase (Limiting CWND on entry): On entry to the
        Validating Phase, the CWND is set to the flight size. */
    path_x->cwin = path_x->bytes_in_transit; // TODO JOERG cwin reduced?

    printf("\033[0;35m%37scwin=%" PRIu64 ", rtt_min=%" PRIu64 ", saved_cwnd=%" PRIu64 ", saved_rtt=%" PRIu64
        ",\n%37sunval_mark=%" PRIu64 ", val_mark=%" PRIu64 ", pipesize=%" PRIu64 "\033[0m\n",
        "", path_x->cwin, path_x->rtt_min, cr_state->saved_cwnd, cr_state->saved_rtt, "",
        cr_state->unval_mark, cr_state->val_mark, cr_state->pipesize);
}

/* Enter RETREAT phase. */
void picoquic_cr_enter_retreat(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x,
                               uint64_t current_time) {
    printf("\033[0;35m%37scwin=%" PRIu64 ", rtt_min=%" PRIu64 ", saved_cwnd=%" PRIu64 ", saved_rtt=%" PRIu64
        ",\n%37sunval_mark=%" PRIu64 ", val_mark=%" PRIu64 ", pipesize=%" PRIu64 "\033[0m\n",
        "", path_x->cwin, path_x->rtt_min, cr_state->saved_cwnd, cr_state->saved_rtt, "",
        cr_state->unval_mark, cr_state->val_mark, cr_state->pipesize);
    printf("\033[0;35m%-15" PRIu64 "%-15" PRIu64 "%-7spicoquic_cr_enter_retreat(unique_path_id=%" PRIu64 ")\033[0m\n",
           current_time, current_time - path_x->cnx->start_time, (path_x->cnx->client_mode) ? "CLIENT" : "SERVER",
           path_x->unique_path_id);
    cr_state->alg_state = picoquic_cr_alg_retreat;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    /* RETREAT: CWND=(PS/2) */
    /* *Safe Retreat Phase (Re-initializing CC): On entry, the CWND MUST
        be reduced to no more than the (PipeSize/2). This avoids
        persistent starvation by allowing capacity for other flows to
        regain their share of the total capacity. */
    /*  Unacknowledged packets that were sent in the Unvalidated Phase can
        be lost when there is congestion. Loss recovery commences using the
        reduced CWND that was set on entry to the Safe Retreat Phase. */
    /*  NOTE: On entry to the Safe Retreat Phase, the CWND can be
        significantly reduced, when there was multiple loss, recovery of
        all lost data could require multiple RTTs to complete. */
    /*  The loss is then detected (by receiving three ACKs that do not cover
        packet number 35), the sender then enters the Safe Retreat Phase
        because the window was not validated. The PipeSize at this point is
        equal to 29 + 34 = 66 packets. Assuming IW=10. The CWND is reset to
        Max(10,ps/2) = Max(10,66/2) = 33 packets. */
    path_x->cwin = (cr_state->pipesize / 2 >= PICOQUIC_CWIN_INITIAL)
                       ? cr_state->pipesize / 2
                       : PICOQUIC_CWIN_INITIAL;

    /* *Safe Retreat Phase (Removing saved information): The set of saved
        CC parameters for the path are deleted, to prevent these from
        being used again by other flows. */
    path_x->cnx->seed_cwin = 0;
    path_x->cnx->seed_rtt_min = 0;

    printf("\033[0;35m%37scwin=%" PRIu64 ", rtt_min=%" PRIu64 ", saved_cwnd=%" PRIu64 ", saved_rtt=%" PRIu64
        ",\n%37sunval_mark=%" PRIu64 ", val_mark=%" PRIu64 ", pipesize=%" PRIu64 "\033[0m\n",
        "", path_x->cwin, path_x->rtt_min, cr_state->saved_cwnd, cr_state->saved_rtt, "",
        cr_state->unval_mark, cr_state->val_mark, cr_state->pipesize);
}

/* Enter NORMAL phase. */
void picoquic_cr_enter_normal(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x,
                              uint64_t current_time) {
    printf("\033[0;35m%37scwin=%" PRIu64 ", rtt_min=%" PRIu64 ", saved_cwnd=%" PRIu64 ", saved_rtt=%" PRIu64
        ",\n%37sunval_mark=%" PRIu64 ", val_mark=%" PRIu64 ", pipesize=%" PRIu64 "\033[0m\n",
        "", path_x->cwin, path_x->rtt_min, cr_state->saved_cwnd, cr_state->saved_rtt, "",
        cr_state->unval_mark, cr_state->val_mark, cr_state->pipesize);
    printf("\033[0;35m%-15" PRIu64 "%-15" PRIu64 "%-7spicoquic_cr_enter_normal(unique_path_id=%" PRIu64 ")\033[0m\n",
           current_time, current_time - path_x->cnx->start_time, (path_x->cnx->client_mode) ? "CLIENT" : "SERVER",
           path_x->unique_path_id);
    cr_state->alg_state = picoquic_cr_alg_normal;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    printf("\033[0;35m%37scwin=%" PRIu64 ", rtt_min=%" PRIu64 ", saved_cwnd=%" PRIu64 ", saved_rtt=%" PRIu64
        ",\n%37sunval_mark=%" PRIu64 ", val_mark=%" PRIu64 ", pipesize=%" PRIu64 "\033[0m\n",
        "", path_x->cwin, path_x->rtt_min, cr_state->saved_cwnd, cr_state->saved_rtt, "",
        cr_state->unval_mark, cr_state->val_mark, cr_state->pipesize);
}

void picoquic_cr_enter_observe(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x, uint64_t current_time) {
    cr_state->alg_state = picoquic_cr_alg_observe;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;
}

uint64_t picoquic_cc_increased_window(picoquic_cnx_t* cnx, uint64_t previous_window) {
    uint64_t new_window;
    if (cnx->path[0]->rtt_min <= PICOQUIC_TARGET_RENO_RTT) {
        new_window = previous_window * 2;
    }
    else {
        double w = (double)previous_window;
        w /= (double)PICOQUIC_TARGET_RENO_RTT;
        w *= (cnx->path[0]->rtt_min > PICOQUIC_TARGET_SATELLITE_RTT)
                 ? PICOQUIC_TARGET_SATELLITE_RTT
                 : cnx->path[0]->rtt_min;
        new_window = (uint64_t)w;
    }
    return new_window;
}
