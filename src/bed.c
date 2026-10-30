#include "bed.h"
#include "conditions.h"
#include <stdio.h>
#include <string.h>

static void clear_bed(Bed *bed)
{
    bed->status = BED_AVAILABLE;
    bed->patient_index = -1;
    bed->patient_id[0] = '\0';
    bed->condition_id = -1;
    bed->priority = 0;
    bed->start_time = -1;
    bed->expected_completion = -1;
}

void init_beds(Bed beds[])
{
    int i;
    for (i = 0; i < NUM_BEDS; i++) {
        beds[i].id = i + 1;
        clear_bed(&beds[i]);
    }
}

int find_free_bed(const Bed beds[])
{
    int i;
    for (i = 0; i < NUM_BEDS; i++) {
        if (beds[i].status == BED_AVAILABLE)
            return i;
    }
    return -1;
}

int count_occupied_beds(const Bed beds[])
{
    int i, n = 0;
    for (i = 0; i < NUM_BEDS; i++) {
        if (beds[i].status == BED_OCCUPIED)
            n++;
    }
    return n;
}

int assign_patient_to_bed(Bed beds[], int bed_index, Patient *p,
                          int patient_index, int now,
                          char *err, size_t errlen)
{
    const Condition *cond;
    int i;

    if (bed_index < 0 || bed_index >= NUM_BEDS) {
        snprintf(err, errlen, "Invalid bed");
        return 0;
    }
    if (beds[bed_index].status != BED_AVAILABLE) {
        snprintf(err, errlen, "Bed %d is already occupied", bed_index + 1);
        return 0;
    }
    if (!can_transition(p->status, PATIENT_IN_BED)) {
        snprintf(err, errlen, "Patient %s is not waiting for a bed", p->id);
        return 0;
    }
    if (p->bed_id != 0) {
        snprintf(err, errlen, "Patient %s already occupies a bed", p->id);
        return 0;
    }
    for (i = 0; i < NUM_BEDS; i++) {
        if (beds[i].status == BED_OCCUPIED && beds[i].patient_index == patient_index) {
            snprintf(err, errlen, "Patient %s already occupies a bed", p->id);
            return 0;
        }
    }
    cond = get_condition(p->condition_id);
    if (cond == NULL) {
        snprintf(err, errlen, "Patient has an unknown condition");
        return 0;
    }

    beds[bed_index].status = BED_OCCUPIED;
    beds[bed_index].patient_index = patient_index;
    snprintf(beds[bed_index].patient_id, sizeof beds[bed_index].patient_id,
             "%s", p->id);
    beds[bed_index].condition_id = p->condition_id;
    beds[bed_index].priority = p->priority;
    beds[bed_index].start_time = now;
    beds[bed_index].expected_completion = now + cond->duration_minutes;

    p->status = PATIENT_IN_BED;
    p->bed_id = beds[bed_index].id;
    p->start_time = now;
    p->expected_completion = beds[bed_index].expected_completion;
    return 1;
}

int release_bed(Bed beds[], int bed_index, Patient *p, int now,
                char *err, size_t errlen)
{
    if (bed_index < 0 || bed_index >= NUM_BEDS ||
        beds[bed_index].status != BED_OCCUPIED) {
        snprintf(err, errlen, "Bed is not occupied");
        return 0;
    }
    if (now < beds[bed_index].expected_completion) {
        snprintf(err, errlen, "Bed %d cannot be released before its expected "
                 "completion time", bed_index + 1);
        return 0;
    }
    if (!can_transition(p->status, PATIENT_COMPLETED)) {
        snprintf(err, errlen, "Patient %s is not in a bed", p->id);
        return 0;
    }

    p->status = PATIENT_COMPLETED;
    p->completion_time = beds[bed_index].expected_completion;
    clear_bed(&beds[bed_index]);
    return 1;
}
