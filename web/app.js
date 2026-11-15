(() => {
  "use strict";

  const $ = (id) => document.getElementById(id);
  const els = {
    url: $("url"),
    transport: $("transport"),
    probeBtn: $("probeBtn"),
    startBtn: $("startBtn"),
    stopBtn: $("stopBtn"),
    demoBtn: $("demoBtn"),
    usbSetupBtn: $("usbSetupBtn"),
    usbTeardownBtn: $("usbTeardownBtn"),
    usbPanel: $("usbPanel"),
    usbState: $("usbState"),
    probeInfo: $("probeInfo"),
    sensitivity: $("sensitivity"),
    videoWrap: $("videoWrap"),
    stream: $("stream"),
    videoHint: $("videoHint"),
    riskBadge: $("riskBadge"),
    metricRisk: $("metricRisk"),
    metricObjects: $("metricObjects"),
    metricFps: $("metricFps"),
    metricLatency: $("metricLatency"),
    sourceText: $("sourceText"),
    statusMessage: $("statusMessage"),
    stateBadge: $("stateBadge"),
    version: $("version"),
  };

  const state = {
    running: false,
    risk: "idle",
    streamKey: 0,
    lastFrames: -1,
  };

  function setLine(el, text, kind) {
    if (!el) return;
    el.textContent = text;
    el.className = "statusLine" + (kind ? " " + kind : "");
  }

  async function api(path, opts) {
    const options = Object.assign({ cache: "no-store" }, opts || {});
    const res = await fetch(path, options);
    const text = await res.text();
    let data;
    try {
      data = JSON.parse(text);
    } catch (_) {
      data = { ok: false, error: text || ("HTTP " + res.status) };
    }
    if (!res.ok || data.ok === false) {
      throw new Error(data.error || ("HTTP " + res.status));
    }
    return data;
  }

  function riskText(r) {
    if (r === "red") return "ROJO · DETENERSE";
    if (r === "amber") return "ÁMBAR · ATENCIÓN";
    if (r === "green") return "VERDE · AVANZAR";
    return "—";
  }

  function applyStatus(s) {
    state.running = s.state === "running" || s.state === "starting";
    state.risk = s.risk || "green";
    els.videoWrap.classList.remove("risk-idle", "risk-green", "risk-amber", "risk-red");
    els.videoWrap.classList.add("risk-" + state.risk);
    els.riskBadge.textContent = riskText(state.risk);
    els.metricRisk.textContent = (s.risk || "—").toUpperCase() +
      (typeof s.risk_value === "number" ? " " + Math.round(s.risk_value * 100) + "%" : "");
    els.metricObjects.textContent = String(s.objects ?? "—");
    els.metricFps.textContent = typeof s.fps === "number" && s.fps > 0
      ? s.fps.toFixed(1) : "—";
    els.metricLatency.textContent = typeof s.latency_ms === "number"
      ? Math.round(s.latency_ms) + " ms" : "—";
    els.sourceText.textContent = "fuente: " + (s.source || "—");
    els.version.textContent = "v" + (s.version || "1.0.0");

    if (s.state === "error") {
      els.stateBadge.textContent = "error";
      els.stateBadge.className = "badge error";
      setLine(els.statusMessage, s.message || "Error de cámara", "bad");
    } else if (state.running) {
      els.stateBadge.textContent = s.state === "starting" ? "conectando" : "en vivo";
      els.stateBadge.className = "badge running";
      setLine(els.statusMessage, s.message || "Capturando y procesando en la UNO Q", "ok");
    } else {
      els.stateBadge.textContent = "inactivo";
      els.stateBadge.className = "badge";
      setLine(els.statusMessage, s.message || "Presioná Iniciar para comenzar", "");
    }

    if (s.adb) {
      els.usbState.textContent = "adb: " + (s.adb.state || "—") +
        (s.adb.detail ? " · " + String(s.adb.detail).split("\n")[0] : "");
    }

    if (state.running) {
      if (!els.stream.getAttribute("src")) startStream();
      els.videoHint.classList.add("hidden");
    } else if (!state.running && state.lastFrames === 0) {
      els.videoHint.classList.remove("hidden");
    }
    state.lastFrames = s.frames ?? -1;
  }

  async function refresh() {
    try {
      const s = await api("/api/status");
      applyStatus(s);
    } catch (err) {
      els.stateBadge.textContent = "sin conexión";
      els.stateBadge.className = "badge error";
      setLine(els.statusMessage, "No responde la UNO Q: " + err.message, "bad");
    }
  }

  function startStream() {
    state.streamKey += 1;
    els.videoHint.classList.add("hidden");
    els.stream.src = "/stream.mjpg?t=" + Date.now() + "-" + state.streamKey;
  }

  function stopStream() {
    els.stream.removeAttribute("src");
    els.videoHint.classList.remove("hidden");
  }

  async function probe() {
    const url = els.url.value.trim();
    if (!url) {
      els.url.classList.add("invalid");
      els.url.focus();
      setLine(els.probeInfo, "Escribí la IP del teléfono primero.", "bad");
      return;
    }
    els.url.classList.remove("invalid");
    setLine(els.probeInfo, "Probando " + url + " …", "");
    els.probeBtn.disabled = true;
    try {
      const r = await api("/api/probe?url=" + encodeURIComponent(url));
      const res = Array.isArray(r.resolution) ? r.resolution : [0, 0];
      setLine(els.probeInfo,
        "Conectado: " + res[1] + "x" + res[0] + " · " + r.photo_url, "ok");
      localStorage.setItem("pasillo.url", url);
    } catch (err) {
      setLine(els.probeInfo, "No se pudo conectar: " + err.message, "bad");
    } finally {
      els.probeBtn.disabled = false;
    }
  }

  async function start() {
    let url = els.url.value.trim();
    if (!url && els.transport.value === "usb") url = "127.0.0.1:8080";
    if (!url) {
      els.url.classList.add("invalid");
      els.url.focus();
      setLine(els.probeInfo, "Falta la IP del teléfono.", "bad");
      return;
    }
    localStorage.setItem("pasillo.url", url);
    localStorage.setItem("pasillo.transport", els.transport.value);
    els.startBtn.disabled = true;
    try {
      await api("/api/start?url=" + encodeURIComponent(url) +
                "&sensitivity=" + encodeURIComponent(els.sensitivity.value),
                { method: "POST" });
      startStream();
      setLine(els.statusMessage, "Iniciando captura…", "");
      await refresh();
    } catch (err) {
      setLine(els.statusMessage, "No se pudo iniciar: " + err.message, "bad");
      els.startBtn.disabled = false;
    }
  }

  async function stop() {
    els.stopBtn.disabled = true;
    try {
      await api("/api/stop", { method: "POST" });
      stopStream();
      setLine(els.statusMessage, "Detenido.", "");
      await refresh();
    } catch (err) {
      setLine(els.statusMessage, "No se pudo detener: " + err.message, "bad");
    } finally {
      els.stopBtn.disabled = false;
      els.startBtn.disabled = false;
    }
  }

  async function demo() {
    els.startBtn.disabled = true;
    els.demoBtn.disabled = true;
    try {
      await api("/api/start?url=fake&sensitivity=medium", { method: "POST" });
      startStream();
      setLine(els.statusMessage,
        "Demo sintética: objetos en movimiento, sin cámara real.", "ok");
      await refresh();
    } catch (err) {
      setLine(els.statusMessage, "No se pudo iniciar la demo: " + err.message, "bad");
    } finally {
      els.startBtn.disabled = false;
      els.demoBtn.disabled = false;
    }
  }

  async function usbSetup() {
    setLine(els.statusMessage, "Preparando USB/ADB…", "");
    els.usbSetupBtn.disabled = true;
    try {
      const r = await api("/api/usb/setup", { method: "POST" });
      els.url.value = "127.0.0.1:8080";
      setLine(els.statusMessage, r.message || "USB listo.", "ok");
      await probe();
      await start();
    } catch (err) {
      setLine(els.statusMessage, "USB: " + err.message, "bad");
    } finally {
      els.usbSetupBtn.disabled = false;
    }
  }

  async function usbTeardown() {
    try {
      await stop();
      await api("/api/usb/teardown", { method: "POST" });
      setLine(els.statusMessage, "USB desconectado.", "");
    } catch (err) {
      setLine(els.statusMessage, "USB: " + err.message, "bad");
    }
  }

  function transportChange(save) {
    const usb = els.transport.value === "usb";
    els.usbPanel.classList.toggle("hidden", !usb);
    els.usbSetupBtn.classList.toggle("hidden", !usb);
    els.usbTeardownBtn.classList.toggle("hidden", !usb);
    if (usb && !els.url.value.trim()) els.url.value = "127.0.0.1:8080";
    if (save) localStorage.setItem("pasillo.transport", els.transport.value);
  }

  function init() {
    const savedUrl = localStorage.getItem("pasillo.url") || "";
    const savedTransport = localStorage.getItem("pasillo.transport") || "lan";
    const savedSens = localStorage.getItem("pasillo.sensitivity") || "medium";
    els.url.value = savedUrl;
    els.transport.value = savedTransport;
    els.sensitivity.value = savedSens;
    transportChange(false);

    els.probeBtn.addEventListener("click", probe);
    els.startBtn.addEventListener("click", start);
    els.stopBtn.addEventListener("click", stop);
    els.demoBtn.addEventListener("click", demo);
    els.usbSetupBtn.addEventListener("click", usbSetup);
    els.usbTeardownBtn.addEventListener("click", usbTeardown);
    els.transport.addEventListener("change", () => transportChange(true));
    els.sensitivity.addEventListener("change", () => {
      localStorage.setItem("pasillo.sensitivity", els.sensitivity.value);
    });
    els.url.addEventListener("input", () => els.url.classList.remove("invalid"));
    els.stream.addEventListener("error", () => {
      if (state.running) {
        setTimeout(() => {
          if (state.running) startStream();
        }, 1200);
      }
    });
    document.addEventListener("visibilitychange", () => {
      if (!document.hidden) {
        refresh();
        if (state.running) startStream();
      }
    });

    refresh();
    setInterval(refresh, 800);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
