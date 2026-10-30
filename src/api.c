#include "api.h"
#include "conditions.h"
#include "json_utils.h"
#include "priority.h"
#include "queue.h"
#include "statistics.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* Response helpers                                                    */
/* ================================================================== */

static void set_body(ApiResponse *r, int status, JsonBuf *jb)
{
    if (jb->oom || jb->data == NULL) {
        r->status = 500;
        r->body = strdup("{\"success\":false,\"error\":\"Out of memory\"}");
    } else {
        r->status = status;
        r->body = jb->data;           /* ownership moves to the response */
        jb->data = NULL;
    }
    jb_free(jb);
}

static void respond_error(ApiResponse *r, int status, const char *message)
{
    JsonBuf jb;
    jb_init(&jb);
    jb_begin_object(&jb, NULL);
    jb_field_bool(&jb, "success", 0);
    jb_field_str(&jb, "error", message);
    jb_end_object(&jb);
    set_body(r, status, &jb);
}

/* `data` already holds a complete JSON value; it is freed here. */
static void respond_ok(ApiResponse *r, int status, const char *message,
                       JsonBuf *data)
{
    JsonBuf jb;
    jb_init(&jb);
    jb_begin_object(&jb, NULL);
    jb_field_bool(&jb, "success", 1);
    jb_field_str(&jb, "message", message);
    if (data != NULL) {
        jb_raw(&jb, ",\"data\":");
        jb_raw(&jb, jb_cstr(data));
        if (data->oom)
            jb.oom = 1;
        jb_free(data);
    }
    jb_end_object(&jb);
    set_body(r, status, &jb);
}

void api_response_free(ApiResponse *resp)
{
    free(resp->body);
    resp->body = NULL;
}

/* ================================================================== */
/* JSON writers for the domain objects                                 */
/* ================================================================== */

static void write_patient(JsonBuf *jb, const Simulation *sim, int idx,
                          int queue_position)
{
    const Patient *p = &sim->patients[idx];
    const Condition *c = get_condition(p->condition_id);

    jb_begin_object(jb, NULL);
    jb_field_str(jb, "id", p->id);
    jb_field_str(jb, "name", p->name);
    jb_field_int(jb, "age", p->age);
    jb_field_str(jb, "gender", gender_name(p->gender));
    jb_field_int(jb, "condition_id", p->condition_id);
    jb_field_str(jb, "condition", c ? c->name : "Unknown");
    jb_field_time(jb, "arrival_time", p->arrival_time);
    jb_field_int(jb, "registration_seq", p->registration_seq);
    jb_field_int(jb, "severity", p->severity);
    jb_field_int(jb, "age_adjustment", p->age_adjustment);
    jb_field_int(jb, "priority", p->priority);
    jb_field_int(jb, "effective_priority",
                 patient_effective_priority(p, sim->now, sim->aging_enabled));
    jb_field_str(jb, "status", status_name(p->status));
    jb_field_str(jb, "status_label", status_label(p->status));
    jb_field_int_or_null(jb, "bed_id", p->bed_id > 0 ? p->bed_id : -1);
    jb_field_time(jb, "start_time", p->start_time);
    jb_field_time(jb, "expected_completion", p->expected_completion);
    jb_field_time(jb, "completion_time", p->completion_time);
    jb_field_int_or_null(jb, "waiting_time", calculate_waiting_time(p, sim->now));
    jb_field_int_or_null(jb, "turnaround_time", calculate_turnaround_time(p));
    jb_field_int_or_null(jb, "response_time", calculate_response_time(p));
    jb_field_int_or_null(jb, "queue_position",
                         queue_position > 0 ? queue_position : -1);
    jb_end_object(jb);
}

static void write_patients_array(JsonBuf *jb, const Simulation *sim)
{
    int order[MAX_PATIENTS];
    int position[MAX_PATIENTS];
    int n, i;

    memset(position, 0, sizeof position);
    n = build_waiting_queue(sim->patients, sim->patient_count, sim->now,
                            sim->aging_enabled, order);
    for (i = 0; i < n; i++)
        position[order[i]] = i + 1;

    jb_begin_array(jb, NULL);
    for (i = 0; i < sim->patient_count; i++)
        write_patient(jb, sim, i, position[i]);
    jb_end_array(jb);
}

