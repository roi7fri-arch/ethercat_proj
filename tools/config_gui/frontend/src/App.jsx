import React, { useEffect, useMemo, useState } from "react";
import PdoTable from "./PdoTable.jsx";
import { getMeta, saveConfig, loadConfig } from "./api.js";

const clone = (o) => JSON.parse(JSON.stringify(o));

function modeLabel(modes, value) {
  const m = modes.find((x) => x.value === value);
  return m ? m.label : String(value);
}

export default function App() {
  const [meta, setMeta] = useState(null);
  const [config, setConfig] = useState(null);
  const [selected, setSelected] = useState(0);
  const [path, setPath] = useState("ethercat_config.json");
  const [toast, setToast] = useState(null);
  const [preview, setPreview] = useState(false);

  useEffect(() => {
    getMeta()
      .then((m) => {
        setMeta(m);
        setConfig(clone(m.default_config));
      })
      .catch((e) => setToast({ kind: "error", msg: "Backend not reachable: " + e.message }));
  }, []);

  const slave = config && config.slaves[selected];

  function flash(kind, msg) {
    setToast({ kind, msg });
    setTimeout(() => setToast(null), 3500);
  }

  function updateNetwork(field, value) {
    setConfig({ ...config, network: { ...config.network, [field]: value } });
  }

  function updateSlave(patch) {
    const slaves = config.slaves.slice();
    slaves[selected] = { ...slaves[selected], ...patch };
    setConfig({ ...config, slaves });
  }

  function addSlave() {
    const slaves = config.slaves.concat({
      name: "Elmo Platinum",
      mode_of_operation: 8,
      rxpdo: clone(meta.mode_templates["8"].rx),
      txpdo: clone(meta.mode_templates["8"].tx),
    });
    setConfig({ ...config, slaves });
    setSelected(slaves.length - 1);
  }

  function duplicateSlave() {
    const slaves = config.slaves.slice();
    slaves.splice(selected + 1, 0, clone(slaves[selected]));
    setConfig({ ...config, slaves });
    setSelected(selected + 1);
  }

  function removeSlave() {
    if (config.slaves.length === 0) return;
    const slaves = config.slaves.filter((_, i) => i !== selected);
    setConfig({ ...config, slaves });
    setSelected(Math.max(0, selected - 1));
  }

  function loadTemplate() {
    const tmpl = meta.mode_templates[String(slave.mode_of_operation)];
    if (!tmpl) {
      flash("error", "No template for " + modeLabel(meta.modes, slave.mode_of_operation));
      return;
    }
    updateSlave({ rxpdo: clone(tmpl.rx), txpdo: clone(tmpl.tx) });
    flash("ok", "Loaded " + modeLabel(meta.modes, slave.mode_of_operation).split(" ")[0] + " template");
  }

  async function doSave() {
    try {
      const res = await saveConfig(path, config);
      flash("ok", "Saved " + res.bytes + " bytes → " + res.path);
    } catch (e) {
      flash("error", "Save failed: " + e.message);
    }
  }

  async function doLoad() {
    try {
      const cfg = await loadConfig(path);
      setConfig(cfg);
      setSelected(0);
      flash("ok", "Loaded " + path);
    } catch (e) {
      flash("error", "Load failed: " + e.message);
    }
  }

  const previewJson = useMemo(
    () => (config ? JSON.stringify(config, null, 2) : ""),
    [config]
  );

  if (!meta || !config) {
    return (
      <div className="loading">
        {toast ? <div className="error">{toast.msg}</div> : "Loading…"}
      </div>
    );
  }

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <span className="logo">⚙</span>
          <div>
            <h1>EtherCAT Config Builder</h1>
            <span className="muted">generates the JSON the master reads on init</span>
          </div>
        </div>
        <div className="file-bar">
          <input
            className="path"
            value={path}
            onChange={(e) => setPath(e.target.value)}
            placeholder="config path"
          />
          <button onClick={doLoad}>Load</button>
          <button className="primary" onClick={doSave}>Save</button>
          <button onClick={() => setPreview(true)}>Preview JSON</button>
        </div>
      </header>

      <section className="network card">
        <h2>Network</h2>
        <div className="grid">
          <label>Interface
            <input value={config.network.interface}
              onChange={(e) => updateNetwork("interface", e.target.value)} />
          </label>
          <label>Redundant interface (2nd NIC)
            <input value={config.network.redundant_interface || ""}
              placeholder="blank = no cable redundancy"
              title="Optional second NIC for EtherCAT cable redundancy (ec_init_redundant). Leave blank to use a single interface."
              onChange={(e) => updateNetwork("redundant_interface", e.target.value)} />
          </label>
          <label>Cycle time (µs)
            <input type="number" value={config.network.cycle_time_us}
              onChange={(e) => updateNetwork("cycle_time_us", Number(e.target.value))} />
          </label>
          <label>Cycles (0 = ∞)
            <input type="number" value={config.network.number_of_cycles}
              onChange={(e) => updateNetwork("number_of_cycles", Number(e.target.value))} />
          </label>
          <label>SYNC0 shift (µs)
            <input type="number" value={config.network.sync0_shift_us}
              onChange={(e) => updateNetwork("sync0_shift_us", Number(e.target.value))} />
          </label>
          <label>Sync Kp divisor
            <input type="number" value={config.network.sync_kp_div}
              onChange={(e) => updateNetwork("sync_kp_div", Number(e.target.value))} />
          </label>
          <label>Sync Ki divisor
            <input type="number" value={config.network.sync_ki_div}
              onChange={(e) => updateNetwork("sync_ki_div", Number(e.target.value))} />
          </label>
          <label className="check">
            <input type="checkbox" checked={config.network.distributed_clock}
              onChange={(e) => updateNetwork("distributed_clock", e.target.checked)} />
            Distributed Clock (SYNC0)
          </label>
          <label className="check">
            <input type="checkbox" checked={config.network.auto_recovery}
              onChange={(e) => updateNetwork("auto_recovery", e.target.checked)} />
            Auto recovery
          </label>
          <label>Auto recovery timeout (µs)
            <input type="number" value={config.network.auto_recovery_timeout_us}
              onChange={(e) => updateNetwork("auto_recovery_timeout_us", Number(e.target.value))} />
          </label>
        </div>
      </section>

      <div className="body">
        <aside className="slaves card">
          <div className="slaves-head">
            <h2>Slaves</h2>
            <span className="muted">bus order</span>
          </div>
          <ul className="slave-list">
            {config.slaves.map((s, i) => (
              <li
                key={i}
                className={i === selected ? "active" : ""}
                onClick={() => setSelected(i)}
              >
                <span className="pos">{i + 1}</span>
                <span className="sname">{s.name || "slave"}</span>
                <span className="tag">{modeLabel(meta.modes, s.mode_of_operation).split(" ")[0]}</span>
              </li>
            ))}
          </ul>
          <div className="slave-actions">
            <button onClick={addSlave}>+ Add</button>
            <button onClick={duplicateSlave} disabled={!slave}>Duplicate</button>
            <button className="danger" onClick={removeSlave} disabled={!slave}>Remove</button>
          </div>
        </aside>

        <main className="detail">
          {slave ? (
            <>
              <div className="slave-head card">
                <label>Name
                  <input value={slave.name}
                    onChange={(e) => updateSlave({ name: e.target.value })} />
                </label>
                <label>Mode of operation
                  <select value={slave.mode_of_operation}
                    onChange={(e) => updateSlave({ mode_of_operation: Number(e.target.value) })}>
                    {meta.modes.map((m) => (
                      <option key={m.value} value={m.value}>{m.label}</option>
                    ))}
                  </select>
                </label>
                <button className="primary" onClick={loadTemplate}>Load mode template</button>
                <label title="Optional. Verified against the drive's SII at start-up; blank = don't check.">Expected Vendor ID
                  <input value={slave.expected_vendor_id || ""} placeholder="e.g. 0x0000009A"
                    onChange={(e) => updateSlave({ expected_vendor_id: e.target.value })} />
                </label>
                <label title="Optional. Verified against the drive's SII at start-up; blank = don't check.">Expected Product code
                  <input value={slave.expected_product_code || ""} placeholder="e.g. 0x00030924"
                    onChange={(e) => updateSlave({ expected_product_code: e.target.value })} />
                </label>
                <label title="Optional. Verified against the drive's SII at start-up; blank = don't check.">Expected Revision
                  <input value={slave.expected_revision || ""} placeholder="e.g. 0x00010420"
                    onChange={(e) => updateSlave({ expected_revision: e.target.value })} />
                </label>
              </div>

              <div className="maps">
                <PdoTable
                  title="RxPDO"
                  subtitle="master → slave (command)"
                  entries={slave.rxpdo}
                  presets={meta.object_dictionary.rx}
                  onChange={(rxpdo) => updateSlave({ rxpdo })}
                />
                <PdoTable
                  title="TxPDO"
                  subtitle="slave → master (feedback)"
                  entries={slave.txpdo}
                  presets={meta.object_dictionary.tx}
                  onChange={(txpdo) => updateSlave({ txpdo })}
                />
              </div>
            </>
          ) : (
            <div className="empty">No slaves. Click “+ Add”.</div>
          )}
        </main>
      </div>

      {preview && (
        <div className="modal-backdrop" onClick={() => setPreview(false)}>
          <div className="modal" onClick={(e) => e.stopPropagation()}>
            <div className="modal-head">
              <h3>Configuration preview</h3>
              <button className="icon" onClick={() => setPreview(false)}>✕</button>
            </div>
            <pre className="json">{previewJson}</pre>
          </div>
        </div>
      )}

      {toast && <div className={"toast " + toast.kind}>{toast.msg}</div>}
    </div>
  );
}
