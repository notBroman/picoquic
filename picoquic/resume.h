#ifndef RESUME_H
#define RESUME_H

#define PICOQUIC_MAX_JUMP 7 // TODO

typedef enum {
    picoquic_resume_alg_observe = 0,
    picoquic_resume_alg_recon, // = 1,
    picoquic_resume_alg_unval, // = 2,
    picoquic_resume_alg_validate, // = 3,
    picoquic_resume_alg_retreat, // = 4,
    picoquic_resume_alg_normal = 100
} picoquic_resume_alg_state_t;

typedef struct st_picoquic_resume_state_t {
    unsigned int enabled : 1; /* careful resume enabled for current connection */

    picoquic_resume_alg_state_t alg_state; /* current state of the careful resume algorithm */

    uint64_t saved_rtt; /* observed RTT from previous connection */
    uint64_t saved_cwnd; /* observed CWND from previous connection */

    uint64_t unval_mark;
    uint64_t val_mark;
    uint64_t pipesize; /* pipesize */
    //uint64_t recover;

    uint64_t start_of_epoch; /* start timestamp of current state */
    uint64_t previous_start_of_epoch; /* start timestamp of previous state */
} picoquic_resume_state_t;

void picoquic_resume_reset(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, unsigned int enabled, uint64_t current_time);
void picoquic_resume_init(picoquic_cnx_t* cnx, picoquic_path_t* path_x, unsigned int enabled, uint64_t current_time);

int picoquic_resume_enabled(picoquic_cnx_t* cnx, picoquic_path_t* path_x);

size_t picoquic_resume_process_ack(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t nb_bytes_acknowledged, uint64_t current_time);
size_t picoquic_resume_send_packet(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t current_time);
unsigned int picoquic_resume_congestion_event(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t lost_packet_number, uint64_t current_time);

void picoquic_resume_enter_recon(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t current_time);
void picoquic_resume_enter_unval(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t current_time);
void picoquic_resume_enter_validate(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t current_time);
void picoquic_resume_enter_retreat(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t current_time);
void picoquic_resume_enter_normal(picoquic_resume_state_t* resume_state, picoquic_path_t* path_x, uint64_t current_time);

void picoquic_resume_delete(picoquic_path_t* path_x);

#endif //RESUME_H