static void write_bed(JsonBuf *jb, const Simulation *sim, int i)
{
    const Bed *b = &sim->beds[i];
    const Condition *c = get_condition(b->condition_id);

    jb_begin_object(jb, NULL);
    jb_field_int(jb, "id", b->id);
    jb_field_str(jb, "status",
                 b->status == BED_OCCUPIED ? "occupied" : "available");
    if (b->status == BED_OCCUPIED) {
        const Patient *p = &sim->patients[b->patient_index];
        jb_field_str(jb, "patient_id", b->patient_id);
        jb_field_str(jb, "patient_name", p->name);
        jb_field_str(jb, "condition", c ? c->name : "Unknown");
        jb_field_int(jb, "priority", b->priority);
        jb_field_time(jb, "start_time", b->start_time);
        jb_field_time(jb, "expected_completion", b->expected_completion);
        jb_field_int(jb, "remaining_minutes",
                     b->expected_completion - sim->now);
    } else {
        jb_field_null(jb, "patient_id");
        jb_field_null(jb, "patient_name");
        jb_field_null(jb, "condition");
        jb_field_null(jb, "priority");
        jb_field_time(jb, "start_time", -1);
        jb_field_time(jb, "expected_completion", -1);
        jb_field_null(jb, "remaining_minutes");
    }
    jb_end_object(jb);
}

static void write_beds_array(JsonBuf *jb, const Simulation *sim)
{
    int i;
    jb_begin_array(jb, NULL);
    for (i = 0; i < NUM_BEDS; i++)
        write_bed(jb, sim, i);
    jb_end_array(jb);
}

static void write_statistics(JsonBuf *jb, const Simulation *sim)
{
    Statistics s;
    compute_statistics(sim, &s);

    jb_begin_object(jb, NULL);
    jb_field_int(jb, "registered", s.registered);
    jb_field_int(jb, "waiting", s.waiting);
    jb_field_int(jb, "scheduled", s.scheduled);
    jb_field_int(jb, "in_bed", s.in_bed);
    jb_field_int(jb, "completed", s.completed);
    jb_field_int(jb, "total_served", s.completed);
    jb_field_int(jb, "total_beds", NUM_BEDS);
    jb_field_int(jb, "occupied_beds", s.occupied_beds);
    jb_field_int(jb, "available_beds", s.available_beds);
    jb_field_double(jb, "bed_utilization", s.utilization);
    if (s.has_completed) {
        jb_field_double(jb, "avg_waiting_time", s.avg_waiting);
        jb_field_double(jb, "avg_turnaround_time", s.avg_turnaround);
        jb_field_double(jb, "avg_response_time", s.avg_response);
    } else {
        jb_field_null(jb, "avg_waiting_time");
        jb_field_null(jb, "avg_turnaround_time");
        jb_field_null(jb, "avg_response_time");
    }
    if (s.has_waiting)
        jb_field_int(jb, "max_waiting_time", s.max_waiting);
    else
        jb_field_null(jb, "max_waiting_time");
    jb_end_object(jb);
}

static const char *sim_status_name(SimStatus s)
{
    switch (s) {
    case SIM_RUNNING: return "running";
    case SIM_PAUSED:  return "paused";
    default:          return "not_started";
    }
}

static const char *sim_status_label(SimStatus s)
{
    switch (s) {
    case SIM_RUNNING: return "Running";
    case SIM_PAUSED:  return "Paused";
    default:          return "Not Started";
    }
}

/* ================================================================== */
/* Input helpers                                                       */
/* ================================================================== */

