/*
 * patient.h - the Patient record, its lifecycle states, input validation
 * and small time helpers (time is always integer minutes after midnight).
 */
#ifndef PATIENT_H
#define PATIENT_H

#include <stddef.h>
#include "config.h"

typedef enum { GENDER_MALE = 0, GENDER_FEMALE = 1, GENDER_OTHER = 2 } Gender;

typedef enum {
    PATIENT_SCHEDULED = 0,   /* registered, arrival time still in future */
    PATIENT_WAITING   = 1,   /* arrived, in the priority queue           */
    PATIENT_IN_BED    = 2,   /* occupying a bed                          */
    PATIENT_COMPLETED = 3    /* occupancy finished, bed released         */
} PatientStatus;

typedef struct {
    char          id[16];                  /* "PAT-0001"                  */
    char          name[MAX_NAME_LEN + 1];
    int           age;
    Gender        gender;
    int           condition_id;
    int           context_confirmed;
    int           arrival_time;            /* minutes after midnight      */
    int           registration_seq;        /* 1, 2, 3 ... (FCFS tiebreak) */
    int           severity;                /* from the condition registry */
    int           age_adjustment;
    int           priority;                /* ORIGINAL score, never edited*/
    PatientStatus status;
    int           bed_id;                  /* 1..NUM_BEDS, 0 = none       */
    int           start_time;              /* -1 until a bed is assigned  */
    int           expected_completion;     /* -1 until a bed is assigned  */
    int           completion_time;         /* -1 until completed          */
} Patient;

/* What a client may submit.  There is deliberately NO priority field. */
typedef struct {
    char   name[MAX_NAME_LEN + 1];
    int    age;
    Gender gender;
    int    condition_id;
    int    context_confirmed;
    int    arrival_time;
} PatientInput;

/* Removes leading/trailing whitespace in place. */
void trim_name(char *name);

/* Returns 1 if the input is valid at simulated time `now`, else 0 + err. */
int validate_patient_input(const PatientInput *in, int earliest_arrival,
                           char *err, size_t errlen);

/* Lifecycle rule: SCHEDULED->WAITING->IN_BED->COMPLETED only. */
int can_transition(PatientStatus from, PatientStatus to);

const char *gender_name(Gender g);
const char *status_name(PatientStatus s);      /* machine name, lower case */
const char *status_label(PatientStatus s);     /* display name             */

/* "HH:MM" helpers.  out must hold at least 6 bytes. */
void format_time(int minutes, char *out);
int  parse_time(const char *text, int *minutes);   /* 1 ok, 0 invalid */

#endif
