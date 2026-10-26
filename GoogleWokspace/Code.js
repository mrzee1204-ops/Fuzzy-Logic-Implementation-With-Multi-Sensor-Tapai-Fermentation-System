const SHEET_NAME = 'Fermentasi Tapai Ketan';
const HEADERS = ['Timestamp', 'Waktu', 'Temperature', 'Humidity', 'Alcohol',
  'Carbondioxide', 'pH', 'Fan Duty', 'Peltier Duty', '_sample_id', '_device_id',
  '_batch_id', '_email_status', '_received_at', '_email_detail'];
const FORMATS = ['dd/MM/yyyy HH:mm:ss', '0.00', '0.00', '0.00', '0.000',
  '0.00', '0.00', '0.00', '0.00', '@', '@', '@', '@', 'dd/MM/yyyy HH:mm:ss', '@'];

function setup() {
  const book = SpreadsheetApp.getActiveSpreadsheet();
  const props = PropertiesService.getScriptProperties();
  if (!book) throw new Error('Buka Apps Script dari spreadsheet tujuan.');
  const oldId = props.getProperty('SPREADSHEET_ID');
  if (oldId && oldId !== book.getId()) throw new Error('SPREADSHEET_ID menunjuk file lain.');
  const sheet = book.getSheetByName(SHEET_NAME) || book.insertSheet(SHEET_NAME);
  if (sheet.getMaxColumns() < 15) sheet.insertColumnsAfter(sheet.getMaxColumns(), 15 - sheet.getMaxColumns());
  if (sheet.getLastRow() && !headersMatch_(sheet) && !legacyHeader_(sheet)) throw new Error('Header berbeda. Gunakan tab kosong dengan nama lain.');
  sheet.getRange(1, 1, 1, 15).setValues([HEADERS]).setFontWeight('bold');
  sheet.setFrozenRows(1);
  sheet.setColumnWidth(1, 175);
  sheet.hideColumns(10, 6);
  book.setSpreadsheetTimeZone('Asia/Jakarta');
  props.setProperty('SPREADSHEET_ID', book.getId());
  if (!props.getProperty('API_TOKEN')) props.setProperty('API_TOKEN', (Utilities.getUuid() + Utilities.getUuid()).replace(/-/g, ''));
  if (props.getProperty('ENABLE_EMAIL') === null) props.setProperty('ENABLE_EMAIL', 'false');
  if (props.getProperty('EMAIL_TO') === null) props.setProperty('EMAIL_TO', '');
  console.log('Setup selesai. API_TOKEN ada di Project Settings > Script properties.');
}

function doGet() {
  return json_({ok: true, service: 'Tapai Sensor API', message: 'Kirim data dengan POST JSON.'});
}

function doPost(e) {
  let id = '', saved = false, lock, locked = false;
  try {
    const body = e && e.postData && e.postData.contents;
    if (typeof body !== 'string' || body.length > 8192) return fail_(400, id, 'Body JSON kosong/terlalu besar.');
    let p;
    try { p = JSON.parse(body); } catch (_) { return fail_(400, id, 'JSON tidak valid.'); }
    if (!p || typeof p !== 'object' || Array.isArray(p)) return fail_(400, id, 'Payload harus objek JSON.');
    const props = PropertiesService.getScriptProperties();
    const token = props.getProperty('API_TOKEN'), bookId = props.getProperty('SPREADSHEET_ID');
    if (!token || token.length < 32 || !bookId) return fail_(503, id, 'Jalankan setup() dan periksa Script properties.');
    if (p.api_token !== token) return fail_(401, id, 'API_TOKEN tidak cocok dengan scriptToken.');
    if (!validPayload_(p)) return fail_(400, id, 'Schema, sample_id, atau waktu tidak valid.');
    id = p.sample_id;
    lock = LockService.getScriptLock();
    locked = lock.tryLock(1000);
    if (!locked) return fail_(503, id, 'Server sibuk; coba lagi.');
    const sheet = SpreadsheetApp.openById(bookId).getSheetByName(SHEET_NAME);
    if (!sheet || !headersMatch_(sheet)) return fail_(503, id, 'Tab/header berubah; periksa setup().');
    const last = sheet.getLastRow();
    const found = last > 1 ? sheet.getRange(2, 10, last - 1, 1).createTextFinder(id)
      .matchEntireCell(true).matchCase(true).useRegularExpression(false).findNext() : null;
    const row = found ? found.getRow() : last + 1;
    if (!found) {
      if (row > sheet.getMaxRows()) sheet.insertRowsAfter(sheet.getMaxRows(), 100);
      const now = new Date();
      const values = [p.sample_epoch ? new Date(p.sample_epoch * 1000) : now, p.uptime_ms / 60000,
        value_(p.temperature_c, p.temperature_valid, -40, 80), value_(p.humidity_pct, p.humidity_valid, 0, 100),
        value_(p.alcohol_est_pct, p.mq3_valid, 0, 100), value_(p.mq135_est_ppm, p.mq135_valid, 10, 1000),
        value_(p.ph, p.ph_valid, 0, 14), value_(p.fan_pct, true, 0, 100), value_(p.ptc_pct, true, 0, 100),
        id, p.device_id, p.batch_id, 'NONE', now, ''];
      sheet.getRange(row, 1, 1, 15).setNumberFormats([FORMATS]).setValues([values]);
      SpreadsheetApp.flush();
    }
    saved = true;
    const reply = {ok: true, code: 200, sample_id: id, saved: true, duplicate: !!found, email_sent: false};
    if (p.kind === 'maturity') Object.assign(reply, sendEmail_(sheet, row, p, props));
    return json_(reply);
  } catch (err) {
    console.error(err.message);
    return fail_(500, id, 'Kesalahan server; lihat Executions.', saved);
  } finally {
    if (locked) lock.releaseLock();
  }
}

