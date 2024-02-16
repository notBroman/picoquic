#include <stdlib.h>
#include <string.h>

#include "picoquic_internal.h"
#include "resume.h"

#include <cc_common.h>

/* Reset careful resume context. */
void picoquic_resume_reset(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, unsigned int enabled, uint64_t current_time)
{
    printf("\033[0;35m%s\tpicoquic_resume_reset(resume_state=%p, path_x=%p, enabled=%d, current_time=%" PRIu64 ")\033[0m\n", (path_x->cnx->client_mode) ? "CLIENT" : "SERVER", resume_state, path_x, enabled, current_time);
    memset(resume_state, 0, sizeof(picoquic_resume_state_t));
    resume_state->enabled = enabled;
    // Starting at recon as draft does not yet discuss observe
    resume_state->alg_state = picoquic_resume_alg_recon;

    // test case TODO get from BDP frame
    resume_state->saved_rtt = 600 * 1000ull; /* rtt in micro seconds */
    resume_state->saved_cwnd = 6365958; /* jump window in packets */ // TODO check if mtu == SMSS, PICOQUIC_MAX_PACKET_SIZE?

    resume_state->unval_mark = 0; /* TODO init CR mark, cr_mark as packet number */
    resume_state->val_mark = 0;

    resume_state->pipesize = 0; /* measure of the validated available capacity based on the acknowledged data while in UNVAL, VALIDATE, RETREAT */
    //resume_state->recover = 0; /* recover mark as packet number */

    resume_state->start_of_epoch = current_time;
    resume_state->previous_start_of_epoch = 0;

    path_x->is_cr_data_updated = 1;
}

/* Initialise careful resume context. */
void picoquic_resume_init(picoquic_cnx_t * cnx, picoquic_path_t* path_x, unsigned int enabled, uint64_t current_time)
{
    printf("\033[0;35m%s\tpicoquic_resume_init(picoquic_cnx_t=%p, path_x=%p, enabled=%u, current_time=%" PRIu64 ")\033[0m\n", (path_x->cnx->client_mode) ? "CLIENT" : "SERVER", cnx, path_x, enabled, current_time);
    /* Initialize the state of the careful resume algorithm */
    picoquic_resume_state_t* resume_state = (picoquic_resume_state_t*)malloc(sizeof(picoquic_resume_state_t));
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(cnx);
#endif
    path_x->careful_resume_state = resume_state; // TODO save cr context somewhere else?
    if (resume_state != NULL) {
        picoquic_resume_reset(resume_state, path_x, enabled, current_time);
    }
}

/* Returns false if careful resume is disabled globally OR state == normal. Otherwise true. */
int picoquic_resume_enabled(picoquic_cnx_t * cnx, picoquic_path_t* path_x)
{
    if (cnx->quic->careful_resume_enabled && path_x->careful_resume_state->enabled && path_x->careful_resume_state->alg_state != picoquic_resume_alg_normal)
    {
        return 1;
    }

    return 0;
}