/* Reads an integer field. Returns 1 ok, 0 if missing/not a whole number. */
static int field_as_int(const JsonObject *obj, const char *key, int *out)
{
    const JsonField *f = json_get(obj, key);
    if (f == NULL || f->type != JV_NUMBER)
        return 0;
    if (f->num != floor(f->num) || f->num < -1e6 || f->num > 1e6)
        return 0;
    *out = (int)f->num;
    return 1;
}

static int parse_gender_text(const char *s, Gender *out)
{
    if (strcmp(s, "male") == 0)        { *out = GENDER_MALE;   return 1; }
    if (strcmp(s, "female") == 0)      { *out = GENDER_FEMALE; return 1; }
    if (strcmp(s, "other") == 0)       { *out = GENDER_OTHER;  return 1; }
    return 0;
}

/* Body must be a valid flat JSON object (or empty when allow_empty). */
static int read_body(const char *body, size_t len, JsonObject *obj,
                     ApiResponse *r)
{
    char err[160];
    if (!json_parse_object(body ? body : "", len, obj, err, sizeof err)) {
        respond_error(r, 400, err);
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Route handlers                                                      */
/* ================================================================== */

typedef void (*Handler)(Simulation *, const char *, size_t, ApiResponse *);

static void handle_state(Simulation *sim, const char *body, size_t len,
                         ApiResponse *r)
{
    JsonBuf d;
    int i;
    (void)body; (void)len;
    jb_init(&d);
    jb_begin_object(&d, NULL);
    jb_field_time(&d, "time", sim->now);
    jb_field_time(&d, "day_start", sim->start_time);
    jb_field_str(&d, "status", sim_status_name(sim->status));
    jb_field_str(&d, "status_label", sim_status_label(sim->status));
    jb_field_bool(&d, "aging_enabled", sim->aging_enabled);
    jb_field_int(&d, "aging_interval", AGING_INTERVAL_MINUTES);
    jb_field_int(&d, "tick_seconds", sim->tick_seconds);
    jb_field_int(&d, "demo_patient_count", demo_patient_count());
    {
        int order[MAX_PATIENTS];
        int n = build_waiting_queue(sim->patients, sim->patient_count,
                                    sim->now, sim->aging_enabled, order);
        jb_field_int(&d, "queue_length", n);
        if (n > 0)
            jb_field_str(&d, "next_patient_id", sim->patients[order[0]].id);
        else
            jb_field_null(&d, "next_patient_id");
    }
    jb_raw(&d, ",\"statistics\":");
    write_statistics(&d, sim);
    jb_begin_array(&d, "events");
    for (i = 0; i < sim->event_count; i++) {
        const SimEvent *e = &sim->events[i];
        jb_begin_object(&d, NULL);
        jb_field_int(&d, "id", e->id);
        jb_field_time(&d, "time", e->time);
        jb_field_str(&d, "text", e->text);
        jb_end_object(&d);
    }
    jb_end_array(&d);
    jb_end_object(&d);
    respond_ok(r, 200, "OK", &d);
}

static void handle_patients_get(Simulation *sim, const char *body, size_t len,
                                ApiResponse *r)
{
    JsonBuf d;
    (void)body; (void)len;
    jb_init(&d);
    write_patients_array(&d, sim);
    respond_ok(r, 200, "OK", &d);
}

static void handle_beds_get(Simulation *sim, const char *body, size_t len,
                            ApiResponse *r)
{
    JsonBuf d;
    (void)body; (void)len;
    jb_init(&d);
    write_beds_array(&d, sim);
    respond_ok(r, 200, "OK", &d);
}

static void handle_statistics_get(Simulation *sim, const char *body,
                                  size_t len, ApiResponse *r)
{
    JsonBuf d;
    (void)body; (void)len;
    jb_init(&d);
    write_statistics(&d, sim);
    respond_ok(r, 200, "OK", &d);
}

static void handle_conditions_get(Simulation *sim, const char *body,
                                  size_t len, ApiResponse *r)
{
    JsonBuf d;
    int i;
    (void)sim; (void)body; (void)len;
    jb_init(&d);
    jb_begin_array(&d, NULL);
    for (i = 0; i < condition_count(); i++) {
        const Condition *c = get_condition(i);
        jb_begin_object(&d, NULL);
        jb_field_int(&d, "id", c->id);
        jb_field_str(&d, "name", c->name);
        jb_field_int(&d, "severity", c->severity);
        jb_field_int(&d, "duration_minutes", c->duration_minutes);
        jb_field_bool(&d, "requires_context", c->requires_context);
        if (c->context_label)
            jb_field_str(&d, "context_label", c->context_label);
        else
            jb_field_null(&d, "context_label");
        jb_field_int(&d, "min_age", c->min_age);
        jb_field_int(&d, "max_age", c->max_age);
        jb_field_int(&d, "gender", c->gender);
        jb_end_object(&d);
    }
    jb_end_array(&d);
    respond_ok(r, 200, "OK", &d);
}

static const char *const PATIENT_FIELDS[] = {
    "name", "age", "gender", "condition_id", "arrival_time",
    "clinical_context_confirmed", NULL
};

static void handle_patient_register(Simulation *sim, const char *body,
                                    size_t len, ApiResponse *r)
{
    JsonObject obj;
    PatientInput in;
    const JsonField *f;
    char err[200];
    char msg[200];
    int i, k, index, age, cond_id;

    if (!read_body(body, len, &obj, r))
        return;

    /* Reject anything unexpected - in particular a client-side "priority". */
    for (i = 0; i < obj.count; i++) {
        int known = 0;
        for (k = 0; PATIENT_FIELDS[k] != NULL; k++) {
            if (strcmp(obj.fields[i].key, PATIENT_FIELDS[k]) == 0)
                known = 1;
        }
        if (!known) {
            snprintf(msg, sizeof msg,
                     "Unknown field \"%s\" (the priority score is always "
                     "calculated by the server)", obj.fields[i].key);
            respond_error(r, 400, msg);
            return;
        }
    }

    memset(&in, 0, sizeof in);

    f = json_get(&obj, "name");
    if (f == NULL || f->type != JV_STRING) {
        respond_error(r, 400, "Missing required field: name (text)");
        return;
    }
    {
        char tmp[JSON_STRING_LEN];
        snprintf(tmp, sizeof tmp, "%s", f->str);
        trim_name(tmp);
        if (strlen(tmp) > MAX_NAME_LEN) {
            snprintf(msg, sizeof msg,
                     "Patient name must be at most %d characters", MAX_NAME_LEN);
            respond_error(r, 400, msg);
            return;
        }
        snprintf(in.name, sizeof in.name, "%s", tmp);
    }

    if (json_get(&obj, "age") == NULL) {
        respond_error(r, 400, "Missing required field: age");
        return;
    }
    if (!field_as_int(&obj, "age", &age) || age < 0 || age > MAX_AGE) {
        snprintf(msg, sizeof msg, "Age must be a whole number from 0 to %d",
                 MAX_AGE);
        respond_error(r, 400, msg);
        return;
    }
    in.age = age;

    f = json_get(&obj, "gender");
    if (f == NULL) {
        respond_error(r, 400, "Missing required field: gender");
        return;
    }
    if (f->type != JV_STRING || !parse_gender_text(f->str, &in.gender)) {
        respond_error(r, 400,
                      "Gender must be \"male\", \"female\" or \"other\"");
        return;
    }

    if (json_get(&obj, "condition_id") == NULL) {
        respond_error(r, 400, "Missing required field: condition_id");
        return;
    }
    if (!field_as_int(&obj, "condition_id", &cond_id) ||
        get_condition(cond_id) == NULL) {
        respond_error(r, 400, "Unsupported condition");
        return;
    }
    in.condition_id = cond_id;

    f = json_get(&obj, "arrival_time");
    if (f == NULL) {
        respond_error(r, 400, "Missing required field: arrival_time (HH:MM)");
        return;
    }
    if (f->type != JV_STRING || !parse_time(f->str, &in.arrival_time)) {
        respond_error(r, 400, "Arrival time must use the HH:MM format "
                              "(00:00-23:59)");
        return;
    }

    f = json_get(&obj, "clinical_context_confirmed");
    if (f != NULL) {
        if (f->type != JV_BOOL) {
            respond_error(r, 400,
                          "clinical_context_confirmed must be true or false");
            return;
        }
        in.context_confirmed = f->boolean;
    }

    if (!register_patient(sim, &in, &index, err, sizeof err)) {
        int status = strstr(err, "No capacity") ? 409 : 400;
        respond_error(r, status, err);
        return;
    }

    {
        const Patient *p = &sim->patients[index];
        int order[MAX_PATIENTS];
        int n = build_waiting_queue(sim->patients, sim->patient_count,
                                    sim->now, sim->aging_enabled, order);
        int position = 0;
        char when[8];
        JsonBuf d;

        for (i = 0; i < n; i++) {
            if (order[i] == index)
                position = i + 1;
        }
        if (p->status == PATIENT_IN_BED) {
            snprintf(msg, sizeof msg, "%s registered and assigned to Bed %d",
                     p->id, p->bed_id);
        } else if (p->status == PATIENT_WAITING) {
            snprintf(msg, sizeof msg,
                     "%s registered; all beds are busy, waiting at queue "
                     "position %d", p->id, position);
        } else {
            format_time(p->arrival_time, when);
            snprintf(msg, sizeof msg,
                     "%s registered; scheduled to arrive at %s", p->id, when);
        }

        jb_init(&d);
        jb_begin_object(&d, NULL);
        jb_raw(&d, "\"patient\":");
        write_patient(&d, sim, index, position);
        jb_begin_object(&d, "breakdown");
        jb_field_int(&d, "severity", p->severity);
        jb_field_int(&d, "age_adjustment", p->age_adjustment);
        jb_field_int(&d, "priority", p->priority);
        jb_end_object(&d);
        jb_field_int_or_null(&d, "queue_position", position > 0 ? position : -1);
        jb_field_bool(&d, "bed_assigned", p->status == PATIENT_IN_BED);
        jb_end_object(&d);
        respond_ok(r, 201, msg, &d);
    }
}

static void handle_clear(Simulation *sim, const char *body, size_t len,
                         ApiResponse *r)
{
    char err[160];
    (void)body; (void)len;
    if (!clear_patients(sim, err, sizeof err)) {
        respond_error(r, 409, err);
        return;
    }
    respond_ok(r, 200, "Patient records cleared", NULL);
}

static void respond_sim_ok(Simulation *sim, ApiResponse *r, const char *msg)
{
    JsonBuf d;
    jb_init(&d);
    jb_begin_object(&d, NULL);
    jb_field_time(&d, "time", sim->now);
    jb_field_str(&d, "status", sim_status_name(sim->status));
    jb_end_object(&d);
    respond_ok(r, 200, msg, &d);
}

static void handle_start(Simulation *sim, const char *body, size_t len,
                         ApiResponse *r)
{
    char err[160];
    (void)body; (void)len;
    if (!start_simulation(sim, err, sizeof err)) {
        respond_error(r, 409, err);
        return;
    }
    respond_sim_ok(sim, r, "Simulation started");
}

static void handle_pause(Simulation *sim, const char *body, size_t len,
                         ApiResponse *r)
{
    char err[160];
    (void)body; (void)len;
    if (!pause_simulation(sim, err, sizeof err)) {
        respond_error(r, 409, err);
        return;
    }
    respond_sim_ok(sim, r, "Simulation paused");
}

static void handle_resume(Simulation *sim, const char *body, size_t len,
                          ApiResponse *r)
{
    char err[160];
    (void)body; (void)len;
    if (!resume_simulation(sim, err, sizeof err)) {
        respond_error(r, 409, err);
        return;
    }
    respond_sim_ok(sim, r, "Simulation resumed");
}

static void handle_advance(Simulation *sim, const char *body, size_t len,
                           ApiResponse *r)
{
    JsonObject obj;
    char err[160];
    char msg[64];
    char t[8];
    int minutes;

    if (!read_body(body, len, &obj, r))
        return;
    if (json_get(&obj, "minutes") == NULL) {
        respond_error(r, 400, "Missing required field: minutes");
        return;
    }
    if (!field_as_int(&obj, "minutes", &minutes)) {
        respond_error(r, 400, "Minutes must be a whole number");
        return;
    }
    if (!advance_simulation(sim, minutes, err, sizeof err)) {
        respond_error(r, 400, err);
        return;
    }
    format_time(sim->now, t);
    snprintf(msg, sizeof msg, "Advanced %d minutes to %s", minutes, t);
    respond_sim_ok(sim, r, msg);
}

static void handle_reset(Simulation *sim, const char *body, size_t len,
                         ApiResponse *r)
{
    (void)body; (void)len;
    reset_simulation(sim);
    respond_sim_ok(sim, r, "Simulation reset");
}

static void handle_demo(Simulation *sim, const char *body, size_t len,
                        ApiResponse *r)
{
    char err[160];
    char msg[80];
    int n;
    (void)body; (void)len;
    n = load_demo(sim, err, sizeof err);
    if (n < 0) {
        respond_error(r, 500, err);
        return;
    }
    snprintf(msg, sizeof msg, "Demo scenario loaded (%d patients)", n);
    respond_sim_ok(sim, r, msg);
}

static void handle_aging(Simulation *sim, const char *body, size_t len,
                         ApiResponse *r)
{
    JsonObject obj;
    const JsonField *f;

    if (!read_body(body, len, &obj, r))
        return;
    f = json_get(&obj, "enabled");
    if (f == NULL || f->type != JV_BOOL) {
        respond_error(r, 400, "Field \"enabled\" (true/false) is required");
        return;
    }
    set_aging(sim, f->boolean);
    respond_sim_ok(sim, r, f->boolean ? "Priority aging enabled"
                                      : "Priority aging disabled");
}

/* ================================================================== */
/* Routing                                                             */
/* ================================================================== */

typedef struct {
    const char *path;
    const char *method;
    Handler     handler;
} Route;

static const Route ROUTES[] = {
    { "/api/state",              "GET",  handle_state },
    { "/api/patients",           "GET",  handle_patients_get },
    { "/api/beds",               "GET",  handle_beds_get },
    { "/api/statistics",         "GET",  handle_statistics_get },
    { "/api/conditions",         "GET",  handle_conditions_get },
    { "/api/patients",           "POST", handle_patient_register },
    { "/api/patients/clear",     "POST", handle_clear },
    { "/api/simulation/start",   "POST", handle_start },
    { "/api/simulation/pause",   "POST", handle_pause },
    { "/api/simulation/resume",  "POST", handle_resume },
    { "/api/simulation/advance", "POST", handle_advance },
    { "/api/simulation/reset",   "POST", handle_reset },
    { "/api/demo/load",          "POST", handle_demo },
    { "/api/settings/aging",     "POST", handle_aging },
};

#define ROUTE_TOTAL ((int)(sizeof(ROUTES) / sizeof(ROUTES[0])))

int api_path_is_api(const char *path)
{
    return strncmp(path, "/api/", 5) == 0;
}

void api_handle_request(Simulation *sim, const char *method, const char *path,
                        const char *body, size_t body_len, ApiResponse *resp)
{
    const char *allowed = NULL;
    int i;

    resp->status = 500;
    resp->body = NULL;
    resp->allow[0] = '\0';

    for (i = 0; i < ROUTE_TOTAL; i++) {
        if (strcmp(ROUTES[i].path, path) != 0)
            continue;
        if (strcmp(ROUTES[i].method, method) == 0) {
            ROUTES[i].handler(sim, body, body_len, resp);
            return;
        }
        allowed = ROUTES[i].method;
    }

    if (allowed != NULL) {
        snprintf(resp->allow, sizeof resp->allow, "%s", allowed);
        respond_error(resp, 405, "Method not allowed for this endpoint");
        return;
    }
    respond_error(resp, 404, "Unknown endpoint");
}