function validPayload_(p) {
  const id = /^[A-Za-z0-9][A-Za-z0-9_.:-]{0,79}$/;
  return p.schema === 1 && ['telemetry', 'maturity'].includes(p.kind) &&
    [p.device_id, p.batch_id].every(v => typeof v === 'string' && id.test(v)) &&
    [p.boot, p.sequence, p.uptime_ms].every(v => Number.isSafeInteger(v) && v >= 0) &&
    p.sample_id === p.device_id + '-' + p.batch_id + '-' + p.boot + '-' + p.sequence &&
    Number.isSafeInteger(p.sample_epoch) && (p.sample_epoch === 0 || (p.sample_epoch >= 1700000000 && p.sample_epoch <= 4102444800));
}

function value_(v, valid, min, max) {
  return valid === true && typeof v === 'number' && Number.isFinite(v) && v >= min && v <= max ? v : '';
}

function legacyHeader_(sheet) {
  const row = sheet.getRange(1, 1, 1, 15).getValues()[0];
  return row.slice(0, 9).every((v, i) => v === HEADERS[i]) && row.slice(9).every(v => v === '');
}

function headersMatch_(sheet) {
  return sheet.getRange(1, 1, 1, 15).getValues()[0].every((v, i) => v === HEADERS[i]);
}
function sendEmail_(sheet, row, p, props) {
  const status = sheet.getRange(row, 13).getValue();
  if (status === 'SENT') return {ok: true, code: 200, email_sent: true};
  if (['SENDING', 'UNCERTAIN'].includes(status)) return {ok: false, code: 409, message: 'Periksa status email secara manual.'};
  const to = (props.getProperty('EMAIL_TO') || '').trim();
  if (props.getProperty('ENABLE_EMAIL') !== 'true' || !/^[^\s@,;<>]+@[^\s@,;<>]+\.[^\s@,;<>]+$/.test(to)) {
    emailState_(sheet, row, 'DISABLED', 'Isi EMAIL_TO dan ENABLE_EMAIL=true.');
    return {ok: false, code: 503, message: 'Data tersimpan; email belum diaktifkan.'};
  }
  const data = sheet.getRange(row, 1, 1, 9).getValues()[0];
  if (![p.maturity_eligible, p.mature_latched, p.mq3_calibrated, p.ph_calibrated, p.mq3_valid, p.ph_valid].every(v => v === true) || data[4] === '' || data[6] === '') {
    emailState_(sheet, row, 'BLOCKED', 'Flag kematangan/kalibrasi atau sensor belum valid.');
    return {ok: false, code: 422, message: 'Event kematangan belum valid.'};
  }
  if (MailApp.getRemainingDailyQuota() < 1) {
    emailState_(sheet, row, 'QUOTA', 'Kuota email habis.');
    return {ok: false, code: 503, message: 'Kuota email habis; coba lagi nanti.'};
  }
  emailState_(sheet, row, 'SENDING', 'Sedang mengirim.');
  try {
    const details = HEADERS.slice(0, 9).map((name, i) => name + ': ' + (data[i] === '' ? 'tidak valid' :
      (i === 0 ? Utilities.formatDate(data[i], 'Asia/Jakarta', 'dd/MM/yyyy HH:mm:ss') : data[i])));
    MailApp.sendEmail({to: to, subject: '[Tapai] Indikasi matang - ' + p.device_id + ' / ' + p.batch_id,
      body: 'Indikasi kematangan menurut firmware. Sample: ' + p.sample_id + '\n' + details.join('\n') +
        '\nWaktu: menit sejak boot. Alcohol/MQ135: estimasi. Peltier Duty = heater PTC.'});
    emailState_(sheet, row, 'SENT', 'MailApp menerima permintaan pengiriman.');
    return {ok: true, code: 200, email_sent: true};
  } catch (err) {
    emailState_(sheet, row, 'UNCERTAIN', 'Periksa penerimaan email sebelum mencoba ulang.');
    return {ok: false, code: 409, message: 'Status email belum pasti; periksa sebelum mencoba ulang.'};
  }
}

function emailState_(sheet, row, status, detail) {
  sheet.getRange(row, 13).setValue(status);
  sheet.getRange(row, 15).setValue(detail);
  SpreadsheetApp.flush();
}

function fail_(code, id, message, saved = false) {
  return json_({ok: false, code: code, sample_id: id, saved: saved, email_sent: false, message: message});
}

function json_(value) {
  return ContentService.createTextOutput(JSON.stringify(value)).setMimeType(ContentService.MimeType.JSON);
}