size_t picoquic_resume_process_ack(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t nb_bytes_acknowledged, uint64_t current_time) {
    //printf("\033[0;35m%s\tpicoquic_resume_process_ack(resume_state=%p, path_x=%p, nb_bytes_acknowledged=%" PRIu64 ", current_time=%" PRIu64 ")\033[0m\n", (path_x->cnx->client_mode) ? "CLIENT" : "SERVER", resume_state, path_x, nb_bytes_acknowledged, current_time);

    // UNVAL, VALIDATE, RETREAT: PS+=ACked
    /*  *Unvalidated Phase (Receiving acknowledgements for reconnaisance
        packets): The variable PipeSize if increased by the amount of
        data that is acknowledged by each acknowledgment (in bytes). This
        indicated a previously unvalidated packet has been succesfuly
        sent over the path. */
    /*  *Validating Phase (Receiving acknowledgements for unvalidated
        packets): The variable PipeSize if increased upon each
        acknowledgment that indicates a packet has been successfuly sent
        over the path. This records the validated PipeSize in bytes. */
    /*  *Safe Retreat Phase (Tracking PipeSize): The sender continues to
        update the PipeSize after processing each ACK. This value is used
        to reset the ssthresh when leaving this phase, it does not modify
        CWND. */
    if (resume_state->alg_state == picoquic_resume_alg_unval || resume_state->alg_state == picoquic_resume_alg_validate || resume_state->alg_state == picoquic_resume_alg_retreat)
    {
        resume_state->pipesize += nb_bytes_acknowledged;
        path_x->is_cr_data_updated = 1;
    }

    //  UNVAL If( >1 RTT has passed or FS=CWND or first unvalidated packet is ACKed), enter Validating
    /* *Unvalidated Phase (Receiving acknowledgements for an unvalidated
     *  packet): The sender enters the Validating Phase when the first
     *  acknowledgement is received for the first packet number (or
     *  higher) that was sent in the Unvalidated Phase. */
    //  first unvalidated packet is ACKed
    /*  ... sender initialises the PipeSize to to the CWND (the same as the flight size, 29 packets) ...
    *   ... When the first unvalidated packet is acknowledged (packet number 30) the sender enters the Validating Phase. */
    //if (resume_state->alg_state == picoquic_resume_alg_unval && picoquic_cc_get_ack_number(path_x->cnx, path_x) > resume_state->unval_mark)
    if (resume_state->alg_state == picoquic_resume_alg_unval && path_x->delivered > resume_state->unval_mark)
    {
        picoquic_resume_enter_validate(resume_state, path_x, current_time);
        //resume_state->cr_mark = picoquic_cc_get_ack_number(path_x->cnx, path_x);
    }

    // VALIDATE: If (last unvalidated packet is ACKed) enter Normal
    /*  *Validating Phase (Receiving acknowledgement for all unvalidated
        packets): The sender enters the Normal Phase when an
        acknowledgement is received for the last packet number (or
        higher) that was sent in the Unvalidated Phase. */
    //  TODO check last unvalidated packet is ACKed
    //if (resume_state->alg_state == picoquic_resume_alg_validate && picoquic_cc_get_ack_number(path_x->cnx, path_x) >= resume_state->val_mark) // TODO check The sender enters the Normal Phase when an acknowledgement is received for the last packet number (or higher) that was sent in the Unvalidated Phase.
    if (resume_state->alg_state == picoquic_resume_alg_validate && path_x->delivered >= resume_state->val_mark)
    {
        picoquic_resume_enter_normal(resume_state, path_x, current_time);
    }

    // RETREAT: if (last unvalidated packet is ACKed), ssthresh=PS and then enter Normal
    if (resume_state->alg_state == picoquic_resume_alg_retreat)
    {
        /*  *Safe Retreat Phase (Receiving acknowledgement for all unvalidated
            packets): The sender enters Normal Phase when the last packet (or
            later) sent during the Unvalidated Phase has been acknowledged.
            The sender MUST set the ssthresh to no morethan the PipeSize. */
        /*  The sender leaves the Safe Retreat Phase when the last packet number
            (or higher) sent in the Unvalidated Phase is acknowledged. If the
            last packet number is not cumulatively acknowledged, then additional
            packets might need to be retransmitted. */
        //if (picoquic_cc_get_ack_number(path_x->cnx, path_x) >= resume_state->val_mark) // TODO last unvalidated packet is ACKed
        if (path_x->delivered >= resume_state->val_mark)
        {
            picoquic_resume_enter_normal(resume_state, path_x, current_time);
            // ssthresh=PS
            return resume_state->pipesize;
        }
    }

    //otherwise we return 0 aka we don't touch ssthresh
    return 0;
}

