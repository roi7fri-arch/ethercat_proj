import React, { useState } from "react";

// Editable, ordered PDO list for one direction. Order matters for the mapping,
// so rows support drag-to-reorder plus keyboard-free up/down controls.
export default function PdoTable({ title, subtitle, entries, presets, onChange }) {
  const [preset, setPreset] = useState("");
  const [manual, setManual] = useState({ index: "0x", subindex: "0", bitlen: "16", name: "" });
  const [dragIndex, setDragIndex] = useState(null);
  const [error, setError] = useState("");

  const totalBits = entries.reduce((sum, e) => sum + Number(e.bitlen || 0), 0);

  function addPreset() {
    if (preset === "") return;
    const p = presets[Number(preset)];
    onChange([...entries, { ...p }]);
    setPreset("");
  }

  function addManual() {
    const value = parseInt(manual.index, 16);
    const sub = parseInt(manual.subindex, 10);
    const bits = parseInt(manual.bitlen, 10);
    if (Number.isNaN(value) || value < 0 || value > 0xffff) {
      setError("Index must be 0x0000-0xFFFF");
      return;
    }
    if (Number.isNaN(sub) || sub < 0 || sub > 255) {
      setError("Sub must be 0-255");
      return;
    }
    if (Number.isNaN(bits) || bits < 1 || bits > 64) {
      setError("Bits must be 1-64");
      return;
    }
    setError("");
    const index = "0x" + value.toString(16).toUpperCase().padStart(4, "0");
    onChange([...entries, { index, subindex: sub, bitlen: bits, name: manual.name.trim() }]);
    setManual({ index: "0x", subindex: "0", bitlen: "16", name: "" });
  }

  function remove(i) {
    onChange(entries.filter((_, idx) => idx !== i));
  }

  function move(i, delta) {
    const j = i + delta;
    if (j < 0 || j >= entries.length) return;
    const next = entries.slice();
    [next[i], next[j]] = [next[j], next[i]];
    onChange(next);
  }

  function onDrop(i) {
    if (dragIndex === null || dragIndex === i) return;
    const next = entries.slice();
    const [moved] = next.splice(dragIndex, 1);
    next.splice(i, 0, moved);
    onChange(next);
    setDragIndex(null);
  }

  return (
    <div className="pdo card">
      <div className="pdo-head">
        <div>
          <h3>{title}</h3>
          <span className="muted">{subtitle}</span>
        </div>
        <span className="badge">{entries.length} entries · {totalBits} bits</span>
      </div>

      <table className="pdo-table">
        <thead>
          <tr>
            <th className="grip"></th>
            <th>Index</th>
            <th>Sub</th>
            <th>Bits</th>
            <th>Name</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          {entries.length === 0 && (
            <tr>
              <td colSpan={6} className="muted center">No objects mapped</td>
            </tr>
          )}
          {entries.map((e, i) => (
            <tr
              key={i}
              draggable
              onDragStart={() => setDragIndex(i)}
              onDragOver={(ev) => ev.preventDefault()}
              onDrop={() => onDrop(i)}
              className={dragIndex === i ? "dragging" : ""}
            >
              <td className="grip" title="Drag to reorder">⋮⋮</td>
              <td className="mono">{e.index}</td>
              <td className="mono center">{e.subindex}</td>
              <td className="mono center">{e.bitlen}</td>
              <td>{e.name}</td>
              <td className="row-actions">
                <button className="icon" title="Up" onClick={() => move(i, -1)}>▲</button>
                <button className="icon" title="Down" onClick={() => move(i, 1)}>▼</button>
                <button className="icon danger" title="Remove" onClick={() => remove(i)}>✕</button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>

      <div className="pdo-add">
        <div className="add-row">
          <select value={preset} onChange={(e) => setPreset(e.target.value)}>
            <option value="">Add preset object…</option>
            {presets.map((p, i) => (
              <option key={i} value={i}>
                {p.index} · {p.name} ({p.bitlen}b, sub{p.subindex})
              </option>
            ))}
          </select>
          <button onClick={addPreset} disabled={preset === ""}>Add</button>
        </div>
        <div className="add-row">
          <input
            className="w-idx"
            value={manual.index}
            onChange={(e) => setManual({ ...manual, index: e.target.value })}
            placeholder="0x6040"
          />
          <input
            className="w-sub"
            value={manual.subindex}
            onChange={(e) => setManual({ ...manual, subindex: e.target.value })}
            placeholder="sub"
          />
          <input
            className="w-bits"
            value={manual.bitlen}
            onChange={(e) => setManual({ ...manual, bitlen: e.target.value })}
            placeholder="bits"
          />
          <input
            value={manual.name}
            onChange={(e) => setManual({ ...manual, name: e.target.value })}
            placeholder="name (optional)"
          />
          <button onClick={addManual}>Add</button>
        </div>
        {error && <div className="error">{error}</div>}
      </div>
    </div>
  );
}
