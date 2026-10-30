#include "test_util.h"
#include "queue.h"
#include "priority.h"

/* Build a waiting patient directly (no beds involved). */
static Patient mk(int seq, int priority, int arrival, PatientStatus st)
{
    Patient p;
    memset(&p, 0, sizeof p);
    snprintf(p.id, sizeof p.id, "PAT-%04d", seq);
    p.registration_seq = seq;
    p.priority = priority;
    p.arrival_time = arrival;
    p.status = st;
    return p;
}

int main(void)
{
    Patient list[5];
    int order[5];
    int n;

    printf("Queue tests\n");

    TEST("higher priority is selected first");
    list[0] = mk(1, 3, T(9, 0), PATIENT_WAITING);
    list[1] = mk(2, 5, T(9, 0), PATIENT_WAITING);
    list[2] = mk(3, 4, T(9, 0), PATIENT_WAITING);
    CHECK(get_next_waiting_patient(list, 3, T(9, 0), 0) == 1);
    n = build_waiting_queue(list, 3, T(9, 0), 0, order);
    CHECK(n == 3 && order[0] == 1 && order[1] == 2 && order[2] == 0);

    TEST("equal priority: earlier arrival wins (FCFS)");
    list[0] = mk(1, 4, T(9, 20), PATIENT_WAITING);
    list[1] = mk(2, 4, T(9, 10), PATIENT_WAITING);
    CHECK(get_next_waiting_patient(list, 2, T(9, 30), 0) == 1);

    TEST("equal priority and arrival: registration order wins");
    list[0] = mk(7, 4, T(9, 0), PATIENT_WAITING);
    list[1] = mk(5, 4, T(9, 0), PATIENT_WAITING);
    list[2] = mk(6, 4, T(9, 0), PATIENT_WAITING);
    n = build_waiting_queue(list, 3, T(9, 0), 0, order);
    CHECK(order[0] == 1 && order[1] == 2 && order[2] == 0);

    TEST("future arrivals cannot be selected");
    list[0] = mk(1, 5, T(10, 0), PATIENT_SCHEDULED);
    list[1] = mk(2, 1, T(9, 0), PATIENT_WAITING);
    CHECK(get_next_waiting_patient(list, 2, T(9, 0), 0) == 1);
    list[1].status = PATIENT_IN_BED;
    CHECK(get_next_waiting_patient(list, 2, T(9, 0), 0) == -1);
    /* even a (wrongly) WAITING record with a future arrival is not eligible */
    list[0].status = PATIENT_WAITING;
    CHECK(get_next_waiting_patient(list, 1, T(9, 0), 0) == -1);

    TEST("aging changes the order when enabled");
    list[0] = mk(1, 5, T(8, 45), PATIENT_WAITING);  /* high, just arrived */
    list[1] = mk(2, 3, T(8, 0), PATIENT_WAITING);   /* low, waited long   */
    /* 08:45: waits are 0 and 45 min -> effective 5 vs 4: patient 1 first   */
    CHECK(get_next_waiting_patient(list, 2, T(8, 45), 1) == 0);
    /* 09:30: waits 45 and 90 min -> 6 vs 6, tie -> earlier arrival (2)     */
    CHECK(patient_effective_priority(&list[1], T(9, 30), 1) == 3 + 90 / 30);
    CHECK(get_next_waiting_patient(list, 2, T(9, 30), 1) == 1);
    /* without aging the original scores decide: patient 1 stays first      */
    CHECK(get_next_waiting_patient(list, 2, T(9, 30), 0) == 0);

    TEST("aging does not change the original score");
    CHECK(list[1].priority == 3);
    (void)patient_effective_priority(&list[1], T(12, 0), 1);
    CHECK(list[1].priority == 3);

    TEST("empty queue returns -1");
    CHECK(get_next_waiting_patient(list, 0, T(9, 0), 0) == -1);

    TEST_DONE("test_queue");
}