size_t picoquic_resume_send_packet(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t current_time) {
    //printf("\033[0;35m%s\tpicoquic_resume_send_packet(resume_state=%p, path_x=%p, current_time=%" PRIu64 ")\033[0m\n", (path_x->cnx->client_mode) ? "CLIENT" : "SERVER", resume_state, path_x, current_time);

    //  RECON: If (FS=CWND and CR confirmed), enter Unvaliding else enter Normal
    //  continues in a subsequent RTT to send more packets until the sender becomes CWND-limited (i.e., flight size=CWND)
    /*  *Reconnaissance Phase (Data-limited sender): If the sender is
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
    if (resume_state->alg_state == picoquic_resume_alg_recon && path_x->bytes_in_transit >= path_x->cwin) //  FS=CWND
    {
        // if saved_cwnd smaller than CWND avoid careful resume -> enter normal
        if (path_x->cwin >= resume_state->saved_cwnd)
        {
            picoquic_resume_enter_normal(resume_state, path_x, current_time);
            return 0;
        }

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
        if (path_x->rtt_min < resume_state->saved_rtt / 2 || path_x->rtt_min >= resume_state->saved_rtt * 10) // path_x->min_rtt_estimate_in_period?
        {
            picoquic_resume_enter_normal(resume_state, path_x, current_time);
            return 0;
        }

        // move to validating and update mark TODO move to validating, expect unval
        picoquic_resume_enter_unval(resume_state, path_x, current_time);
        return 0;
    }

    // UNVAL: If( >1 RTT has passed or FS=CWND or first unvalidated packet is ACKed), enter Validating
    if (resume_state->alg_state == picoquic_resume_alg_unval && (current_time - resume_state->start_of_epoch > path_x->rtt_min || path_x->bytes_in_transit >= path_x->cwin)) // >1 RTT has passed or FS=CWND
    {
        picoquic_resume_enter_validate(resume_state, path_x, current_time);
        return 0;
    }

    return 0;
}

unsigned int picoquic_resume_congestion_event(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t lost_packet_number, uint64_t current_time) {
    printf("\033[0;35m%s\tpicoquic_resume_congestion_event(resume_state=%p, path_x=%p, lost_packet_numer=%" PRIu64 ", current_time=%" PRIu64 ")\033[0m\n", (path_x->cnx->client_mode) ? "CLIENT" : "SERVER", resume_state, path_x, lost_packet_number, current_time);

    // RECON: Normal CC method CR is not allowed
    /* *Reconnaissance Phase (Detected congestion): If the sender detects
        congestion (e.g., packet loss or ECN-CE marking), the sender does
        not use the Careful Resume method and MUST enter the Normal Phase
        to respond to the detected congestion. */
    if (resume_state->alg_state == picoquic_resume_alg_recon)
    {
        picoquic_resume_enter_normal(resume_state, path_x, current_time);
        return 1; // TODO check
    }

    // UNVAL, VALIDATE: Enter Safe Retreat
    /*  *Validating Phase (Congestion indication): If a sender determines
        that congestion was experienced (e.g., packet loss or ECN-CE
        marking), Careful Resume enters the Safe Retreat Phase. */
    if (resume_state->alg_state == picoquic_resume_alg_unval || resume_state->alg_state == picoquic_resume_alg_validate)
    {
        picoquic_resume_enter_retreat(resume_state, path_x, current_time);
        //resume_state->recover = path_x->path_packet_last->sequence_number; // TODO check; vs path_x->path_packet_acked_number

        return 1;
    }

    return 0;
}

// On entry
void picoquic_resume_enter_recon(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t current_time)
{
    printf("\033[0;35m%s\tpicoquic_resume_enter_recon(resume_state=%p, path_x=%p, current_time=%" PRIu64 ")\033[0m\n", (path_x->cnx->client_mode) ? "CLIENT" : "SERVER", resume_state, path_x, current_time);
    resume_state->alg_state = picoquic_resume_alg_recon;

    resume_state->previous_start_of_epoch = resume_state->start_of_epoch;
    resume_state->start_of_epoch = current_time;

    // RECON: CWND=IW
    path_x->cwin = PICOQUIC_CWIN_INITIAL; // TODO check if cubic alredy sets cwin

    path_x->is_cr_data_updated = 1;
}

void picoquic_resume_enter_unval(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t current_time)
{
    printf("\033[0;35m%s\tpicoquic_resume_enter_unval(resume_state=%p, path_x=%p, current_time=%" PRIu64 ")\033[0m\n", (path_x->cnx->client_mode) ? "CLIENT" : "SERVER", resume_state, path_x, current_time);
    resume_state->alg_state = picoquic_resume_alg_unval;

    resume_state->previous_start_of_epoch = resume_state->start_of_epoch;
    resume_state->start_of_epoch = current_time;

    //resume_state->unval_mark = picoquic_cc_get_sequence_number(path_x->cnx, path_x); // or path_x->path_packet_last->sequence_number;? // TODO check sequence_number; vs path_x->path_packet_acked_number
    resume_state->unval_mark = path_x->bytes_sent;
    //resume_state->val_mark = ((resume_state->saved_cwnd / 2) - path_x->bytes_in_transit) / PICOQUIC_MAX_PACKET_SIZE; // TODO JOERG packet_sequence or bytes?
    resume_state->val_mark = ((resume_state->saved_cwnd / 2) - path_x->bytes_in_transit);
    // UNVAL: PS=CWND
    /*  *Unvalidated Phase (Initialising PipeSize): The variable PipeSize
        if initialised to CWND on entry to the Unvalidated Phase. This
        records the value before the jump is applied. */
    resume_state->pipesize = path_x->bytes_in_transit; // TODO why bytes_in_transit and not cwin?
    // UNVAL: CWND=jump_cwnd
    /*  *Unvalidated Phase (Setting the jump_cwnd): To avoid starving
        other flows that could have either started or increased their
        used capacity after the Observation Phase, the jump_cwnd MUST be
        no more than half of the saved_cwnd. Hence, jump_cwnd is less
        than or equal to the (saved_cwnd/2). CWND = jump_cwnd. */
    //uint64_t jump_cwnd = resume_state->saved_cwnd / 2;
    path_x->cwin = resume_state->saved_cwnd / 2; // - path_x->bytes_in_transit // TODO check

    path_x->is_cr_data_updated = 1;
}

