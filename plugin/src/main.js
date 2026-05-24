/*
 * chromacal panel logic.
 *
 * Flow:  grab a frame  ->  native solve (chromacal.uxpaddon)  ->  .cube
 *        ->  apply to the selected clip's Lumetri Input LUT (or save the file).
 *
 * The Premiere host glue is inlined here (rather than a separate module) because
 * UXP resolves require() relative to the plugin root, which makes cross-file
 * relative requires from src/ unreliable.
 */

const { storage } = require("uxp");
const fs = storage.localFileSystem;

// Native addon. In UXP, require("*.uxpaddon") resolves asynchronously (returns
// a Promise), so we must await it before use.
let addon = null;
async function getAddon() {
    if (addon) return addon;
    try {
        const mod = require("chromacal.uxpaddon");
        addon = (mod && typeof mod.then === "function") ? await mod : mod;
    } catch (e) {
        addon = null;
        console.log("[chromacal] addon load failed:", String(e && e.message || e));
    }
    if (addon) {
        console.log("[chromacal] addon ready; solveFromPng type =", typeof addon.solveFromPng);
    }
    return addon;
}

// Premiere API (absent when run outside Premiere).
let ppro = null;
try { ppro = require("premierepro"); } catch (e) { ppro = null; }

// ---------------------------------------------------------------------------
// Premiere host glue
// ---------------------------------------------------------------------------

function inPremiere() { return !!ppro; }

function splitPath(nativePath) {
    const i = Math.max(nativePath.lastIndexOf("/"), nativePath.lastIndexOf("\\"));
    return { dir: nativePath.substring(0, i + 1), name: nativePath.substring(i + 1) };
}

async function frameSize(sequence) {
    try {
        if (sequence.getVideoFrameDimensions) {
            const d = await sequence.getVideoFrameDimensions();
            if (d && d.width && d.height) return { w: d.width, h: d.height };
        }
    } catch (e) { /* fall through */ }
    return { w: 1920, h: 1080 };
}

// Export the current sequence frame (at the playhead) to a plain filesystem
// path. We deliberately avoid the UXP sandbox temp folder: Premiere's native
// exporter (running outside the UXP sandbox) can't write there. A /tmp path is
// writable by Premiere and readable by the native addon — neither goes through
// the UXP file API. Premiere UXP DOM calls are async on this build, so await.
async function exportCurrentFrame(outPath, outDir) {
    if (!ppro) throw new Error("Not running inside Premiere Pro.");
    const project = await ppro.Project.getActiveProject();
    if (!project) throw new Error("No active project.");
    const sequence = await project.getActiveSequence();
    if (!sequence) throw new Error("No active sequence — open one in the timeline.");
    if (!ppro.Exporter || typeof ppro.Exporter.exportSequenceFrame !== "function") {
        throw new Error("Frame export needs Premiere 25.0+; use “Calibrate from image file”.");
    }
    const time = await sequence.getPlayerPosition();
    const { w, h } = await frameSize(sequence);
    const ok = await ppro.Exporter.exportSequenceFrame(sequence, time, outPath, outDir, w, h);
    if (!ok) throw new Error("Exporter.exportSequenceFrame returned false.");
    return true;
}

// Best-effort: set the selected clip's Lumetri Input LUT. Returns false to
// signal the caller to fall back to manual instructions.
async function applyInputLut(nativePath) {
    if (!ppro) return false;
    try {
        const project = await ppro.Project.getActiveProject();
        const sequence = project && (await project.getActiveSequence());
        if (!sequence || !sequence.getSelection) return false;
        const selection = await sequence.getSelection();
        const clips = selection && selection.getTrackItems ? (await selection.getTrackItems()) : [];
        if (!clips || clips.length === 0) return false;

        let applied = false;
        for (const clip of clips) {
            if (!clip.getComponentChain) continue;
            const chain = await clip.getComponentChain();
            if (!chain || !chain.getComponentCount) continue;
            const n = await chain.getComponentCount();
            for (let i = 0; i < n; i++) {
                const c = await chain.getComponentAtIndex(i);
                if (await setLutOnLumetri(c, nativePath)) applied = true;
            }
        }
        return applied;
    } catch (e) {
        return false;
    }
}

async function setLutOnLumetri(component, nativePath) {
    try {
        let name = "";
        if (component.getMatchName) name = await component.getMatchName();
        if (!name && component.getDisplayName) name = await component.getDisplayName();
        if (typeof name !== "string" || !/lumetri/i.test(name)) return false;
        let count = 0;
        if (component.getParamCount) count = await component.getParamCount();
        else if (component.getParameterCount) count = await component.getParameterCount();
        for (let i = 0; i < count; i++) {
            let p = null;
            if (component.getParam) p = await component.getParam(i);
            else if (component.getParameterAtIndex) p = await component.getParameterAtIndex(i);
            if (!p) continue;
            let dn = "";
            if (p.getDisplayName) dn = await p.getDisplayName();
            else dn = p.displayName || "";
            if (typeof dn === "string" && /input *lut/i.test(dn) && p.setValue) {
                await p.setValue(nativePath);
                return true;
            }
        }
    } catch (e) { /* unsupported on this build */ }
    return false;
}

// ---------------------------------------------------------------------------
// Panel UI
// ---------------------------------------------------------------------------

let lastCubeEntry = null;
let lastPresetEntry = null;
const $ = (id) => document.getElementById(id);

function setStatus(msg, kind) {
    const el = $("status");
    el.textContent = msg;
    el.className = kind || "";
}

