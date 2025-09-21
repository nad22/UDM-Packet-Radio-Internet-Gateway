<?php
require_once("session_check.php");
if (!is_admin()) { header("Location: login.php"); exit; }
require_once("db.php");
?>
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>UDMPRIG-Server Webportal</title>
<meta name="viewport" content="width=device-width, initial-scale=1.0"/>
<link href="https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/css/materialize.min.css" rel="stylesheet">
<style>
body {padding:18px; max-width: 1400px; margin: 0 auto;}
pre {font-size: 12px; background:#222; color:#eee; padding:10px;}
.input-field label {color: #009688;}
.tabs .tab a.active { color: #009688;}
.tabs .tab a { color: #444;}
.input-field {margin-bottom: 0;}
.custom-row {margin-bottom: 20px;}
td input, td select {width:95%; margin:0; font-size:1em;}
td {vertical-align:middle;}
.icon-btn {border:none; background:transparent; cursor:pointer;}
.success-msg {color: #388e3c; font-weight: bold;}
.container { max-width: 1400px; }
@media (max-width: 768px) { 
  body { padding: 10px; } 
  table { font-size: 0.9em; }
}
@media (max-width: 480px) { 
  table { font-size: 0.8em; }
  td input, td select { font-size: 0.8em; }
}
</style>
</head>
<body>
<h4><i class="material-icons left">settings_ethernet</i>UDMPRIG-Server</h4>
<div style="text-align:right">
  <a href="logout.php" class="btn-flat"><i class="material-icons left">exit_to_app</i>Logout</a>
</div>
<ul id="tabs-swipe-demo" class="tabs">
  <li class="tab col s3"><a class="active" href="#config">Config</a></li>
  <li class="tab col s3"><a href="#monitor">Monitor</a></li>
</ul>
<div id="config" class="col s12">
  <!-- ADMIN USER -->
  <h5>Admin-Zugang</h5>
  <form id="adminform" autocomplete="off">
    <div class="input-field custom-row">
      <input id="admin_username" name="admin_username" type="text" maxlength="48">
      <label for="admin_username" class="active">Benutzername</label>
    </div>
    <div class="input-field custom-row">
      <input id="admin_newpass" name="admin_newpass" type="password" maxlength="64">
      <label for="admin_newpass">Neues Passwort (leer lassen für kein Wechsel)</label>
    </div>
    <button class="btn teal" type="submit">Speichern</button>
    <span id="adminmsg" class="success-msg"></span>
  </form>
  <!-- SERVER CONFIG -->
  <h5 style="margin-top:2em;">Server-Konfiguration</h5>
  <form id="configform" autocomplete="off">
    <div class="input-field custom-row">
      <input id="callsign" name="callsign" type="text" maxlength="31">
      <label for="callsign" class="active">Server Callsign</label>
    </div>
    <div class="input-field custom-row">
      <select id="loglevel" name="loglevel">
        <option value="0">Error</option>
        <option value="1">Info</option>
        <option value="2">Warning</option>
        <option value="3">Debug</option>
      </select>
      <label for="loglevel">Loglevel</label>
    </div>
    <button class="btn teal" type="submit">Speichern</button>
    <span id="configmsg" class="success-msg"></span>
  </form>
  <!-- CLIENT LIST -->
  <h5 style="margin-top:2em;">Client-Liste</h5>
  <table class="striped" id="clienttbl">
    <thead>
      <tr><th>#</th><th>Callsign</th><th>Status</th><th>Aktion</th></tr>
    </thead>
    <tbody id="clientbody"></tbody>
    <tfoot>
      <tr>
        <td></td>
        <td><input id="new_callsign" maxlength="31"></td>
        <td>
          <select id="new_status">
            <option value="0">allow</option>
            <option value="1">deny</option>
            <option value="2">blocked</option>
          </select>
        </td>
        <td><button class="icon-btn" type="button" onclick="addRow()"><i class='material-icons green-text'>save</i></button></td>
      </tr>
    </tfoot>
  </table>
</div>
<div id="monitor" class="col s12" style="display:none;">
  <h6>Server Monitor</h6>
  <pre id="monitorArea" style="height:600px;overflow:auto;background:#222;color:#eee;"></pre>
  <button class="btn red" type="button" onclick="clearMonitor()">Leeren</button>
  
  <!-- AX25 Test Sender -->
  <div class="card" style="margin-top: 20px;">
    <div class="card-content">
      <span class="card-title">Smart AX25 Test Sender</span>
      <div class="row">
        <div class="col s8">
          <input type="text" id="testMessage" placeholder="Test message" value="test">
        </div>
        <div class="col s4">
          <button class="btn green" onclick="sendSmartTestFrame()">Smart Broadcast</button>
        </div>
      </div>
      <p class="grey-text">Sends: fm PRGSRV to CQ ctl UI^ pid F0 [your message]</p>
      <p class="blue-text"><strong>Smart-Polling:</strong> Nachrichten kommen in 2-10 Sekunden an!</p>
    </div>
  </div>
  
  <!-- Smart-Polling Status -->
  <div class="card" style="margin-top: 20px;">
    <div class="card-content">
      <span class="card-title">Smart-Polling Status</span>
      <div class="row">
        <div class="col s6">
          <p><strong>System:</strong> <span class="green-text">Smart-Polling aktiv</span></p>
          <p><strong>Wartende Notifications:</strong> <span id="pendingCount">0</span></p>
          <p><strong>Aktive Clients:</strong> <span id="activeClients">0</span></p>
          <p><strong>Gesendet (1h):</strong> <span id="sentCount">0</span></p>
        </div>
        <div class="col s6">
          <button class="btn blue" onclick="checkSmartStatus()">Status Aktualisieren</button>
        </div>
      </div>
    </div>
  </div>
</div>
<script>
function statusLabel(val){ if(val==0) return 'allow'; if(val==1) return 'deny'; return 'blocked'; }
let editing = {};
let currentList = [];
function renderTable(list){
  let body = '';
  for(let i=0;i<list.length;++i){
    let c = list[i];
    if(!c.callsign) continue;
    if(editing[i]){
      body += '<tr><td>'+(i+1)+'</td>';
      body += '<td><input id="e_callsign_'+i+'" maxlength="31" value="'+(c.callsign||"")+'" /></td>';
      body += '<td><select id="e_status_'+i+'">';
      body += '<option value="0"'+(c.status==0?' selected':'')+'>allow</option>';
      body += '<option value="1"'+(c.status==1?' selected':'')+'>deny</option>';
      body += '<option value="2"'+(c.status==2?' selected':'')+'>blocked</option></select></td>';
      body += '<td><button class="icon-btn" type="button" onclick="saveRow('+i+')"><i class="material-icons green-text">save</i></button>';
      body += '<button class="icon-btn" type="button" onclick="cancelEdit('+i+')"><i class="material-icons">cancel</i></button>';
      body += '<button class="icon-btn" type="button" onclick="delRow('+i+')"><i class="material-icons red-text">delete</i></button></td></tr>';
    } else {
      body += '<tr><td>'+(i+1)+'</td>';
      body += '<td ondblclick="editRow('+i+')">'+(c.callsign||"")+'</td>';
      body += '<td ondblclick="editRow('+i+')">'+statusLabel(c.status)+'</td>';
      body += '<td><button class="icon-btn" type="button" onclick="editRow('+i+')"><i class="material-icons blue-text">edit</i></button>';
      body += '<button class="icon-btn" type="button" onclick="delRow('+i+')"><i class="material-icons red-text">delete</i></button></td></tr>';
    }
  }
  document.getElementById('clientbody').innerHTML = body;
  M.FormSelect.init(document.querySelectorAll('select'));
}
function fetchList(){ fetch('api/client_list_json.php').then(r=>r.json()).then(list=>{currentList = list; renderTable(list);}); }
function editRow(i){ editing[i]=true; renderTable(currentList); }
function cancelEdit(i){ editing[i]=false; renderTable(currentList); }
function saveRow(i){
  let data = { id:currentList[i].id, callsign:document.getElementById('e_callsign_'+i).value,
    status:document.getElementById('e_status_'+i).value };
  fetch('api/client_list_save.php', {method:'POST',body:new URLSearchParams(data)}).then(fetchList);
  editing[i]=false;
}
function delRow(i){ if(!confirm('Wirklich löschen?')) return; fetch('api/client_list_del.php?id='+currentList[i].id).then(fetchList); }
function addRow(){
  let data = { id:-1, callsign:document.getElementById('new_callsign').value,
    status:document.getElementById('new_status').value };
  if(!data.callsign) { alert('Callsign eingeben!'); return; }
  fetch('api/client_list_save.php', {method:'POST',body:new URLSearchParams(data)}).then(_=>{
    document.getElementById('new_callsign').value=''; 
    document.getElementById('new_status').value='0';
    fetchList();
  });
}

// SERVER CONFIG AJAX
function getServerConfig(){
  fetch('api/server_config_json.php').then(r=>r.json()).then(cfg=>{
    document.getElementById('callsign').value = cfg.callsign||'';
    document.getElementById('loglevel').value = cfg.loglevel;
    M.updateTextFields();
    M.FormSelect.init(document.querySelectorAll('select'));
  });
}
document.getElementById('configform').onsubmit = function(e){
  e.preventDefault();
  let data = { callsign: document.getElementById('callsign').value, loglevel: document.getElementById('loglevel').value };
  fetch('api/config_save.php', {method:'POST',body:new URLSearchParams(data)})
    .then(r=>r.text()).then(_=>{
      document.getElementById('configmsg').innerText = 'Gespeichert!';
      getServerConfig();
      setTimeout(()=>{document.getElementById('configmsg').innerText = '';}, 1200);
    });
};

// ADMIN USER AJAX
function getAdminUser(){
  fetch('api/admin_user_json.php').then(r=>r.json()).then(cfg=>{
    document.getElementById('admin_username').value = cfg.username||'';
    M.updateTextFields();
  });
}
document.getElementById('adminform').onsubmit = function(e){
  e.preventDefault();
  let data = { username: document.getElementById('admin_username').value, newpass: document.getElementById('admin_newpass').value };
  fetch('api/admin_user_save.php', {method:'POST',body:new URLSearchParams(data)})
    .then(r=>r.text()).then(_=>{
      document.getElementById('adminmsg').innerText = 'Gespeichert!';
      getAdminUser();
      document.getElementById('admin_newpass').value = '';
      setTimeout(()=>{document.getElementById('adminmsg').innerText = '';}, 1200);
    });
};

document.addEventListener('DOMContentLoaded', function(){
  var tabs = document.querySelectorAll('.tabs');
  M.Tabs.init(tabs);
  fetchList();
  getServerConfig();
  getAdminUser();
  M.FormSelect.init(document.querySelectorAll('select'));
  document.querySelectorAll('.tabs .tab a').forEach(function(tabLink){
    tabLink.addEventListener('click', function(e){
      var showId = tabLink.getAttribute('href').substring(1);
      document.getElementById('config').style.display = (showId == 'config') ? 'block' : 'none';
      document.getElementById('monitor').style.display = (showId == 'monitor') ? 'block' : 'none';
    });
  });
  document.getElementById('config').style.display = 'block';
  document.getElementById('monitor').style.display = 'none';
});
</script>
<script>
function updateMonitor() {
  fetch('api/monitor.php').then(r=>r.text()).then(t=>{
    let areaDiv = document.getElementById('monitorArea');
    let atBottom = (areaDiv.scrollTop + areaDiv.clientHeight >= areaDiv.scrollHeight-2);
    areaDiv.innerHTML = t.replace(/\n/g,'<br>');
    if(atBottom) areaDiv.scrollTop = areaDiv.scrollHeight;
  });
}
setInterval(updateMonitor, 1000);
function clearMonitor() {
  fetch('api/monitor_clear.php').then(()=>updateMonitor());
}

function sendSmartTestFrame() {
  const message = document.getElementById('testMessage').value || 'test';
  fetch('api/smart_broadcast.php', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'message=' + encodeURIComponent(message)
  }).then(response => response.json()).then(data => {
    console.log('Smart broadcast sent:', data);
    if (data.success) {
      M.toast({html: `Smart-Broadcast gesendet! ${data.message}`, classes: 'green', displayLength: 4000});
      checkSmartStatus(); // Status aktualisieren
    } else {
      M.toast({html: 'Fehler: ' + data.error, classes: 'red'});
    }
  }).catch(error => {
    console.error('Error sending smart broadcast:', error);
    M.toast({html: 'Fehler beim Smart-Broadcast', classes: 'red'});
  });
}

function checkSmartStatus() {
  fetch('api/smart_broadcast.php')
    .then(response => response.json())
    .then(data => {
      document.getElementById('pendingCount').textContent = data.pending_notifications || 0;
      document.getElementById('sentCount').textContent = data.sent_last_hour || 0;
      document.getElementById('activeClients').textContent = data.active_clients || 0;
      
      M.toast({html: 'Smart-Polling Status aktualisiert', classes: 'blue'});
    })
    .catch(error => {
      console.error('Status check error:', error);
      M.toast({html: 'Fehler beim Status-Check', classes: 'red'});
    });
}

// Lade Monitor und Status beim Start
window.onload = function() {
  updateMonitor();
  setTimeout(checkSmartStatus, 1000); // Status nach 1s laden
};
</script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/js/materialize.min.js"></script>
<link href="https://fonts.googleapis.com/icon?family=Material+Icons" rel="stylesheet">
</body>
</html>