/* Hospital Bed Scheduler - browser UI.
 * This file only DISPLAYS state and SENDS user actions. Every scheduling
 * decision (priority, queue order, bed assignment, clock) is made by the C
 * backend and fetched through the JSON API. */
(function () {
  'use strict';

  const $ = (sel) => document.querySelector(sel);
  const $$ = (sel) => Array.from(document.querySelectorAll(sel));

  function esc(value) {
    return String(value === null || value === undefined ? '' : value).replace(
      /[&<>"']/g,
      (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c])
    );
  }

  let state = null;
  let patients = [];
  let beds = [];
  let conditions = [];
  let lastEventId = null;      // null until the first state arrives
  let arrivalTouched = false;
  let pending = false;

  /* ------------------------------------------------------------ API */

  async function api(method, path, body) {
    const options = { method: method, headers: {} };
    if (body !== undefined) {
      options.headers['Content-Type'] = 'application/json';
      options.body = JSON.stringify(body);
    }
    let json;
    try {
      const res = await fetch(path, options);
      json = await res.json();
    } catch (e) {
      throw new Error('Cannot reach the server');
    }
    if (!json.success) throw new Error(json.error || 'Request failed');
    return json;
  }

  /* --------------------------------------------------------- toasts */

  function toast(text, kind) {
    const el = document.createElement('div');
    el.className = 'toast' + (kind ? ' ' + kind : '');
    el.textContent = text;
    $('#toasts').appendChild(el);
    setTimeout(() => el.remove(), 4500);
  }

  function handleEvents(silent) {
    const events = state.events || [];
    const maxId = events.length ? events[events.length - 1].id : (lastEventId || 0);
    if (lastEventId === null || silent) {
      lastEventId = Math.max(lastEventId || 0, maxId);
      return;
    }
    const fresh = events.filter((e) => e.id > lastEventId);
    lastEventId = Math.max(lastEventId, maxId);
    fresh.slice(-4).forEach((e) => {
      const kind = /completed|released/.test(e.text) ? 'good' : '';
      toast(e.time_str + '  ' + e.text, kind);
    });
    if (fresh.length > 4) toast('+ ' + (fresh.length - 4) + ' more events (see Recent activity)');
  }

  /* --------------------------------------------------------- render */

  const fmtMin = (m) => (m === null || m === undefined ? '—' : m + ' min');
  const fmtTime = (t) => (t === null || t === undefined ? '—' : t);

  function renderHeader() {
    $('#sim-time').textContent = state.time_str;
    const badge = $('#sim-status');
    badge.textContent = state.status_label;
    badge.className = 'badge ' + state.status;
    const toggle = $('#btn-toggle');
    toggle.textContent = state.status === 'running' ? 'Pause Simulation'
      : state.status === 'paused' ? 'Resume Simulation' : 'Start Simulation';
    $('#aging-toggle').checked = state.aging_enabled;

    let note;
    if (state.status === 'running') {
      note = state.tick_seconds > 0
        ? 'Running: the clock advances 1 simulated minute every ' + state.tick_seconds +
          ' s. Pause to hold it still. The +5/+15/+30/+60 buttons always work.'
        : 'Running (auto-advance disabled on this server). Use the +minutes buttons.';
    } else {
      note = 'The clock only moves when you press an advance button' +
        (state.tick_seconds > 0 ? ' or start the simulation.' : '.') +
        ' Refreshing this page never changes the time.';
    }
    $('#auto-note').textContent = note;

    const earliest = minutesToTime(timeToMinutes(state.time_str) + MIN_ARRIVAL_LEAD);
    if (!arrivalTouched) {
      $('#f-arrival').value = earliest;
    }
    $('#f-arrival').min = earliest;
  }

  function stat(value, label, cls) {
    return '<div class="stat ' + (cls || '') + '"><div class="v">' + esc(value) +
      '</div><div class="l">' + esc(label) + '</div></div>';
  }

  function renderCards() {
    const s = state.statistics;
    $('#summary-cards').innerHTML =
      stat(s.registered, 'Registered patients') +
      stat(s.waiting, 'Patients waiting', 'amber') +
      stat(s.occupied_beds, 'Occupied beds', 'amber') +
      stat(s.available_beds, 'Available beds', 'green') +
      stat(s.completed, 'Completed patients', 'blue') +
      stat(s.bed_utilization.toFixed(0) + '%', 'Bed utilization');
  }

  function bedCard(b) {
    if (b.status !== 'occupied') {
      return '<div class="bed available"><div class="bed-head"><span class="bed-no">Bed ' + b.id +
        '</span><span class="pill">Available</span></div><div class="free-text">Free</div></div>';
    }
    return '<div class="bed occupied"><div class="bed-head"><span class="bed-no">Bed ' + b.id +
      '</span><span class="pill">Occupied</span></div><dl>' +
      '<dt>Patient</dt><dd>' + esc(b.patient_name) + '</dd>' +
      '<dt>ID</dt><dd>' + esc(b.patient_id) + '</dd>' +
      '<dt>Condition</dt><dd>' + esc(b.condition) + '</dd>' +
      '<dt>Priority</dt><dd>' + esc(b.priority) + '</dd>' +
      '<dt>Started</dt><dd>' + esc(b.start_time_str) + '</dd>' +
      '<dt>Ends</dt><dd>' + esc(b.expected_completion_str) + '</dd>' +
      '<dt>Remaining</dt><dd>' + esc(b.remaining_minutes) + ' min</dd>' +
      '</dl></div>';
  }

  function renderBeds() {
    const html = beds.map(bedCard).join('');
    $('#bed-grid-dashboard').innerHTML = html;
    $('#bed-grid-beds').innerHTML = html;
  }

  function table(el, headers, rows, emptyText) {
    let html = '<thead><tr>' + headers.map((h) => '<th>' + esc(h) + '</th>').join('') + '</tr></thead><tbody>';
    if (!rows.length) {
      html += '<tr><td class="empty" colspan="' + headers.length + '">' + esc(emptyText) + '</td></tr>';
    } else {
      html += rows.join('');
    }
    el.innerHTML = html + '</tbody>';
  }

  function statusTag(p) {
    return '<span class="tag ' + esc(p.status) + '">' + esc(p.status_label) + '</span>';
  }

  function renderQueue() {
    const term = $('#queue-search').value.trim().toLowerCase();
    const match = (p) => !term || p.id.toLowerCase().includes(term) || p.name.toLowerCase().includes(term);
    const aging = state.aging_enabled;

    const waiting = patients.filter((p) => p.status === 'waiting')
      .sort((a, b) => a.queue_position - b.queue_position).filter(match);
    const headers = ['#', 'Patient ID', 'Name', 'Age', 'Gender', 'Condition', 'Arrival',
      'Severity', 'Age adj.', 'Priority'];
    if (aging) headers.push('Effective');
    headers.push('Waiting', 'Status');
    table($('#queue-table'), headers, waiting.map((p) => {
      const cells = [p.queue_position, p.id, p.name, p.age, p.gender, p.condition,
        p.arrival_time_str, p.severity, '+' + p.age_adjustment, p.priority];
      if (aging) cells.push(p.effective_priority);
      cells.push(fmtMin(p.waiting_time));
      return '<tr' + (p.queue_position === 1 ? ' class="next"' : '') + '>' +
        cells.map((c) => '<td>' + esc(c) + '</td>').join('') +
        '<td>' + (p.queue_position === 1 ? '<span class="tag in_bed">Next for a bed</span>' : statusTag(p)) +
        '</td></tr>';
    }), term ? 'No waiting patient matches the search.' : 'Nobody is waiting for a bed.');

    const future = patients.filter((p) => p.status === 'scheduled')
      .sort((a, b) => a.arrival_time - b.arrival_time || a.registration_seq - b.registration_seq)
      .filter(match);
    table($('#future-table'),
      ['Patient ID', 'Name', 'Age', 'Condition', 'Arrival', 'Arrives in', 'Priority', 'Status'],
      future.map((p) => '<tr>' + [p.id, p.name, p.age, p.condition, p.arrival_time_str,
        (p.arrival_time - timeToMinutes(state.time_str)) + ' min', p.priority]
        .map((c) => '<td>' + esc(c) + '</td>').join('') + '<td>' + statusTag(p) + '</td></tr>'),
      'No future arrivals.');
  }

  function timeToMinutes(str) {
    const parts = str.split(':');
    return parseInt(parts[0], 10) * 60 + parseInt(parts[1], 10);
  }

  function renderMini() {
    const waiting = patients.filter((p) => p.status === 'waiting')
      .sort((a, b) => a.queue_position - b.queue_position).slice(0, 5);
    $('#mini-queue').innerHTML = waiting.length
      ? waiting.map((p) => '<div class="item"><span>' + esc(p.queue_position) + '. ' + esc(p.name) +
        '</span><span>P' + esc(p.effective_priority) + ' &middot; ' + fmtMin(p.waiting_time) + '</span></div>').join('')
      : '<p class="muted">Nobody is waiting.</p>';
  }

  function renderEvents() {
    const events = (state.events || []).slice().reverse().slice(0, 40);
    $('#event-feed').innerHTML = events.length
      ? events.map((e) => '<li><time>' + esc(e.time_str) + '</time>' + esc(e.text) + '</li>').join('')
      : '<li class="muted">No activity yet.</li>';
  }

  function renderCompleted() {
    const done = patients.filter((p) => p.status === 'completed')
      .sort((a, b) => a.completion_time - b.completion_time || a.registration_seq - b.registration_seq);
    table($('#completed-table'),
      ['Patient ID', 'Name', 'Condition', 'Bed', 'Arrival', 'Bed assigned', 'Completed', 'Waiting', 'Turnaround'],
      done.map((p) => '<tr>' + [p.id, p.name, p.condition, p.bed_id, p.arrival_time_str,
        p.start_time_str, p.completion_time_str, fmtMin(p.waiting_time), fmtMin(p.turnaround_time)]
        .map((c) => '<td>' + esc(c) + '</td>').join('') + '</tr>'),
      'No patient has completed yet.');
  }

  function bar(label, value, max, cls, shown) {
    const pct = max > 0 ? Math.min(100, (value / max) * 100) : 0;
    return '<div class="bar-row"><span>' + esc(label) + '</span><div class="bar"><span class="' + (cls || '') +
      '" style="width:' + pct.toFixed(1) + '%"></span></div><b>' + esc(shown !== undefined ? shown : value) + '</b></div>';
  }

  function renderStatistics() {
    const s = state.statistics;
    const na = (v) => (v === null ? 'N/A' : v.toFixed(1) + ' min');
    $('#stat-cards').innerHTML =
      stat(na(s.avg_waiting_time), 'Average waiting time') +
      stat(na(s.avg_turnaround_time), 'Average turnaround time') +
      stat(na(s.avg_response_time), 'Average response time') +
      stat(s.bed_utilization.toFixed(1) + '%', 'Bed utilization', 'green') +
      stat(s.completed, 'Completed patients', 'blue') +
      stat(s.max_waiting_time === null ? 'N/A' : s.max_waiting_time + ' min', 'Longest current wait', 'amber') +
      stat(s.total_served, 'Total patients served');

    $('#util-chart').innerHTML =
      bar('Occupied', s.occupied_beds, s.total_beds, 'amber', s.occupied_beds + '/' + s.total_beds) +
      bar('Available', s.available_beds, s.total_beds, 'green', s.available_beds + '/' + s.total_beds) +
      '<p class="muted">Utilization = occupied / total beds &times; 100 = ' + s.bed_utilization.toFixed(1) + '%</p>';

    const max = Math.max(1, s.registered);
    $('#state-chart').innerHTML =
      bar('Scheduled', s.scheduled, max, 'blue') + bar('Waiting', s.waiting, max, 'amber') +
      bar('In bed', s.in_bed, max, 'green') + bar('Completed', s.completed, max, 'grey');

    const done = patients.filter((p) => p.status === 'completed');
    const top = Math.max(1, ...done.map((p) => p.turnaround_time || 0));
    $('#wait-chart').innerHTML = done.length
      ? done.map((p) => bar(p.id + ' wait', p.waiting_time, top, 'amber', p.waiting_time) +
        bar(p.id + ' turnaround', p.turnaround_time, top, '', p.turnaround_time)).join('')
      : '<p class="muted">No completed patients yet.</p>';
  }

  const MIN_ARRIVAL_LEAD = 1;   /* minutes; must match MIN_ARRIVAL_LEAD_MINUTES */

  function minutesToTime(m) {
    m = Math.min(m, 1439);
    return String(Math.floor(m / 60)).padStart(2, '0') + ':' + String(m % 60).padStart(2, '0');
  }

  function genderNote(c) {
    return c.gender === 0 ? 'Male patients only' : c.gender === 1 ? 'Female patients only' : '';
  }

  /* Only conditions that fit the chosen gender are offered. */
  function renderConditionOptions() {
    const select = $('#f-condition');
    const g = $('#f-gender').value;
    const previous = select.value;
    const fits = (c) => c.gender < 0 || (g === 'male' && c.gender === 0) || (g === 'female' && c.gender === 1);
    select.innerHTML = '<option value="">Select...</option>' + conditions.filter(fits).map((c) =>
      '<option value="' + c.id + '">' + esc(c.name) + ' (severity ' + c.severity + ', ' +
      c.duration_minutes + ' min)</option>').join('');
    if (conditions.some((c) => String(c.id) === previous && fits(c))) select.value = previous;
    updateContextField();
  }

  function renderConditions() {
    table($('#conditions-table'),
      ['Condition', 'Severity', 'Modeled bed occupancy', 'Eligibility notes'],
      conditions.map((c) => '<tr><td>' + esc(c.name) + '</td><td>' + c.severity + '</td><td>' +
        c.duration_minutes + ' min</td><td>' +
        (c.requires_context ? 'Requires clinical-context confirmation (ages ' + c.min_age + '-' + c.max_age + ')' : 'General') +
        (genderNote(c) ? '; ' + genderNote(c) : '') +
        '</td></tr>'),
      'No conditions.');
    renderConditionOptions();
  }

  function render() {
    renderHeader();
    renderCards();
    renderBeds();
    renderMini();
    renderEvents();
    renderQueue();
    renderCompleted();
    renderStatistics();
  }

  /* -------------------------------------------------------- refresh */

  async function refresh(silent) {
    try {
      const results = await Promise.all([
        api('GET', '/api/state'), api('GET', '/api/patients'), api('GET', '/api/beds')]);
      state = results[0].data;
      patients = results[1].data;
      beds = results[2].data;
      handleEvents(silent === true);
      render();
    } catch (e) {
      $('#auto-note').textContent = 'Server unreachable: ' + e.message;
    }
  }

  /* -------------------------------------------------------- actions */

  async function act(request, fallbackMessage) {
    if (pending) return;
    pending = true;
    $$('button').forEach((b) => (b.disabled = true));
    try {
      const res = await request();
      toast(res.message || fallbackMessage, 'good');
      await refresh(true);
    } catch (e) {
      toast(e.message, 'error');
    } finally {
      pending = false;
      $$('button').forEach((b) => (b.disabled = false));
    }
  }

  function setupControls() {
    $('#btn-toggle').addEventListener('click', () => {
      const path = state.status === 'running' ? '/api/simulation/pause'
        : state.status === 'paused' ? '/api/simulation/resume' : '/api/simulation/start';
      act(() => api('POST', path, {}));
    });
    $$('[data-advance]').forEach((btn) => btn.addEventListener('click', () => {
      act(() => api('POST', '/api/simulation/advance', { minutes: parseInt(btn.dataset.advance, 10) }));
    }));
    $('#btn-custom').addEventListener('click', () => {
      const minutes = parseInt($('#custom-minutes').value, 10);
      if (!Number.isInteger(minutes) || minutes < 1 || minutes > 480) {
        toast('Enter a whole number of minutes from 1 to 480', 'error');
        return;
      }
      act(() => api('POST', '/api/simulation/advance', { minutes: minutes }));
    });
    $('#btn-demo').addEventListener('click', () => {
      arrivalTouched = false;
      act(() => api('POST', '/api/demo/load', {}));
    });
    $('#btn-reset').addEventListener('click', () => {
      arrivalTouched = false;
      act(() => api('POST', '/api/simulation/reset', {}));
    });
    $('#aging-toggle').addEventListener('change', (ev) => {
      act(() => api('POST', '/api/settings/aging', { enabled: ev.target.checked }));
    });
    $('#queue-search').addEventListener('input', () => state && renderQueue());
  }

  /* ----------------------------------------------------------- form */

  function setError(id, message) {
    const el = $('#e-' + id);
    el.textContent = message || '';
    const input = $('#f-' + id);
    if (input) input.classList.toggle('invalid', !!message);
  }

  function selectedCondition() {
    const id = parseInt($('#f-condition').value, 10);
    return conditions.find((c) => c.id === id) || null;
  }

  function updateContextField() {
    const c = selectedCondition();
    const show = !!(c && c.requires_context);
    $('#context-field').hidden = !show;
    if (show) $('#context-label').textContent = c.context_label;
    else $('#f-context').checked = false;
  }

  function validateForm() {
    let ok = true;
    const name = $('#f-name').value.trim();
    setError('name', '');
    if (!name) { setError('name', 'Patient name is required'); ok = false; }
    else if (name.length > 80) { setError('name', 'At most 80 characters'); ok = false; }

    const ageText = $('#f-age').value.trim();
    const age = Number(ageText);
    setError('age', '');
    if (ageText === '' || !Number.isInteger(age) || age < 0 || age > 120) {
      setError('age', 'Enter a whole number from 0 to 120'); ok = false;
    }

    setError('gender', '');
    if (!$('#f-gender').value) { setError('gender', 'Select a gender'); ok = false; }

    setError('condition', '');
    const c = selectedCondition();
    if (!c) { setError('condition', 'Select a condition'); ok = false; }
    else if (c.gender === 0 && $('#f-gender').value !== 'male') { setError('condition', 'This condition applies to male patients only'); ok = false; }
    else if (c.gender === 1 && $('#f-gender').value !== 'female') { setError('condition', 'This condition applies to female patients only'); ok = false; }

    setError('context', '');
    if (c && c.requires_context) {
      if (!$('#f-context').checked) {
        setError('context', 'Confirmation required for this condition'); ok = false;
      } else if (Number.isInteger(age) && ageText !== '' && (age < c.min_age || age > c.max_age)) {
        setError('condition', 'This condition is modeled for ages ' + c.min_age + '-' + c.max_age); ok = false;
      }
    }

    setError('arrival', '');
    const arrival = $('#f-arrival').value;
    if (!/^\d{2}:\d{2}$/.test(arrival)) {
      setError('arrival', 'Enter an arrival time'); ok = false;
    } else if (state && timeToMinutes(arrival) < timeToMinutes(state.time_str) + MIN_ARRIVAL_LEAD) {
      setError('arrival', 'Must be at least ' + MIN_ARRIVAL_LEAD + ' minutes after ' + state.time_str); ok = false;
    }
    return ok;
  }

  function showResult(html, good) {
    const box = $('#register-result');
    box.className = 'card ' + (good ? 'result-ok' : 'result-bad');
    box.innerHTML = '<h2>Result</h2>' + html;
  }

  async function submitForm(ev) {
    ev.preventDefault();
    if (pending) return;
    if (!validateForm()) {
      toast('Please fix the highlighted fields', 'error');
      return;
    }
    const c = selectedCondition();
    const body = {
      name: $('#f-name').value.trim(),
      age: parseInt($('#f-age').value, 10),
      gender: $('#f-gender').value,
      condition_id: c.id,
      arrival_time: $('#f-arrival').value
    };
    if (c.requires_context) body.clinical_context_confirmed = $('#f-context').checked;

    pending = true;
    $('#btn-submit').disabled = true;
    try {
      const res = await api('POST', '/api/patients', body);
      const p = res.data.patient;
      const b = res.data.breakdown;
      let where;
      if (p.status === 'in_bed') where = 'Assigned to <strong>Bed ' + esc(p.bed_id) + '</strong> (until ' + esc(p.expected_completion_str) + ').';
      else if (p.status === 'waiting') where = 'All beds are occupied: <strong>waiting</strong>, queue position ' + esc(res.data.queue_position) + '.';
      else where = 'Scheduled to arrive at <strong>' + esc(p.arrival_time_str) + '</strong>; not eligible for a bed before then.';
      showResult('<p><strong>' + esc(p.id) + '</strong> &mdash; ' + esc(p.name) + '</p>' +
        '<p class="score-line">Severity ' + esc(b.severity) + ' + Age adjustment ' + esc(b.age_adjustment) +
        ' = <strong>Priority ' + esc(b.priority) + '</strong></p><p>' + where + '</p>', true);
      toast(res.message, 'good');
      $('#patient-form').reset();
      arrivalTouched = false;
      renderConditionOptions();
      await refresh(true);
    } catch (e) {
      showResult('<p>' + esc(e.message) + '</p>', false);
      toast(e.message, 'error');
    } finally {
      pending = false;
      $('#btn-submit').disabled = false;
    }
  }

  function setupForm() {
    $('#patient-form').addEventListener('submit', submitForm);
    $('#f-condition').addEventListener('change', updateContextField);
    $('#f-gender').addEventListener('change', renderConditionOptions);
    $('#f-arrival').addEventListener('input', () => { arrivalTouched = true; });
  }

  /* ----------------------------------------------------- navigation */

  function showSection(name) {
    const known = $$('.panel-section').map((s) => s.id);
    if (!known.includes(name)) name = 'dashboard';
    $$('.panel-section').forEach((s) => { s.hidden = s.id !== name; });
    $$('.sidebar nav a').forEach((a) => a.classList.toggle('active', a.dataset.section === name));
  }

  window.addEventListener('hashchange', () => showSection(location.hash.slice(1)));

  /* ----------------------------------------------------------- init */

  async function init() {
    showSection(location.hash.slice(1));
    setupControls();
    setupForm();
    try {
      conditions = (await api('GET', '/api/conditions')).data;
      renderConditions();
    } catch (e) {
      toast(e.message, 'error');
    }
    await refresh(true);
    setInterval(() => { if (!pending) refresh(false); }, 1500);
  }

  init();
})();
