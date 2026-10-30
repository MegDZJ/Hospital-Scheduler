#include "test_util.h"
#include "statistics.h"

static Simulation sim;
static char err[200];

int main(void)
{
    Statistics s;
    int a, b, c;

    printf("Statistics tests\n");

    TEST("zero completed patients: no division by zero");
    init_simulation(&sim);
    compute_statistics(&sim, &s);
    CHECK(s.completed == 0 && !s.has_completed);
    CHECK(s.avg_waiting == 0.0 && s.avg_turnaround == 0.0);
    CHECK(!s.has_waiting);

    TEST("bed utilization");
    CHECK(calculate_bed_utilization(0, 10) == 0.0);
    CHECK(calculate_bed_utilization(4, 10) == 40.0);
    CHECK(calculate_bed_utilization(10, 10) == 100.0);
    CHECK(calculate_bed_utilization(1, 0) == 0.0);

    TEST("waiting / turnaround / response time of a finished patient");
    init_simulation(&sim);
    /* ten long patients, then one fever patient who must wait */
    for (a = 0; a < 10; a++)
        reg(&sim, "Long", 30, COND_ASTHMA, T(9, 0));        /* 60 min */
    b = reg(&sim, "Waiter", 30, COND_FEVER, T(9, 5));       /* arrives 9:05 */
    advance_simulation(&sim, 60, err, sizeof err);           /* -> 10:00 */
    /* beds free at 10:00; waiter starts 10:00, ends 10:20 */
    CHECK(sim.patients[b].start_time == T(10, 0));
    CHECK(calculate_waiting_time(&sim.patients[b], sim.now) == 55);
    CHECK(calculate_response_time(&sim.patients[b]) == 55);
    CHECK(calculate_turnaround_time(&sim.patients[b]) == -1); /* unfinished */
    advance_simulation(&sim, 20, err, sizeof err);
    CHECK(sim.patients[b].status == PATIENT_COMPLETED);
    CHECK(calculate_turnaround_time(&sim.patients[b]) == 75);  /* 10:20-9:05 */

    TEST("current waiting time of a waiting patient grows with the clock");
    init_simulation(&sim);
    for (a = 0; a < 10; a++)
        reg(&sim, "Long", 30, COND_FRACTURE, T(9, 0));
    b = reg(&sim, "Waiter", 30, COND_FEVER, T(9, 10));
    CHECK(calculate_waiting_time(&sim.patients[b], sim.now) == -1); /* future */
    advance_simulation(&sim, 25, err, sizeof err);
    CHECK(calculate_waiting_time(&sim.patients[b], sim.now) == 15);

    TEST("averages cover only completed patients");
    init_simulation(&sim);
    a = reg(&sim, "Quick", 30, COND_CONSULT, T(9, 0));      /* 15 min, wait 0  */
    b = reg(&sim, "Quick2", 30, COND_FEVER, T(9, 0));       /* 20 min, wait 0  */
    c = reg(&sim, "Slow", 30, COND_CARDIAC, T(9, 0));       /* still in bed    */
    advance_simulation(&sim, 20, err, sizeof err);
    compute_statistics(&sim, &s);
    CHECK(s.completed == 2 && s.in_bed == 1);
    CHECK(s.has_completed);
    CHECK(s.avg_waiting == 0.0);
    CHECK(s.avg_turnaround == 17.5);         /* (15 + 20) / 2, Slow excluded */
    CHECK(s.avg_response == 0.0);
    CHECK(s.occupied_beds == 1 && s.available_beds == 9);
    CHECK(s.utilization == 10.0);
    (void)a; (void)b; (void)c;

    TEST("maximum current waiting time");
    init_simulation(&sim);
    for (a = 0; a < 10; a++)
        reg(&sim, "Long", 30, COND_FRACTURE, T(9, 0));
    reg(&sim, "W1", 30, COND_FEVER, T(9, 0));
    reg(&sim, "W2", 30, COND_FEVER, T(9, 20));
    advance_simulation(&sim, 40, err, sizeof err);
    compute_statistics(&sim, &s);
    CHECK(s.has_waiting && s.waiting == 2);
    CHECK(s.max_waiting == 40);              /* W1: 9:00 -> 9:40 */

    TEST_DONE("test_statistics");
}