void picoquic_resume_enter_validate(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t current_time)
{
    printf("\033[0;35m%s\tpicoquic_resume_enter_validate(resume_state=%p, path_x=%p, current_time=%" PRIu64 ")\033[0m\n", (path_x->cnx->client_mode) ? "CLIENT" : "SERVER", resume_state, path_x, current_time);
    resume_state->alg_state = picoquic_resume_alg_validate;

    resume_state->previous_start_of_epoch = resume_state->start_of_epoch;
    resume_state->start_of_epoch = current_time;

    // VALIDATE: CWND=FS
    /*  *Validating Phase (Limiting CWND on entry): On entry to the
        Validating Phase, the CWND is set to the flight size. */
    path_x->cwin = path_x->bytes_in_transit; // TODO JOERG cwin reduced?

    path_x->is_cr_data_updated = 1;
}

void picoquic_resume_enter_retreat(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t current_time)
{
    printf("\033[0;35m%s\tpicoquic_resume_enter_retreat(resume_state=%p, path_x=%p, current_time=%" PRIu64 ")\033[0m\n", (path_x->cnx->client_mode) ? "CLIENT" : "SERVER", resume_state, path_x, current_time);
    resume_state->alg_state = picoquic_resume_alg_retreat;

    resume_state->previous_start_of_epoch = resume_state->start_of_epoch;
    resume_state->start_of_epoch = current_time;

    // RETREAT: CWND=(PS/2)
    /*  *Safe Retreat Phase (Re-initializing CC): On entry, the CWND MUST
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
    path_x->cwin = (resume_state->pipesize / 2 >= PICOQUIC_CWIN_INITIAL) ? resume_state->pipesize / 2 : PICOQUIC_CWIN_INITIAL;

    path_x->is_cr_data_updated = 1;
}

void picoquic_resume_enter_normal(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t current_time)
{
    printf("\033[0;35m%s\tpicoquic_resume_enter_normal(resume_state=%p, path_x=%p, current_time=%" PRIu64 ")\033[0m\n", (path_x->cnx->client_mode) ? "CLIENT" : "SERVER", resume_state, path_x, current_time);
    resume_state->alg_state = picoquic_resume_alg_normal;

    resume_state->previous_start_of_epoch = resume_state->start_of_epoch;
    resume_state->start_of_epoch = current_time;

    path_x->is_cr_data_updated = 1;
}

void picoquic_resume_delete(picoquic_path_t* path_x)
{
    printf("\033[0;35m%s\tpicoquic_resume_delete(unique_path_id=%" PRIu64 ")\033[0m\n", (path_x->cnx->client_mode) ? "CLIENT" : "SERVER", path_x->unique_path_id);
    if (path_x->careful_resume_state != NULL) {
        free(path_x->congestion_alg_state);
        path_x->congestion_alg_state = NULL;
    }
}

void picoquic_resume_debug_printf(picoquic_resume_state_t resume_state)
{
    /* printf("\033[0;35m{enabled=%u, alg_state=%d,\nprevious_rtt=%" PRIu64 ", saved_cwnd=%lu,\ncr_mark=%" PRIu64 ", pipesize=%lu,\nrecover=%" PRIu64 "}\033[0m\n",
        resume_state.enabled, resume_state.alg_state, resume_state.saved_rtt, resume_state.saved_cwnd, resume_state.unval_mark,
        resume_state.pipesize, resume_state.recover);*/
}

// TODO path address vs unique path id in comments

