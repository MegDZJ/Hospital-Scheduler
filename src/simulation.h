/*
 * simulation.h - the whole hospital state plus the operations that change it.
 * Every state transition (register, arrive, allocate, complete, advance,
 * reset) happens in simulation.c; the browser only displays the result.
 */
#ifndef SIMULATION_H
#define SIMULATION_H

#include <stddef.h>
#include "config.h"
#include "patient.h"
#include "bed.h"

typedef enum { SIM_NOT_STARTED = 0, SIM_RUNNING = 1, SIM_PAUSED = 2 } SimStatus;

typedef struct {
    int  id;                       /* increasing, lets the UI show only new */
    int  time;                     /* simulated minute it happened          */
    char text[EVENT_TEXT_LEN];
} SimEvent;

typedef struct {
    int       now;                 /* simulated minutes after midnight      */
    int       start_time;          /* the clock value reset returns to      */
    SimStatus status;
    int       aging_enabled;       /* configuration, survives reset         */
    int       tick_seconds;        /* configuration, survives reset         */
    int       next_sequence;       /* registration sequence counter         */

    int       patient_count;
    Patient   patients[MAX_PATIENTS];
    Bed       beds[NUM_BEDS];

    SimEvent  events[MAX_EVENTS];
    int       event_count;
    int       next_event_id;
} Simulation;

/* Returns a fresh simulation (aging off, default tick). */
void init_simulation(Simulation *sim);

/* Back to the documented initial state: clock = start_time, no patients,
 * all beds available, NOT_STARTED, ID counter restarted.  aging_enabled and
 * tick_seconds are configuration and are kept. */
void reset_simulation(Simulation *sim);

/* Validates, stores the patient (always at the end of the array so indices
 * stay stable) and immediately tries to allocate beds.
 * Returns 1 on success and the new patient's index in *out_index. */
int register_patient(Simulation *sim, const PatientInput *in,
                     int *out_index, char *err, size_t errlen);

/* Moves the clock forward by `minutes`, processing every arrival and
 * completion on the way in chronological order. Returns 1 on success. */
int advance_simulation(Simulation *sim, int minutes, char *err, size_t errlen);

int start_simulation(Simulation *sim, char *err, size_t errlen);
int pause_simulation(Simulation *sim, char *err, size_t errlen);
int resume_simulation(Simulation *sim, char *err, size_t errlen);

/* Called by the server timer. Advances one minute while RUNNING. */
void auto_tick(Simulation *sim);

/* Removes all patient records. Refused while any bed is occupied. */
int clear_patients(Simulation *sim, char *err, size_t errlen);

/* Resets, then registers the demonstration patients. Returns the number of
 * patients loaded, or -1 on failure. */
int load_demo(Simulation *sim, char *err, size_t errlen);

void set_aging(Simulation *sim, int enabled);

/* Number of patients the demo dataset contains. */
int demo_patient_count(void);

#endif
