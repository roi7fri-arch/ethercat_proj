import React, { useMemo, useState } from "react";

/*
 * Parameter set editor.
 *
 * A parameter set is a deliberately separate document from the bus
 * configuration. The config describes the machine and is read by the master at
 * every start-up; a parameter set is commissioning data - gains, limits,
 * scaling, homing - that gets written into the drives' own memory once and
 * then lives there.
 *
 * The editor groups rows the way an engineer thinks about them rather than by
 * object index, and offers a catalogue picklist so nobody has to remember that
 * "max torque" is 0x6072 in per-mille.
 */

const clone = (o) => JSON.parse(JSON.stringify(o));

export default function ParamEditor({
  pmeta, params, onChange, slaves, warnings, onValidate,
}) {
  const [picker, setPicker] = useState("");

  const catalogue = (pmeta && pmeta.catalogue) || [];
  const types = (pmeta && pmeta.types) || [];

  // Group for display, but keep the real array index on every row: the order
  // in the file is the order the drives are written in, and for 0x1010 "store"
  // that ordering is the difference between tuning that survives a power cycle
  // and tuning that does not.
  const grouped = useMemo(() => {
    const out = [];
    (params.parameters || []).forEach((p, i) => {
      const g = p.group || "General";
      let bucket = out.find((b) => b.name === g);
      if (!bucket) { bucket = { name: g, rows: [] }; out.push(bucket); }
      bucket.rows.push({ p, i });
    });
    return out;
  }, [params]);

  function patch(next) {
    onChange(next);
  }

  function updateEntry(i, delta) {
    const list = (params.parameters || []).slice();
    list[i] = { ...list[i], ...delta };
    patch({ ...params, parameters: list });
  }

  function removeEntry(i) {
    patch({
      ...params,
      parameters: (params.parameters || []).filter((_, k) => k !== i),
    });
  }

  function moveEntry(i, dir) {
    const list = (params.parameters || []).slice();
    const j = i + dir;
    if (j < 0 || j >= list.length) return;
    const tmp = list[i];
    list[i] = list[j];
    list[j] = tmp;
    patch({ ...params, parameters: list });
  }

  function addFromCatalogue(key) {
    if (!key) return;
    const src = catalogue.find((c) => c.index + ":" + c.subindex === key);
    if (!src) return;
    const entry = {
      slave: 1,
      index: src.index,
      subindex: src.subindex,
      type: src.type,
      value: src.value,
      verify: true,
      name: src.name,
      group: src.group,
      unit: src.unit || "",
    };
    patch({ ...params, parameters: (params.parameters || []).concat(entry) });
    setPicker("");
  }

  function addBlank() {
    const entry = (pmeta && clone(pmeta.default_entry)) || {
      slave: 1, index: "0x0000", subindex: 0, type: "u32",
      value: "0", verify: true, name: "", group: "General", unit: "",
    };
    entry.index = "0x0000";
    entry.name = "";
    entry.group = "General";
    entry.unit = "";
    entry.value = "0";
    patch({ ...params, parameters: (params.parameters || []).concat(entry) });
  }

  function addFile() {
    const f = (pmeta && clone(pmeta.default_file)) || {
      slave: 1, path: "", remote_name: "",
      password: "0x00000000", use_boot_state: true,
    };
    patch({ ...params, files: (params.files || []).concat(f) });
  }

  function updateFile(i, delta) {
    const list = (params.files || []).slice();
    list[i] = { ...list[i], ...delta };
    patch({ ...params, files: list });
  }

  function removeFile(i) {
    patch({ ...params, files: (params.files || []).filter((_, k) => k !== i) });
  }

  const slaveOptions = [{ value: 0, label: "all slaves" }].concat(
    (slaves || []).map((s, i) => ({
      value: i + 1,
      label: (i + 1) + " — " + (s.name || "slave"),
    }))
  );

  return (
    <div className="params">
      <section className="card">
        <div className="sdo-head">
          <h2>Parameter set</h2>
          <span className="muted">
            written into the drives once, not read at every start-up
          </span>
        </div>
        <div className="grid">
          <label>Set name
            <input value={params.name || ""}
              onChange={(e) => patch({ ...params, name: e.target.value })} />
          </label>
          <label className="wide">Description
            <input value={params.description || ""}
              placeholder="e.g. Axis 1+2 tuning after gearbox change"
              onChange={(e) => patch({ ...params, description: e.target.value })} />
          </label>
        </div>
      </section>

      {warnings && warnings.length > 0 && (
        <section className="card warnings">
          <h3>Before you download</h3>
          <ul>
            {warnings.map((w, i) => <li key={i}>{w}</li>)}
          </ul>
        </section>
      )}

      <section className="card">
        <div className="sdo-head">
          <h3>Objects</h3>
          <span className="muted">
            written in this order — “Store parameters” must come last
          </span>
        </div>

        <div className="param-add">
          <select value={picker} onChange={(e) => addFromCatalogue(e.target.value)}>
            <option value="">+ Add from catalogue…</option>
            {(pmeta ? pmeta.groups : []).map((g) => (
              <optgroup key={g} label={g}>
                {catalogue
                  .filter((c) => c.group === g)
                  .map((c) => (
                    <option key={c.index + ":" + c.subindex}
                            value={c.index + ":" + c.subindex}>
                      {c.name} ({c.index}:{String(c.subindex).padStart(2, "0")})
                    </option>
                  ))}
              </optgroup>
            ))}
          </select>
          <button onClick={addBlank}>+ Add blank row</button>
          {onValidate && <button onClick={onValidate}>Check set</button>}
        </div>

        {(params.parameters || []).length === 0 ? (
          <div className="empty">
            No parameters yet. Pick one from the catalogue, or add a blank row
            and type the object index from the drive manual.
          </div>
        ) : (
          grouped.map((bucket) => (
            <div className="param-group" key={bucket.name}>
              <h4>{bucket.name}</h4>
              <table className="param-table">
                <thead>
                  <tr>
                    <th>Slave</th>
                    <th>Index</th>
                    <th>Sub</th>
                    <th>Type</th>
                    <th>Value</th>
                    <th>Unit</th>
                    <th>Name</th>
                    <th title="Read the object back after writing and compare. A drive that clamps the value to its own legal range is only visible this way.">
                      Verify
                    </th>
                    <th></th>
                  </tr>
                </thead>
                <tbody>
                  {bucket.rows.map(({ p, i }) => (
                    <tr key={i}>
                      <td>
                        <select className="slave" value={p.slave}
                          onChange={(e) => updateEntry(i, { slave: Number(e.target.value) })}>
                          {slaveOptions.map((o) => (
                            <option key={o.value} value={o.value}>{o.label}</option>
                          ))}
                        </select>
                      </td>
                      <td><input className="idx" value={p.index}
                        onChange={(e) => updateEntry(i, { index: e.target.value })} /></td>
                      <td><input className="num" type="number" min="0" max="255"
                        value={p.subindex}
                        onChange={(e) => updateEntry(i, { subindex: Number(e.target.value) })} /></td>
                      <td>
                        <select className="ptype" value={p.type}
                          onChange={(e) => updateEntry(i, { type: e.target.value })}>
                          {types.map((t) => (
                            <option key={t.id} value={t.id}>{t.id}</option>
                          ))}
                        </select>
                      </td>
                      <td><input className="val" value={p.value}
                        title="Decimal, negative or 0x-hex. Floats accept 1.25 or a raw hex bit pattern."
                        onChange={(e) => updateEntry(i, { value: e.target.value })} /></td>
                      <td><input className="unit" value={p.unit || ""}
                        onChange={(e) => updateEntry(i, { unit: e.target.value })} /></td>
                      <td className="grow"><input value={p.name || ""}
                        onChange={(e) => updateEntry(i, { name: e.target.value })} /></td>
                      <td className="mid">
                        <input type="checkbox" checked={p.verify !== false}
                          onChange={(e) => updateEntry(i, { verify: e.target.checked })} />
                      </td>
                      <td className="rowbtns">
                        <button className="icon" title="move up"
                          onClick={() => moveEntry(i, -1)}>↑</button>
                        <button className="icon" title="move down"
                          onClick={() => moveEntry(i, 1)}>↓</button>
                        <button className="danger icon" title="remove"
                          onClick={() => removeEntry(i)}>✕</button>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          ))
        )}
      </section>

      <section className="card">
        <div className="sdo-head">
          <h3>Files (FoE)</h3>
          <span className="muted">
            firmware or data files pushed to a drive over the cable
          </span>
        </div>
        <table className="param-table">
          <thead>
            <tr>
              <th>Slave</th>
              <th>Local file</th>
              <th title="The name used inside the FoE transaction. Most drives expect the plain file name.">
                Name on drive
              </th>
              <th>Password</th>
              <th title="Firmware must be written in BOOT state. The drive stops controlling the motor for the duration.">
                Via BOOT
              </th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            {(params.files || []).map((f, i) => (
              <tr key={i}>
                <td><input className="num" type="number" min="1" value={f.slave}
                  onChange={(e) => updateFile(i, { slave: Number(e.target.value) })} /></td>
                <td><input value={f.path}
                  placeholder="/path/to/firmware.bin"
                  onChange={(e) => updateFile(i, { path: e.target.value })} /></td>
                <td><input value={f.remote_name || ""}
                  placeholder="(file name)"
                  onChange={(e) => updateFile(i, { remote_name: e.target.value })} /></td>
                <td><input className="idx" value={f.password}
                  onChange={(e) => updateFile(i, { password: e.target.value })} /></td>
                <td className="mid">
                  <input type="checkbox" checked={f.use_boot_state !== false}
                    onChange={(e) => updateFile(i, { use_boot_state: e.target.checked })} />
                </td>
                <td className="rowbtns">
                  <button className="danger icon" onClick={() => removeFile(i)}>✕</button>
                </td>
              </tr>
            ))}
            {(params.files || []).length === 0 && (
              <tr><td colSpan={6} className="muted">
                none — most parameter sets do not need a file transfer
              </td></tr>
            )}
          </tbody>
        </table>
        <button onClick={addFile}>+ Add file</button>
      </section>
    </div>
  );
}