function showResults(r) {
    $("results").style.display = "block";
    $("rPatches").textContent = String(r.patches);
    $("rRel").textContent = Number(r.minReliability).toFixed(3);
    $("rErr").textContent = Number(r.finalError).toFixed(1);
    const m = r.ccm.map((v) => v.toFixed(3).padStart(7));
    $("rCcm").textContent =
        `CCM\n[${m[0]} ${m[1]} ${m[2]}]\n[${m[3]} ${m[4]} ${m[5]}]\n[${m[6]} ${m[7]} ${m[8]}]`;
    $("btnSavePreset").disabled = !r.presetWritten;
    $("btnApply").disabled = !inPremiere();
    $("btnSave").disabled = false;
}

async function solve(frameNativePath) {
    const a = await getAddon();
    if (!a || typeof a.solveFromPng !== "function") {
        setStatus("Native addon not loaded (chromacal.uxpaddon). See plugin/README.md.", "err");
        return null;
    }
    const tmp = await fs.getTemporaryFolder();
    lastCubeEntry = await tmp.createFile("chromacal.cube", { overwrite: true });
    lastPresetEntry = await tmp.createFile("chromacal.cmcal", { overwrite: true });
    setStatus("Detecting chart and solving…");
    await new Promise((res) => setTimeout(res, 0));
    // Full-resolution analysis -> writes a .cmcal preset (for the native effect)
    // and a .cube (for Lumetri).
    const r = a.solveFromPng(frameNativePath, lastCubeEntry.nativePath,
                             lastPresetEntry.nativePath, 33);
    console.log("[chromacal] solve:", JSON.stringify({ ok: r.ok, patches: r.patches,
        minReliability: r.minReliability, error: r.error, ccm: r.ccm }));
    if (!r.ok) { setStatus(r.error || "Calibration failed.", "err"); return null; }
    showResults(r);
    setStatus(`Calibrated from ${r.patches} patches.`, "ok");
    return r;
}

async function onCalibrateFromFrame() {
    try {
        if (!inPremiere()) { setStatus("Not in Premiere — use “Calibrate from image file”.", "err"); return; }
        // Plain filesystem path Premiere can write and the addon can read.
        const outPath = "/tmp/chromacal_frame.png";
        setStatus("Exporting current frame…");
        await exportCurrentFrame(outPath, "/tmp/");
        await solve(outPath);
    } catch (e) { setStatus(String(e.message || e), "err"); }
}

// Parity helper: export the current sequence frame (with whatever effects are on
// the clip — e.g. chromacal with Apply ON) at full resolution. Toggle the effect's
// Apply off/on between exports to get the raw and applied frames for ppro_parity.sh.
async function onExportFrame() {
    try {
        if (!inPremiere()) { setStatus("Not in Premiere.", "err"); return; }
        const ts = Date.now();
        const outPath = "/tmp/cc_export_" + ts + ".png";
        setStatus("Exporting current frame…");
        await exportCurrentFrame(outPath, "/tmp/");
        setStatus("Exported " + outPath + " (effect state as shown). For parity: "
                  + "export once with Apply OFF (raw) and once ON (applied).", "ok");
    } catch (e) { setStatus(String(e.message || e), "err"); }
}

async function onCalibrateFromFile() {
    try {
        const file = await fs.getFileForOpening({ types: ["png", "tif", "tiff", "jpg", "jpeg"] });
        if (!file) return;
        await solve(file.nativePath);
    } catch (e) { setStatus(String(e.message || e), "err"); }
}

async function onApply() {
    if (!lastCubeEntry) return;
    const applied = await applyInputLut(lastCubeEntry.nativePath);
    setStatus(applied
        ? "Applied LUT to selected clip's Lumetri Input LUT."
        : "Couldn't set the LUT automatically. Use “Save .cube…”, then Lumetri Color → Basic Correction → Input LUT → Browse…",
        applied ? "ok" : "err");
}

async function onSave() {
    if (!lastCubeEntry) return;
    try {
        const dest = await fs.getFileForSaving("chromacal.cube");
        if (!dest) return;
        const data = await lastCubeEntry.read({ format: storage.formats.binary });
        await dest.write(data, { format: storage.formats.binary });
        setStatus("Saved .cube.", "ok");
    } catch (e) { setStatus(String(e.message || e), "err"); }
}

// Save the calibration preset for the native chromacal effect's Load calibration.
async function onSavePreset() {
    if (!lastPresetEntry) return;
    try {
        const dest = await fs.getFileForSaving("chromacal.cmcal");
        if (!dest) return;
        const data = await lastPresetEntry.read({ format: storage.formats.binary });
        await dest.write(data, { format: storage.formats.binary });
        setStatus("Saved calibration preset. In the clip's chromacal effect: "
                  + "Load calibration… → pick this file, then tick Apply.", "ok");
    } catch (e) { setStatus(String(e.message || e), "err"); }
}

$("btnFrame").addEventListener("click", onCalibrateFromFrame);
$("btnFile").addEventListener("click", onCalibrateFromFile);
$("btnExport").addEventListener("click", onExportFrame);
$("btnSavePreset").addEventListener("click", onSavePreset);
$("btnApply").addEventListener("click", onApply);
$("btnSave").addEventListener("click", onSave);

(async () => {
    const a = await getAddon();
    if (!a || typeof a.solveFromPng !== "function")
        setStatus("Native addon not loaded — build chromacal.uxpaddon (plugin/README.md).", "err");
    else if (!inPremiere())
        setStatus("Ready. (Frame export needs Premiere; file mode works anywhere.)");
    else
        setStatus("Ready.");
})();
