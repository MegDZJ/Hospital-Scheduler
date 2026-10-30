/*
 * bed.h - the fixed set of hospital beds and the rules for using them.
 */
#ifndef BED_H
#define BED_H

#include <stddef.h>
#include "patient.h"

typedef enum { BED_AVAILABLE = 0, BED_OCCUPIED = 1 } BedStatus;

typedef struct {
    int       id;                    /* 1..NUM_BEDS                         */
    BedStatus status;
    int       patient_index;         /* index into the patient array, -1    */
    char      patient_id[16];        /* "" when available                   */
    int       condition_id;          /* -1 when available                   */
    int       priority;              /* original score of the occupant      */
    int       start_time;            /* -1 when available                   */
    int       expected_completion;   /* -1 when available                   */
} Bed;

void init_beds(Bed beds[]);

/* Lowest-numbered available bed (array index), or -1 if all are taken. */
int find_free_bed(const Bed beds[]);

int count_occupied_beds(const Bed beds[]);

/*
 * Gives beds[bed_index] to the patient at time `now`.
 * Refuses (returns 0 and fills err) if the bed is occupied, the patient is
 * not WAITING, or the patient already holds a bed.  Returns 1 on success.
 */
int assign_patient_to_bed(Bed beds[], int bed_index, Patient *p,
                          int patient_index, int now,
                          char *err, size_t errlen);

/*
 * Releases an occupied bed and marks its patient COMPLETED.  Refuses to
 * release before the expected completion time.  Returns 1 on success.
 */
int release_bed(Bed beds[], int bed_index, Patient *p, int now,
                char *err, size_t errlen);

#endif
