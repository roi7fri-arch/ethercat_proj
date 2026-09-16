import React, { useEffect, useState } from "react";
import {
  busStatus, busScan, busDownload, busUpload, busSendFiles,
} from "./api.js";

/*
 * Live bus operations.
 *
 * This is the only tab that touches hardware. Everything here goes through the
 * backend to src/tools/ecat_param_tool, which links the same SOEM master as the
 * production application - so what the GUI writes is what the machine will see.
 *
 * Two deliberate constraints:
 *  - Downloading works on *saved* files, not on the in-browser draft. The tool
 *    reads the file from disk, so what gets written is exactly what is on disk
 *    and can be diffed, archived and re-applied later.
 *  - Parameters are written in PRE-OP. The mailbox is alive so CoE works, but
 *    no process data is exchanged and nothing is energised.
 */

function Result({ result }) {
  if (!result) return null;

  const rows = result.results || [];
  const bad = (r) => r.status !== "ok";

  return (
    <div className={"bus-result " + (result.ok ? "ok" : "bad")}>
      <div className="bus-result-head">
        <strong>{result.ok ? "Completed" : "Finished with problems"}</strong>
        {result.summary && (
          <span className="muted">
            {result.summary.ok} ok · {result.summary.mismatched} mismatched ·{" "}
            {result.summary.failed} failed
          </span>
        )}
        {result.error && <span className="err">{result.error}</span>}
      </div>

      {result.slaves && (
        <table className="param-table">
          <thead>
            <tr>
              <th>Pos</th><th>Name</th><th>Vendor</th><th>Product</th>
              <th>Rev</th><th>CoE</th><th>FoE</th><th>Profile in config</th>
            </tr>
          </thead>
          <tbody>
            {result.slaves.map((s) => (
              <tr key={s.position}>
                <td>{s.position}</td>
                <td>{s.name}</td>
                <td className="mono">{s.vendor_id}</td>
                <td className="mono">{s.product_code}</td>
                <td className="mono">{s.revision}</td>
                <td className="mid">{s.supports_coe ? "yes" : "—"}</td>
                <td className="mid">{s.supports_foe ? "yes" : "—"}</td>
                <td>{s.configured_profile || <span className="muted">not in config</span>}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}

      {rows.length > 0 && (
        <table className="param-table">
          <thead>
            <tr>
              <th>Slave</th><th>Object</th><th>Name</th><th>Type</th>
              <th>Wanted</th><th>Read back</th><th>Result</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((r, i) => (
              <tr key={i} className={bad(r) ? "row-bad" : ""}>
                <td>{r.slave}</td>
                <td className="mono">{r.index}:{String(r.subindex).padStart(2, "0")}</td>
                <td>{r.name}</td>
                <td>{r.type}</td>
                <td className="mono">{r.wanted}</td>
                <td className="mono">{r.readback === null ? "—" : r.readback}</td>
                <td>{r.status}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}

      {result.log && <pre className="bus-log">{result.log}</pre>}
    </div>
  );
}

export default function BusPanel({
  iface, configPath, paramsPath, onParamsPath, dirty, onLoadParams,
}) {
  const [status, setStatus] = useState(null);
  const [busy, setBusy] = useState("");
  const [result, setResult] = useState(null);
  const [error, setError] = useState("");
  const [verify, setVerify] = useState(true);
  const [capturePath, setCapturePath] = useState("drive-live.params.json");
  const [confirmed, setConfirmed] = useState(false);

  useEffect(() => {
    busStatus().then(setStatus).catch((e) => setError(e.message));
  }, []);

  async function run(label, fn) {
    setBusy(label);
    setError("");
    setResult(null);
    try {
      setResult(await fn());
    } catch (e) {
      setError(e.message);
    } finally {
      setBusy("");
    }
  }

  const ready = status && status.available;
  const canWrite = ready && iface && paramsPath && confirmed;

  return (
    <div className="bus">
      <section className="card">
        <div className="sdo-head">
          <h2>Bus</h2>
          <span className="muted">
            operations run on the segment attached to this PC
          </span>
        </div>

        {!status && <p className="muted">checking for the bus tool…</p>}

        {status && !status.available && (
          <div className="banner bad">
            <strong>Bus tool not available.</strong>
            <p>{status.hint}</p>
          </div>
        )}

        {status && status.available && status.hint && (
          <div className="banner warn">
            <pre>{status.hint}</pre>
          </div>
        )}

        <div className="grid">
          <label>Interface
            <input value={iface} readOnly
              title="Set on the General tab — the same interface the master will use." />
          </label>
          <label>Bus config file
            <input value={configPath} readOnly
              title="Set on the General tab. Optional here: it lets the scan show which profile each slave is configured as." />
          </label>
          <label className="wide">Parameter set file
            <input value={paramsPath}
              placeholder="drive.params.json"
              onChange={(e) => onParamsPath(e.target.value)} />
          </label>
        </div>

        <p className="muted">
          Bus operations read the parameter set <em>from disk</em>, not from the
          editor. Save on the Parameters tab first, so what reaches the drives is
          exactly the file you can archive and re-apply later.
        </p>
        {dirty && (
          <div className="banner warn">
            The parameter editor has unsaved changes. Save them before
            downloading, or the drives will get the previous version.
          </div>
        )}
      </section>

      <section className="card">
        <h3>Inspect</h3>
        <p className="muted">
          Safe: brings the segment to PRE-OP and reads. Nothing is written and
          no axis is energised.
        </p>
        <div className="bus-actions">
          <button disabled={!ready || !iface || !!busy}
            onClick={() => run("scan", () =>
              busScan({ interface: iface, config_path: configPath || null }))}>
            {busy === "scan" ? "Scanning…" : "Scan bus"}
          </button>

          <button disabled={!ready || !iface || !paramsPath || !!busy}
            onClick={() => run("upload", () =>
              busUpload({
                interface: iface,
                params_path: paramsPath,
                out_path: capturePath || null,
              }))}>
            {busy === "upload" ? "Reading…" : "Read values from drives"}
          </button>

          <label className="inline">Capture to
            <input value={capturePath}
              placeholder="leave blank to only display"
              onChange={(e) => setCapturePath(e.target.value)} />
          </label>

          {capturePath && onLoadParams && (
            <button disabled={!!busy} onClick={() => onLoadParams(capturePath)}>
              Open captured file
            </button>
          )}
        </div>
      </section>

      <section className="card danger-zone">
        <h3>Write to drives</h3>
        <p className="muted">
          Writes happen in PRE-OP with the drives disabled. Even so, this changes
          machine behaviour: check the axes are safe and, for firmware, that
          power will not be interrupted mid-transfer.
        </p>

        <label className="check confirm">
          <input type="checkbox" checked={confirmed}
            onChange={(e) => setConfirmed(e.target.checked)} />
          The machine is in a safe state and I want to write to it
        </label>

        <div className="bus-actions">
          <label className="check">
            <input type="checkbox" checked={verify}
              onChange={(e) => setVerify(e.target.checked)} />
            Read every object back and compare
          </label>

          <button className="primary" disabled={!canWrite || !!busy}
            onClick={() => run("download", () =>
              busDownload({
                interface: iface,
                params_path: paramsPath,
                config_path: configPath || null,
                verify,
              }))}>
            {busy === "download" ? "Writing…" : "Download parameters"}
          </button>

          <button className="danger" disabled={!canWrite || !!busy}
            onClick={() => run("files", () =>
              busSendFiles({ interface: iface, params_path: paramsPath }))}>
            {busy === "files" ? "Transferring…" : "Send files (FoE)"}
          </button>
        </div>

        <p className="muted">
          Verification is what catches a drive that accepts a write and then
          silently clamps the value to its own legal range — the write succeeds,
          the read back differs, and the row is flagged.
        </p>
      </section>

      {error && <div className="banner bad">{error}</div>}
      <Result result={result} />
    </div>
  );
}
