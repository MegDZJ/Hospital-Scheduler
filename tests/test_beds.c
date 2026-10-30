#include "test_util.h"
#include "bed.h"

static Patient mk(int seq, int cond)
{
    Patient p;
    memset(&p, 0, sizeof p);
    snprintf(p.id, sizeof p.id, "PAT-%04d", seq);
    p.registration_seq = seq;
    p.condition_id = cond;
    p.priority = 3;
    p.status = PATIENT_WAITING;
    p.start_time = p.expected_completion = p.completion_time = -1;
    return p;
}

int main(void)
{
    Bed beds[NUM_BEDS];
    Patient p1, p2, p3;
    char err[100];
    int i;

    printf("Bed tests\n");

    TEST("all 10 beds start available");
    init_beds(beds);
    CHECK(NUM_BEDS == 10);
    for (i = 0; i < NUM_BEDS; i++) {
        CHECK(beds[i].status == BED_AVAILABLE);
        CHECK(beds[i].id == i + 1);
    }
    CHECK(count_occupied_beds(beds) == 0);

    TEST("a patient can be assigned to an available bed");
    p1 = mk(1, COND_FEVER);
    CHECK(assign_patient_to_bed(beds, 0, &p1, 0, T(9, 0), err, sizeof err));
    CHECK(beds[0].status == BED_OCCUPIED);
    CHECK(p1.status == PATIENT_IN_BED && p1.bed_id == 1);
    CHECK(beds[0].start_time == T(9, 0));
    CHECK(beds[0].expected_completion == T(9, 20));

    TEST("the same patient cannot occupy two beds");
    CHECK(!assign_patient_to_bed(beds, 1, &p1, 0, T(9, 0), err, sizeof err));
    p1.status = PATIENT_WAITING;           /* try to cheat the status check */
    CHECK(!assign_patient_to_bed(beds, 1, &p1, 0, T(9, 0), err, sizeof err));
    CHECK(beds[1].status == BED_AVAILABLE);
    p1.status = PATIENT_IN_BED;

    TEST("an occupied bed cannot be allocated again");
    p2 = mk(2, COND_CONSULT);
    CHECK(!assign_patient_to_bed(beds, 0, &p2, 1, T(9, 0), err, sizeof err));
    CHECK(p2.status == PATIENT_WAITING);
    CHECK(strcmp(beds[0].patient_id, "PAT-0001") == 0);

    TEST("allocation uses the lowest-numbered available bed");
    init_beds(beds);
    p1 = mk(1, COND_FEVER); p2 = mk(2, COND_FEVER); p3 = mk(3, COND_FEVER);
    assign_patient_to_bed(beds, find_free_bed(beds), &p1, 0, 0, err, sizeof err);
    assign_patient_to_bed(beds, find_free_bed(beds), &p2, 1, 0, err, sizeof err);
    assign_patient_to_bed(beds, find_free_bed(beds), &p3, 2, 0, err, sizeof err);
    CHECK(p1.bed_id == 1 && p2.bed_id == 2 && p3.bed_id == 3);
    /* free bed 2 (index 1): it must be the next one chosen */
    p2.completion_time = -1;
    CHECK(release_bed(beds, 1, &p2, T(0, 20), err, sizeof err));
    CHECK(find_free_bed(beds) == 1);

    TEST("a bed cannot be released early; completion releases it");
    CHECK(!release_bed(beds, 0, &p1, T(0, 19), err, sizeof err));
    CHECK(beds[0].status == BED_OCCUPIED && p1.status == PATIENT_IN_BED);
    CHECK(release_bed(beds, 0, &p1, T(0, 20), err, sizeof err));
    CHECK(beds[0].status == BED_AVAILABLE && beds[0].patient_id[0] == '\0');
    CHECK(p1.status == PATIENT_COMPLETED && p1.completion_time == T(0, 20));
    CHECK(count_occupied_beds(beds) == 1);

    TEST("a full hospital has no free bed");
    init_beds(beds);
    for (i = 0; i < NUM_BEDS; i++) {
        Patient p = mk(i + 1, COND_ASTHMA);
        CHECK(assign_patient_to_bed(beds, i, &p, i, 0, err, sizeof err));
    }
    CHECK(find_free_bed(beds) == -1);

    TEST_DONE("test_beds");
}
