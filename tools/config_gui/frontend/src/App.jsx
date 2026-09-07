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

  const profiles = (meta && meta.profiles) || [];
  const mapDefaults = (meta && meta.map_defaults) || {
    rxpdo_map_base: "0x1600", txpdo_map_base: "0x1A00",
    sm2_assign: "0x1C12", sm3_assign: "0x1C13", map_entries_per_obj: 8,
  };
  const profileById = (id) => profiles.find((p) => p.id === id);
  const activeProfile = slave ? profileById(slave.profile) : null;
  const activeOD =
    (activeProfile && activeProfile.object_dictionary) ||
    (meta && meta.object_dictionary);
  const activeTemplates =
    (activeProfile && activeProfile.mode_templates) ||
    (meta && meta.mode_templates);

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

  function changeProfile(id) {
    const p = profileById(id);
    const patch = { profile: id };
    // Fill in the vendor id from the profile only when the user hasn't set one.
    if (p && p.default_vendor_id && !slave.expected_vendor_id) {
      patch.expected_vendor_id = p.default_vendor_id;
    }
    updateSlave(patch);
  }

  function newSlaveDefaults(name) {
    return {
      name: name || "Drive",
      profile: (meta && meta.default_profile) || "elmo_platinum",
      mode_of_operation: 8,
      rxpdo_map_base: mapDefaults.rxpdo_map_base,
      txpdo_map_base: mapDefaults.txpdo_map_base,
      sm2_assign: mapDefaults.sm2_assign,
      sm3_assign: mapDefaults.sm3_assign,
      map_entries_per_obj: mapDefaults.map_entries_per_obj,
      startup_sdo: [],
      rxpdo: clone(meta.mode_templates["8"].rx),
      txpdo: clone(meta.mode_templates["8"].tx),
    };
  }

  function addStartupSdo() {
    const list = (slave.startup_sdo || []).concat({
      index: "0x0000", subindex: 0, size: 4, value: "0x00000000", comment: "",
    });
    updateSlave({ startup_sdo: list });
  }

  function updateStartupSdo(i, patch) {
    const list = (slave.startup_sdo || []).slice();
    list[i] = { ...list[i], ...patch };
    updateSlave({ startup_sdo: list });
  }

  function removeStartupSdo(i) {
    const list = (slave.startup_sdo || []).filter((_, k) => k !== i);
    updateSlave({ startup_sdo: list });
  }

  function addSlave() {
    const slaves = config.slaves.concat(newSlaveDefaults("Drive"));
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
    const tmpl = activeTemplates[String(slave.mode_of_operation)];
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
          <label className="check">
            <input type="checkbox" checked={config.network.verify_identity !== false}
              onChange={(e) => updateNetwork("verify_identity", e.target.checked)} />
            Verify slave identity at start-up
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
                {profiles.length > 0 && (
                  <label title="Drive family. Selects the object picklist and mode templates. The drive's own SII still drives per-slave SM sizing at run-time.">Drive profile
                    <select value={slave.profile || (meta.default_profile || "")}
                      onChange={(e) => changeProfile(e.target.value)}>
                      {profiles.map((p) => (
                        <option key={p.id} value={p.id}>{p.label}</option>
                      ))}
                    </select>
                  </label>
                )}
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

              <details className="card advanced">
                <summary>Advanced: PDO-mapping objects &amp; startup SDOs</summary>
                <div className="grid">
                  <label title="First RxPDO mapping object (CiA402 default 0x1600).">RxPDO map base
                    <input value={slave.rxpdo_map_base || mapDefaults.rxpdo_map_base}
                      onChange={(e) => updateSlave({ rxpdo_map_base: e.target.value })} />
                  </label>
                  <label title="First TxPDO mapping object (CiA402 default 0x1A00).">TxPDO map base
                    <input value={slave.txpdo_map_base || mapDefaults.txpdo_map_base}
                      onChange={(e) => updateSlave({ txpdo_map_base: e.target.value })} />
                  </label>
                  <label title="SyncManager 2 PDO assignment object (default 0x1C12).">SM2 assign
                    <input value={slave.sm2_assign || mapDefaults.sm2_assign}
                      onChange={(e) => updateSlave({ sm2_assign: e.target.value })} />
                  </label>
                  <label title="SyncManager 3 PDO assignment object (default 0x1C13).">SM3 assign
                    <input value={slave.sm3_assign || mapDefaults.sm3_assign}
                      onChange={(e) => updateSlave({ sm3_assign: e.target.value })} />
                  </label>
                  <label title="Max PDO entries packed into each mapping object before spilling to the next.">Entries / map object
                    <input type="number" min="1" max="64"
                      value={slave.map_entries_per_obj || mapDefaults.map_entries_per_obj}
                      onChange={(e) => updateSlave({ map_entries_per_obj: Number(e.target.value) })} />
                  </label>
                </div>

                <div className="sdo-head">
                  <h4>Startup SDO writes</h4>
                  <span className="muted">applied in order during PreOP→SafeOP, before PDO mapping</span>
                </div>
                <table className="sdo-table">
                  <thead>
                    <tr>
                      <th>Index</th><th>Sub</th><th>Size</th><th>Value</th><th>Comment</th><th></th>
                    </tr>
                  </thead>
                  <tbody>
                    {(slave.startup_sdo || []).map((c, i) => (
                      <tr key={i}>
                        <td><input value={c.index}
                          onChange={(e) => updateStartupSdo(i, { index: e.target.value })} /></td>
                        <td><input type="number" min="0" max="255" value={c.subindex}
                          onChange={(e) => updateStartupSdo(i, { subindex: Number(e.target.value) })} /></td>
                        <td>
                          <select value={c.size}
                            onChange={(e) => updateStartupSdo(i, { size: Number(e.target.value) })}>
                            <option value={1}>1</option>
                            <option value={2}>2</option>
                            <option value={4}>4</option>
                          </select>
                        </td>
                        <td><input value={c.value}
                          onChange={(e) => updateStartupSdo(i, { value: e.target.value })} /></td>
                        <td><input value={c.comment || ""}
                          onChange={(e) => updateStartupSdo(i, { comment: e.target.value })} /></td>
                        <td><button className="danger icon" onClick={() => removeStartupSdo(i)}>✕</button></td>
                      </tr>
                    ))}
                    {(slave.startup_sdo || []).length === 0 && (
                      <tr><td colSpan={6} className="muted">none — the drive uses its stored defaults</td></tr>
                    )}
                  </tbody>
                </table>
                <button onClick={addStartupSdo}>+ Add startup SDO</button>
              </details>

              <div className="maps">
                <PdoTable
                  title="RxPDO"
                  subtitle="master → slave (command)"
                  entries={slave.rxpdo}
                  presets={activeOD.rx}
                  onChange={(rxpdo) => updateSlave({ rxpdo })}
                />
                <PdoTable
                  title="TxPDO"
                  subtitle="slave → master (feedback)"
                  entries={slave.txpdo}
                  presets={activeOD.tx}
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
