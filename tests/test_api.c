/* API tests call api_handle_request() directly (no sockets needed). */
#include "test_util.h"
#include "api.h"
#include "statistics.h"

static Simulation sim;

static int has(const ApiResponse *r, const char *needle)
{
    return strstr(r->body, needle) != NULL;
}

/* Performs a request and returns the HTTP status; keeps the last body. */
static ApiResponse last;

static int call(const char *method, const char *path, const char *body)
{
    if (last.body)
        api_response_free(&last);
    api_handle_request(&sim, method, path, body, body ? strlen(body) : 0, &last);
    return last.status;
}

static const char *GOOD =
    "{\"name\":\"  Asha Rao \",\"age\":40,\"gender\":\"female\","
    "\"condition_id\":3,\"arrival_time\":\"09:00\"}";

int main(void)
{
    printf("API tests\n");
    init_simulation(&sim);

    TEST("GET endpoints return valid success envelopes");
    CHECK(call("GET", "/api/state", NULL) == 200);
    CHECK(has(&last, "\"success\":true") && has(&last, "\"time_str\":\"09:00\""));
    CHECK(has(&last, "\"status\":\"not_started\""));
    CHECK(call("GET", "/api/beds", NULL) == 200);
    CHECK(has(&last, "\"id\":10"));
    CHECK(call("GET", "/api/patients", NULL) == 200);
    CHECK(call("GET", "/api/statistics", NULL) == 200);
    CHECK(has(&last, "\"avg_waiting_time\":null"));
    CHECK(call("GET", "/api/conditions", NULL) == 200);
    CHECK(has(&last, "Mild fever") && has(&last, "Serious cardiac condition"));

    TEST("valid registration (name trimmed, priority from the server)");
    CHECK(call("POST", "/api/patients", GOOD) == 201);
    CHECK(has(&last, "\"id\":\"PAT-0001\""));
    CHECK(has(&last, "\"name\":\"Asha Rao\""));
    CHECK(has(&last, "\"priority\":4"));
    CHECK(has(&last, "\"bed_assigned\":true"));

    TEST("invalid age");
    CHECK(call("POST", "/api/patients",
        "{\"name\":\"A\",\"age\":-1,\"gender\":\"male\",\"condition_id\":0,"
        "\"arrival_time\":\"09:00\"}") == 400);
    CHECK(has(&last, "\"success\":false"));
    CHECK(call("POST", "/api/patients",
        "{\"name\":\"A\",\"age\":121,\"gender\":\"male\",\"condition_id\":0,"
        "\"arrival_time\":\"09:00\"}") == 400);
    CHECK(call("POST", "/api/patients",
        "{\"name\":\"A\",\"age\":\"forty\",\"gender\":\"male\",\"condition_id\":0,"
        "\"arrival_time\":\"09:00\"}") == 400);
    CHECK(call("POST", "/api/patients",
        "{\"name\":\"A\",\"age\":4.5,\"gender\":\"male\",\"condition_id\":0,"
        "\"arrival_time\":\"09:00\"}") == 400);

    TEST("invalid condition");
    CHECK(call("POST", "/api/patients",
        "{\"name\":\"A\",\"age\":30,\"gender\":\"male\",\"condition_id\":99,"
        "\"arrival_time\":\"09:00\"}") == 400);
    CHECK(has(&last, "Unsupported condition"));

    TEST("missing fields");
    CHECK(call("POST", "/api/patients", "{\"name\":\"A\"}") == 400);
    CHECK(has(&last, "Missing required field"));
    CHECK(call("POST", "/api/patients",
        "{\"name\":\"   \",\"age\":30,\"gender\":\"male\",\"condition_id\":0,"
        "\"arrival_time\":\"09:00\"}") == 400);

    TEST("malformed JSON");
    CHECK(call("POST", "/api/patients", "{\"name\":") == 400);
    CHECK(has(&last, "Invalid JSON"));
    CHECK(call("POST", "/api/patients", "") == 400);

    TEST("a client cannot submit its own priority");
    CHECK(call("POST", "/api/patients",
        "{\"name\":\"A\",\"age\":30,\"gender\":\"male\",\"condition_id\":0,"
        "\"arrival_time\":\"09:00\",\"priority\":99}") == 400);
    CHECK(has(&last, "calculated by the server"));
    CHECK(sim.patient_count == 1);

    TEST("arrival time in the past / bad format");
    CHECK(call("POST", "/api/simulation/advance", "{\"minutes\":10}") == 200);
    CHECK(call("POST", "/api/patients",
        "{\"name\":\"A\",\"age\":30,\"gender\":\"male\",\"condition_id\":0,"
        "\"arrival_time\":\"09:05\"}") == 400);
    CHECK(call("POST", "/api/patients",
        "{\"name\":\"A\",\"age\":30,\"gender\":\"male\",\"condition_id\":0,"
        "\"arrival_time\":\"25:00\"}") == 400);

    TEST("context-dependent condition needs confirmation, not a gender");
    CHECK(call("POST", "/api/patients",
        "{\"name\":\"B\",\"age\":30,\"gender\":\"male\",\"condition_id\":6,"
        "\"arrival_time\":\"09:10\"}") == 400);
    CHECK(call("POST", "/api/patients",
        "{\"name\":\"B\",\"age\":30,\"gender\":\"female\",\"condition_id\":6,"
        "\"arrival_time\":\"09:10\",\"clinical_context_confirmed\":true}") == 201);

    TEST("HTML in names is stored as data and JSON-escaped");
    CHECK(call("POST", "/api/patients",
        "{\"name\":\"<b>\\\"x\\\"</b>\",\"age\":30,\"gender\":\"other\","
        "\"condition_id\":0,\"arrival_time\":\"09:10\"}") == 201);
    CHECK(has(&last, "\\\"x\\\""));

    TEST("simulation controls and invalid advances");
    CHECK(call("POST", "/api/simulation/advance", "{\"minutes\":-5}") == 400);
    CHECK(call("POST", "/api/simulation/advance", "{\"minutes\":9999}") == 400);
    CHECK(call("POST", "/api/simulation/advance", "{}") == 400);
    CHECK(call("POST", "/api/simulation/advance", "garbage") == 400);
    CHECK(call("POST", "/api/simulation/pause", NULL) == 409);
    CHECK(call("POST", "/api/simulation/start", NULL) == 200);
    CHECK(call("POST", "/api/simulation/start", NULL) == 409);
    CHECK(call("POST", "/api/simulation/pause", NULL) == 200);
    CHECK(call("POST", "/api/simulation/resume", NULL) == 200);

    TEST("routing: 404 and 405");
    CHECK(call("GET", "/api/nothing", NULL) == 404);
    CHECK(call("GET", "/api/simulation/advance", NULL) == 405);
    CHECK(strcmp(last.allow, "POST") == 0);
    CHECK(call("DELETE", "/api/patients", NULL) == 405);

    TEST("clear refused while beds are occupied, then reset works");
    CHECK(call("POST", "/api/patients/clear", NULL) == 409);
    CHECK(call("POST", "/api/simulation/reset", NULL) == 200);
    CHECK(sim.patient_count == 0 && sim.now == T(9, 0));
    CHECK(call("POST", "/api/patients/clear", NULL) == 200);

    TEST("aging setting");
    CHECK(call("POST", "/api/settings/aging", "{\"enabled\":true}") == 200);
    CHECK(sim.aging_enabled == 1);
    CHECK(call("POST", "/api/settings/aging", "{\"enabled\":\"yes\"}") == 400);
    CHECK(call("POST", "/api/settings/aging", "{\"enabled\":false}") == 200);

    TEST("end-to-end acceptance scenario");
    CHECK(call("POST", "/api/demo/load", NULL) == 200);
    CHECK(sim.now == T(9, 0));
    /* priorities calculated by C: Ravi 72/cardiac => 7 */
    CHECK(call("GET", "/api/patients", NULL) == 200);
    CHECK(has(&last, "\"name\":\"Ravi Kumar\""));
    {
        Statistics s;
        int i;
        compute_statistics(&sim, &s);
        CHECK(sim.patients[0].priority == 7);
        CHECK(s.occupied_beds == 10 && s.in_bed == 10);
        CHECK(s.waiting == 2);                    /* the two priority-1 */
        CHECK(s.scheduled == demo_patient_count() - 12);
        for (i = 0; i < sim.patient_count; i++) {
            if (sim.patients[i].status == PATIENT_SCHEDULED)
                CHECK(sim.patients[i].bed_id == 0);   /* no early beds */
        }
        /* Neha and Karan (priority 1) are the waiters, FCFS: Neha first */
        CHECK(strcmp(sim.patients[9].name, "Neha Gupta") == 0);
        CHECK(sim.patients[9].status == PATIENT_WAITING);
        CHECK(sim.patients[10].status == PATIENT_WAITING);
    }
    CHECK(call("POST", "/api/simulation/advance", "{\"minutes\":15}") == 200);
    CHECK(has(&last, "\"time_str\":\"09:15\""));
    {
        /* Maria Garcia (15 min consultation) completed exactly at 09:15 and
         * Fatima Noor (priority 4, arrived 09:10) took her bed at 09:15. */
        const Patient *maria = &sim.patients[7];
        const Patient *fatima = &sim.patients[12];
        CHECK(maria->status == PATIENT_COMPLETED);
        CHECK(maria->completion_time == T(9, 15));
        CHECK(fatima->status == PATIENT_IN_BED);
        CHECK(fatima->start_time == T(9, 15));
        CHECK(fatima->bed_id == maria->bed_id);
        CHECK(calculate_waiting_time(fatima, sim.now) == 5);
        CHECK(calculate_turnaround_time(maria) == 15);
        CHECK(sim.patients[9].status == PATIENT_WAITING);    /* Neha still */
    }
    /* pause: the clock must not change, even with polling */
    CHECK(call("POST", "/api/simulation/start", NULL) == 200);
    CHECK(call("POST", "/api/simulation/pause", NULL) == 200);
    CHECK(call("GET", "/api/state", NULL) == 200);
    CHECK(call("GET", "/api/state", NULL) == 200);
    CHECK(sim.now == T(9, 15));
    CHECK(call("POST", "/api/simulation/resume", NULL) == 200);
    CHECK(call("POST", "/api/simulation/advance", "{\"minutes\":30}") == 200);
    CHECK(sim.now == T(9, 45));
    CHECK(call("POST", "/api/simulation/reset", NULL) == 200);
    CHECK(sim.now == T(9, 0) && sim.patient_count == 0);
    CHECK(count_occupied_beds(sim.beds) == 0);
    CHECK(sim.status == SIM_NOT_STARTED);

    if (last.body)
        api_response_free(&last);
    TEST_DONE("test_api");
}
