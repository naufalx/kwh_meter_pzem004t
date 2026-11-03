
/* =========================================================
   KWH METER - UNIFIED SCRIPT
   WebSocket ESP32 saat ini menggunakan port 81.
   Jika ESP32 memakai /ws pada port yang sama, ubah WS_URL.
========================================================= */

let socket = null;
let reconnectTimer = null;
let lastDataTime = 0;
let reconnecting = false;

/* Jika ESP32 WebSocket server memakai port 81: */
const WS_URL = `ws://${window.location.hostname}:81/`;


/* ---------- RIWAYAT PERSISTEN DI BROWSER ---------- */
const DB_NAME = "KWHMeterDB";
const DB_VERSION = 1;
const STORE_NAME = "riwayat";

function bukaDatabase(){
    return new Promise((resolve,reject)=>{
        const req=indexedDB.open(DB_NAME,DB_VERSION);
        req.onupgradeneeded=()=>{
            const db=req.result;
            if(!db.objectStoreNames.contains(STORE_NAME)){
                const store=db.createObjectStore(STORE_NAME,{keyPath:"id",autoIncrement:true});
                store.createIndex("waktu","waktu",{unique:false});
            }
        };
        req.onsuccess=()=>resolve(req.result);
        req.onerror=()=>reject(req.error);
    });
}
async function simpanRiwayat(data){
    const db=await bukaDatabase();
    return new Promise((resolve,reject)=>{
        const tx=db.transaction(STORE_NAME,"readwrite");
        tx.objectStore(STORE_NAME).add({
            waktu:new Date().toISOString(),
            voltage:Number(data.voltage),
            current:Number(data.current),
            powerFactor:Number(data.powerFactor ?? data.pf),
            activePower:Number(data.activePower ?? data.power),
            energy:Number(data.energy)
        });
        tx.oncomplete=()=>{db.close();resolve();};
        tx.onerror=()=>{const err=tx.error;db.close();reject(err);};
        tx.onabort=()=>{const err=tx.error;db.close();reject(err);};
    });
}
async function ambilRiwayat(){
    const db=await bukaDatabase();
    return new Promise((resolve,reject)=>{
        const tx=db.transaction(STORE_NAME,"readonly");
        const req=tx.objectStore(STORE_NAME).getAll();
        req.onsuccess=()=>{const rows=req.result;db.close();resolve(rows);};
        req.onerror=()=>{const err=req.error;db.close();reject(err);};
    });
}
function csvCell(value){
    return '"' + String(value ?? "").replace(/"/g,'""') + '"';
}
async function eksporCSV(){
    try{
        const rows=await ambilRiwayat();
        if(!rows.length){alert("Belum ada data riwayat untuk diekspor.");return;}
        rows.sort((a,b)=>a.id-b.id);
        const headers=["Waktu","Tegangan_V","Arus_A","Faktor_Daya","Daya_Aktif_W","Energi_kWh"];
        const lines=[headers.map(csvCell).join(";")];
        rows.forEach(r=>lines.push([
            new Date(r.waktu).toLocaleString("id-ID"),
            r.voltage,r.current,r.powerFactor,r.activePower,r.energy
        ].map(csvCell).join(";")));
        const blob=new Blob(["\\uFEFF"+lines.join("\\r\\n")],{type:"text/csv;charset=utf-8;"});
        const url=URL.createObjectURL(blob);
        const a=document.createElement("a");
        a.href=url;a.download="riwayat_kwh_meter.csv";
        document.body.appendChild(a);a.click();a.remove();
        setTimeout(()=>URL.revokeObjectURL(url),1000);
    }catch(err){console.error(err);alert("Ekspor CSV gagal: "+err.message);}
}
async function renderSavedHistory(){
    const body=$("historyBody");
    if(!body || location.pathname.split("/").pop()!=="riwayat.html") return;
    try{
        const rows=await ambilRiwayat();
        rows.sort((a,b)=>b.id-a.id);
        body.replaceChildren();
        if(!rows.length){
            const tr=document.createElement("tr"),td=document.createElement("td");
            td.colSpan=6;td.textContent="Belum ada data tersimpan. Buka dashboard dan tunggu data diterima.";
            tr.appendChild(td);body.appendChild(tr);return;
        }
        rows.slice(0,1000).forEach(r=>{
            const tr=document.createElement("tr");
            [
                new Date(r.waktu).toLocaleString("id-ID"),
                formatNumber(r.voltage,1),formatNumber(r.current,2),
                formatNumber(r.powerFactor,2),formatNumber(r.activePower,1),
                formatNumber(r.energy,3)
            ].forEach(v=>{const td=document.createElement("td");td.textContent=v;tr.appendChild(td);});
            body.appendChild(tr);
        });
    }catch(err){console.error("Gagal memuat riwayat",err);}
}

/* ---------- helper ---------- */
function $(id){ return document.getElementById(id); }

function formatNumber(value, digits=2){
    const n = Number(value);
    return Number.isFinite(n) ? n.toFixed(digits) : "--";
}

function setText(id,value){
    const el=$(id);
    if(el && value !== undefined && value !== null) el.textContent=value;
}

function setMeter(selector, percent){
    const el=document.querySelector(selector);
    if(el) el.style.width=Math.max(0,Math.min(100,percent))+"%";
}

/* ---------- SIDEBAR ---------- */
function setupSidebar(){
    const sidebar=document.querySelector(".sidebar");
    const menu=document.querySelector(".menu");
    const overlay=$("sidebarOverlay");

    if(!sidebar || !menu || !overlay) return;

    function openMenu(){
        sidebar.classList.add("open");
        overlay.classList.add("show");
        document.body.classList.add("menu-open");
    }

    function closeMenu(){
        sidebar.classList.remove("open");
        overlay.classList.remove("show");
        document.body.classList.remove("menu-open");
    }

    menu.addEventListener("click",()=>{
        sidebar.classList.contains("open") ? closeMenu() : openMenu();
    });

    overlay.addEventListener("click",closeMenu);

    document.querySelectorAll(".nav-item").forEach(item=>{
        item.addEventListener("click",()=>{
            if(window.innerWidth <= 800) closeMenu();
        });
    });

    window.addEventListener("resize",()=>{
        if(window.innerWidth > 800) closeMenu();
    });
}

/* ---------- ACTIVE PAGE ---------- */
function setActivePage(){
    let current=location.pathname.split("/").pop();
    if(!current) current="index.html";

    document.querySelectorAll(".nav-item").forEach(item=>{
        const href=(item.getAttribute("href")||"").split("/").pop();
        item.classList.toggle("active",href===current);
    });
}

/* ---------- CLOCK ---------- */
function updateClock(){
    const now=new Date();
    const dateEl=$("sideDate");
    const timeEl=$("sideTime");

    if(dateEl){
        dateEl.textContent=now.toLocaleDateString("id-ID",{
            day:"2-digit",month:"long",year:"numeric"
        });
    }
    if(timeEl){
        timeEl.textContent=now.toLocaleTimeString("id-ID",{
            hour:"2-digit",minute:"2-digit",second:"2-digit"
        });
    }
}

/* ---------- STATUS ---------- */
function setStatus(online){
    const text=$("connectionStatus");
    const box=$("espStatus");
    const dot=box ? box.querySelector("i") : null;

    if(text) text.textContent=online ? "Online" : "Offline";
    if(text) text.style.color=online ? "var(--green)" : "var(--danger)";
    if(box) box.classList.toggle("offline",!online);
    if(dot){
        dot.classList.toggle("offline",!online);
    }

    document.querySelectorAll("[data-connection]").forEach(el=>{
        el.textContent=online ? "Terhubung" : "Terputus";
        el.classList.toggle("offline",!online);
    });
}

/* ---------- WEBSOCKET ---------- */
function connectWebSocket(){
    if(socket && (socket.readyState===WebSocket.OPEN || socket.readyState===WebSocket.CONNECTING)) return;

    console.log("Menghubungkan WebSocket:",WS_URL);

    try{
        socket=new WebSocket(WS_URL);

        socket.onopen=()=>{
            console.log("ESP32 WebSocket terhubung");
            reconnecting=false;
            lastDataTime=Date.now();
            setStatus(true);
        };

        socket.onmessage=(event)=>{
            try{
                const data=JSON.parse(event.data);
                lastDataTime=Date.now();
                setStatus(true);
                updateDashboard(data);
                simpanRiwayat(data).then(()=>{
                    const count=$("historyCount");
                    if(count) ambilRiwayat().then(rows=>count.textContent=rows.length);
                }).catch(err=>console.error("Gagal menyimpan riwayat:",err));
            }catch(error){
                console.error("JSON WebSocket tidak valid:",error);
            }
        };

        socket.onclose=()=>{
            console.log("ESP32 terputus");
            setStatus(false);
            scheduleReconnect();
        };

        socket.onerror=(error)=>{
            console.error("WebSocket error:",error);
            setStatus(false);
        };
    }catch(error){
        console.error("Gagal membuat WebSocket:",error);
        setStatus(false);
        scheduleReconnect();
    }
}

function scheduleReconnect(){
    if(reconnecting) return;
    reconnecting=true;
    clearTimeout(reconnectTimer);
    reconnectTimer=setTimeout(()=>{
        reconnecting=false;
        connectWebSocket();
    },2000);
}

/* ---------- DATA ---------- */
function updateDashboard(data){
    /* Mendukung dua nama key:
       powerFactor / pf
       activePower / power
    */
    const voltage=data.voltage;
    const current=data.current;
    const pf=data.powerFactor ?? data.pf;
    const power=data.activePower ?? data.power;
    const energy=data.energy;

    setText("voltage",formatNumber(voltage,1));
    setText("current",formatNumber(current,2));
    setText("powerFactor",formatNumber(pf,2));
    setText("activePower",formatNumber(power,1));
    setText("energyTotal",formatNumber(energy,2));

    setText("tVoltage",formatNumber(voltage,1));
    setText("tCurrent",formatNumber(current,2));
    setText("tPf",formatNumber(pf,2));
    setText("tPower",formatNumber(power,1));
    setText("tEnergy",formatNumber(energy,2));

    setText("energy",formatNumber(energy,2));
    setText("energyVoltage",formatNumber(voltage,1)+" V");
    setText("energyCurrent",formatNumber(current,2)+" A");
    setText("energyPower",formatNumber(power,1)+" W");

    /* progress meter */
    if(Number.isFinite(Number(voltage))) setMeter(".voltage .meter span",Number(voltage)/250*100);
    if(Number.isFinite(Number(current))) setMeter(".current .meter span",Number(current)/10*100);
    if(Number.isFinite(Number(pf))) setMeter(".pf .meter span",Number(pf)*100);
    if(Number.isFinite(Number(power))) setMeter(".power .meter span",Number(power)/1000*100);

    updateHistory(data);
}

/* ---------- HISTORY ---------- */
function updateHistory(data){
    const body=$("historyBody");
    if(!body) return;

    if(Array.isArray(data.history)){
        body.innerHTML="";
        data.history.slice().reverse().forEach(row=>{
            const tr=document.createElement("tr");
            const values=[
                row.time ?? "--",
                row.voltage ?? "--",
                row.current ?? "--",
                row.powerFactor ?? row.pf ?? "--",
                row.activePower ?? row.power ?? "--",
                row.energy ?? "--"
            ];
            values.forEach(v=>{
                const td=document.createElement("td");
                td.textContent=v;
                tr.appendChild(td);
            });
            body.appendChild(tr);
        });
        return;
    }

    /* Kalau ESP hanya mengirim satu data, tambahkan sebagai baris terbaru. */
    if(body.dataset.liveRows==="true"){
        const tr=document.createElement("tr");
        [
            new Date().toLocaleTimeString("id-ID"),
            formatNumber(data.voltage,1),
            formatNumber(data.current,2),
            formatNumber(data.powerFactor ?? data.pf,2),
            formatNumber(data.activePower ?? data.power,1),
            formatNumber(data.energy,2)
        ].forEach(v=>{
            const td=document.createElement("td");
            td.textContent=v;
            tr.appendChild(td);
        });
        body.prepend(tr);
        while(body.children.length>20) body.removeChild(body.lastElementChild);
    }
}

/* ---------- SETTINGS ---------- */
function saveSettings(){
    const name=$("deviceName");
    const interval=$("interval");
    const info=$("saveInfo");

    if(name) localStorage.setItem("deviceName",name.value);
    if(interval) localStorage.setItem("interval",interval.value);

    if(info){
        info.textContent="✓ Tersimpan";
        setTimeout(()=>info.textContent="",2000);
    }
}

function loadSettings(){
    const name=$("deviceName");
    const interval=$("interval");
    if(name) name.value=localStorage.getItem("deviceName") || "KWH METER ESP32";
    if(interval) interval.value=localStorage.getItem("interval") || "1000";
}

/* ---------- CONNECTION WATCHDOG ---------- */
setInterval(()=>{
    if(lastDataTime>0 && Date.now()-lastDataTime>4000){
        setStatus(false);
    }
},1000);

/* ---------- START ---------- */
document.addEventListener("DOMContentLoaded",()=>{
    setupSidebar();
    setActivePage();
    updateClock();
    setInterval(updateClock,1000);
    loadSettings();
    connectWebSocket();
    renderSavedHistory();
    const exportBtn=$("exportCsv");
    if(exportBtn) exportBtn.addEventListener("click",eksporCSV);
    const refreshBtn=$("refreshHistory");
    if(refreshBtn) refreshBtn.addEventListener("click",renderSavedHistory);
    const exportDash=$("exportDashboard");
    if(exportDash) exportDash.addEventListener("click",eksporCSV);
});
