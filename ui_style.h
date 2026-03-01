#pragma once
#include <Arduino.h>

static const char UI_STYLE_CSS[] PROGMEM = R"CSS(
:root{--bg:#0b0f14;--card:#111823;--muted:#91a4bd;--text:#e7eef9;--accent:#7dd3fc;--danger:#fb7185;}
*{box-sizing:border-box;font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial;}
body{margin:0;background:var(--bg);color:var(--text);}
.wrap{max-width:980px;margin:0 auto;padding:18px;}
.topbar{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px;}
.brand{font-weight:800;letter-spacing:.5px;}
.nav{display:flex;gap:10px;flex-wrap:wrap;}
.navlink{color:var(--muted);text-decoration:none;padding:8px 10px;border-radius:10px;background:rgba(255,255,255,.04);}
.navlink:hover{color:var(--text);background:rgba(255,255,255,.08);}
.card{background:var(--card);border:1px solid rgba(255,255,255,.06);border-radius:16px;padding:16px;margin-bottom:14px;box-shadow:0 10px 25px rgba(0,0,0,.25);}
h2{margin:0 0 10px 0;font-size:18px;}
.row{display:flex;gap:10px;flex-wrap:wrap;margin:10px 0;}
.btn{cursor:pointer;border:0;border-radius:12px;padding:10px 12px;background:rgba(125,211,252,.16);color:var(--text);font-weight:700;}
.btn:hover{background:rgba(125,211,252,.24);}
.btn.ghost{background:rgba(255,255,255,.06);}
.btn.danger{background:rgba(251,113,133,.22);}
.btn.danger:hover{background:rgba(251,113,133,.30);}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:12px;}
.grid3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px;}
@media(max-width:760px){.grid2,.grid3{grid-template-columns:1fr;}}
.field label{display:block;color:var(--muted);font-size:13px;margin-bottom:6px;}
input,select{width:100%;padding:10px;border-radius:12px;border:1px solid rgba(255,255,255,.12);background:rgba(255,255,255,.04);color:var(--text);}
.hint{color:var(--muted);font-size:13px;margin-top:6px;line-height:1.3;}
.stat{background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.06);border-radius:14px;padding:10px;}
.stat .k{color:var(--muted);font-size:12px;}
.stat .v{font-weight:800;margin-top:3px;}
.sliderrow{display:flex;align-items:center;gap:10px;}
.pill{min-width:70px;text-align:center;padding:8px 10px;border-radius:999px;background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.08);}
)CSS";