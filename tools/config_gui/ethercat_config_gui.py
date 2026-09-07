#!/usr/bin/env python3
"""EtherCAT application configuration builder.

Generates a JSON configuration file that the EtherCAT master application reads
on init to learn the network settings, each slave's mode of operation, and the
RxPDO / TxPDO object maps it must build in PRE-OP.

Pure standard-library Tkinter GUI: run with `python3 ethercat_config_gui.py`.
No third-party packages required.
"""

import json
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

CONFIG_VERSION = 1

# CiA402 modes of operation (value written to object 0x6060).
MODES = [
    (1, "PP  - Profile Position"),
    (3, "PV  - Profile Velocity"),
    (4, "TQ  - Profile Torque"),
    (6, "HM  - Homing"),
    (8, "CSP - Cyclic Sync Position"),
    (9, "CSV - Cyclic Sync Velocity"),
    (10, "CST - Cyclic Sync Torque"),
]

# Known CoE objects offered as picklist presets. Fields: index, sub, bits, name.
# Directions are only a hint for grouping; any object can be added to either map.
OBJECT_DICTIONARY = {
    "rx": [
        ("0x6040", 0, 16, "Controlword"),
        ("0x6060", 0, 8, "Modes of operation"),
        ("0x607A", 0, 32, "Target position"),
        ("0x60FF", 0, 32, "Target velocity"),
        ("0x6071", 0, 16, "Target torque"),
        ("0x6072", 0, 16, "Max torque"),
        ("0x60B0", 0, 32, "Position offset"),
        ("0x60B1", 0, 32, "Velocity offset"),
        ("0x60B2", 0, 16, "Torque offset"),
    ],
    "tx": [
        ("0x6041", 0, 16, "Statusword"),
        ("0x6061", 0, 8, "Modes of operation display"),
        ("0x603F", 0, 16, "Error code"),
        ("0x6062", 0, 32, "Position demand value"),
        ("0x6064", 0, 32, "Position actual value"),
        ("0x606B", 0, 32, "Velocity demand value"),
        ("0x606C", 0, 32, "Velocity actual value"),
        ("0x6074", 0, 16, "Torque demand"),
        ("0x6077", 0, 16, "Torque actual value"),
        ("0x6078", 0, 16, "Current actual value"),
        ("0x60F4", 0, 32, "Following error actual value"),
        ("0x3610", 0, 32, "Elmo: manufacturer object"),
        ("0x3640", 0, 32, "Elmo: manufacturer object"),
        ("0x2FE4", 1, 64, "Elmo: manufacturer object"),
        ("0x2FE8", 1, 32, "Elmo: manufacturer object"),
        ("0x2FEC", 1, 32, "Elmo: manufacturer object"),
        ("0xF6F0", 1, 32, "Elmo: manufacturer object"),
    ],
}

# Ready-made PDO maps per mode, matching the existing cyclic_sync_*_mode.dat files.
MODE_TEMPLATES = {
    8: {
        "rx": [
            ("0x6040", 0, 16, "Controlword"),
            ("0x60B0", 0, 32, "Position offset"),
            ("0x60B1", 0, 32, "Velocity offset"),
            ("0x60B2", 0, 16, "Torque offset"),
        ],
        "tx": [
            ("0x6041", 0, 16, "Statusword"),
            ("0x6061", 0, 8, "Modes of operation display"),
            ("0x603F", 0, 16, "Error code"),
            ("0x6062", 0, 32, "Position demand value"),
            ("0x606B", 0, 32, "Velocity demand value"),
            ("0x6074", 0, 16, "Torque demand"),
            ("0x6064", 0, 32, "Position actual value"),
            ("0x606C", 0, 32, "Velocity actual value"),
            ("0x6077", 0, 16, "Torque actual value"),
            ("0x6078", 0, 16, "Current actual value"),
            ("0x60F4", 0, 32, "Following error actual value"),
            ("0x3610", 0, 32, "Elmo: manufacturer object"),
            ("0x3640", 0, 32, "Elmo: manufacturer object"),
        ],
    },
    9: {
        "rx": [
            ("0x6040", 0, 16, "Controlword"),
            ("0x60B1", 0, 32, "Velocity offset"),
            ("0x60B2", 0, 16, "Torque offset"),
            ("0x60FF", 0, 32, "Target velocity"),
        ],
        "tx": [
            ("0x6041", 0, 16, "Statusword"),
            ("0x6061", 0, 8, "Modes of operation display"),
            ("0x606B", 0, 32, "Velocity demand value"),
            ("0x6064", 0, 32, "Position actual value"),
            ("0x606C", 0, 32, "Velocity actual value"),
            ("0x6077", 0, 16, "Torque actual value"),
            ("0x6078", 0, 16, "Current actual value"),
            ("0x2FE4", 1, 64, "Elmo: manufacturer object"),
            ("0x2FE8", 1, 32, "Elmo: manufacturer object"),
            ("0x2FE4", 2, 64, "Elmo: manufacturer object"),
            ("0x2FE8", 2, 32, "Elmo: manufacturer object"),
            ("0x2FE4", 3, 64, "Elmo: manufacturer object"),
            ("0x2FE8", 3, 32, "Elmo: manufacturer object"),
            ("0x2FEC", 1, 32, "Elmo: manufacturer object"),
            ("0x3640", 0, 32, "Elmo: manufacturer object"),
            ("0x603F", 0, 16, "Error code"),
            ("0xF6F0", 1, 32, "Elmo: manufacturer object"),
        ],
    },
    10: {
        "rx": [
            ("0x6040", 0, 16, "Controlword"),
            ("0x60B2", 0, 16, "Torque offset"),
            ("0x6071", 0, 16, "Target torque"),
        ],
        "tx": [
            ("0x6041", 0, 16, "Statusword"),
            ("0x6061", 0, 8, "Modes of operation display"),
            ("0x6074", 0, 16, "Torque demand"),
            ("0x6064", 0, 32, "Position actual value"),
            ("0x606C", 0, 32, "Velocity actual value"),
            ("0x6077", 0, 16, "Torque actual value"),
            ("0x6078", 0, 16, "Current actual value"),
            ("0x2FE4", 1, 64, "Elmo: manufacturer object"),
            ("0x2FE8", 1, 32, "Elmo: manufacturer object"),
            ("0x2FE4", 2, 64, "Elmo: manufacturer object"),
            ("0x2FE8", 2, 32, "Elmo: manufacturer object"),
            ("0x2FE4", 3, 64, "Elmo: manufacturer object"),
            ("0x2FE8", 3, 32, "Elmo: manufacturer object"),
            ("0x2FEC", 1, 32, "Elmo: manufacturer object"),
            ("0x3640", 0, 32, "Elmo: manufacturer object"),
            ("0x603F", 0, 16, "Error code"),
            ("0xF6F0", 1, 32, "Elmo: manufacturer object"),
        ],
    },
}


def mode_label(value):
    for v, label in MODES:
        if v == value:
            return label
    return str(value)


def mode_value(label):
    for v, lbl in MODES:
        if lbl == label:
            return v
    return int(label)


def normalize_index(text):
    """Return a canonical 0xNNNN index string, or raise ValueError."""
    text = text.strip()
    if not text:
        raise ValueError("index is empty")
    value = int(text, 16) if text.lower().startswith("0x") else int(text, 16)
    if not 0 <= value <= 0xFFFF:
        raise ValueError("index out of range")
    return "0x%04X" % value


class PdoEditor(ttk.LabelFrame):
    """Editable, ordered list of PDO entries for one direction (rx or tx)."""

    def __init__(self, master, title, direction):
        super().__init__(master, text=title, padding=6)
        self.direction = direction

        columns = ("index", "sub", "bits", "name")
        self.tree = ttk.Treeview(self, columns=columns, show="headings", height=10)
        self.tree.heading("index", text="Index")
        self.tree.heading("sub", text="Sub")
        self.tree.heading("bits", text="Bits")
        self.tree.heading("name", text="Name")
        self.tree.column("index", width=70, anchor="center")
        self.tree.column("sub", width=40, anchor="center")
        self.tree.column("bits", width=45, anchor="center")
        self.tree.column("name", width=190, anchor="w")
        self.tree.grid(row=0, column=0, columnspan=6, sticky="nsew")

        scroll = ttk.Scrollbar(self, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        scroll.grid(row=0, column=6, sticky="ns")

        # Preset picker.
        preset_values = [
            "%s  %s (%db, sub%d)" % (idx, name, bits, sub)
            for idx, sub, bits, name in OBJECT_DICTIONARY[direction]
        ]
        self.preset_map = {
            preset_values[i]: OBJECT_DICTIONARY[direction][i]
            for i in range(len(preset_values))
        }
        ttk.Label(self, text="Preset:").grid(row=1, column=0, sticky="e", pady=(6, 0))
        self.preset_cb = ttk.Combobox(self, values=preset_values, state="readonly", width=34)
        self.preset_cb.grid(row=1, column=1, columnspan=3, sticky="we", pady=(6, 0))
        ttk.Button(self, text="Add preset", command=self._add_preset).grid(
            row=1, column=4, columnspan=2, sticky="we", pady=(6, 0))

        # Manual entry.
        ttk.Label(self, text="Index").grid(row=2, column=0, sticky="e")
        self.idx_var = tk.StringVar(value="0x")
        ttk.Entry(self, textvariable=self.idx_var, width=8).grid(row=2, column=1, sticky="w")
        ttk.Label(self, text="Sub").grid(row=2, column=2, sticky="e")
        self.sub_var = tk.StringVar(value="0")
        ttk.Entry(self, textvariable=self.sub_var, width=5).grid(row=2, column=3, sticky="w")
        ttk.Label(self, text="Bits").grid(row=2, column=4, sticky="e")
        self.bits_var = tk.StringVar(value="16")
        ttk.Entry(self, textvariable=self.bits_var, width=5).grid(row=2, column=5, sticky="w")
        ttk.Label(self, text="Name").grid(row=3, column=0, sticky="e")
        self.name_var = tk.StringVar()
        ttk.Entry(self, textvariable=self.name_var, width=30).grid(
            row=3, column=1, columnspan=3, sticky="we")
        ttk.Button(self, text="Add", command=self._add_manual).grid(
            row=3, column=4, columnspan=2, sticky="we")

        # Row controls.
        controls = ttk.Frame(self)
        controls.grid(row=4, column=0, columnspan=7, sticky="we", pady=(6, 0))
        ttk.Button(controls, text="Remove", command=self._remove).pack(side="left")
        ttk.Button(controls, text="Move up", command=lambda: self._move(-1)).pack(side="left", padx=4)
        ttk.Button(controls, text="Move down", command=lambda: self._move(1)).pack(side="left")
        self.count_var = tk.StringVar(value="0 entries")
        ttk.Label(controls, textvariable=self.count_var).pack(side="right")

        self.rowconfigure(0, weight=1)
        self.columnconfigure(1, weight=1)

    def _append(self, index, sub, bits, name):
        self.tree.insert("", "end", values=(index, sub, bits, name))
        self._update_count()

    def _add_preset(self):
        sel = self.preset_cb.get()
        if sel in self.preset_map:
            idx, sub, bits, name = self.preset_map[sel]
            self._append(idx, sub, bits, name)

    def _add_manual(self):
        try:
            index = normalize_index(self.idx_var.get())
            sub = int(self.sub_var.get())
            bits = int(self.bits_var.get())
            if not 0 <= sub <= 255:
                raise ValueError("subindex out of range (0-255)")
            if bits <= 0 or bits > 64:
                raise ValueError("bit length out of range (1-64)")
        except ValueError as exc:
            messagebox.showerror("Invalid PDO entry", str(exc))
            return
        self._append(index, sub, bits, self.name_var.get().strip())
        self.idx_var.set("0x")
        self.name_var.set("")

    def _remove(self):
        for item in self.tree.selection():
            self.tree.delete(item)
        self._update_count()

    def _move(self, delta):
        sel = self.tree.selection()
        if not sel:
            return
        item = sel[0]
        index = self.tree.index(item)
        new_index = index + delta
        if 0 <= new_index < len(self.tree.get_children()):
            self.tree.move(item, "", new_index)

    def _update_count(self):
        self.count_var.set("%d entries" % len(self.tree.get_children()))

    def set_entries(self, entries):
        self.tree.delete(*self.tree.get_children())
        for e in entries:
            self.tree.insert("", "end", values=(e["index"], e["subindex"], e["bitlen"], e.get("name", "")))
        self._update_count()

    def get_entries(self):
        result = []
        for item in self.tree.get_children():
            index, sub, bits, name = self.tree.item(item, "values")
            result.append({
                "index": index,
                "subindex": int(sub),
                "bitlen": int(bits),
                "name": name,
            })
        return result


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("EtherCAT Application Config Builder")
        self.geometry("980x680")

        self.slaves = []          # list of slave dicts
        self.current = None       # index of slave being edited
        self.path = None          # last saved path

        self._build_menu()
        self._build_network_frame()
        self._build_body()
        self._new_config(confirm=False)

    # ---- layout ---------------------------------------------------------
    def _build_menu(self):
        menubar = tk.Menu(self)
        filemenu = tk.Menu(menubar, tearoff=0)
        filemenu.add_command(label="New", command=self._new_config)
        filemenu.add_command(label="Open...", command=self._open)
        filemenu.add_command(label="Save", command=self._save)
        filemenu.add_command(label="Save As...", command=self._save_as)
        filemenu.add_separator()
        filemenu.add_command(label="Preview JSON", command=self._preview)
        filemenu.add_separator()
        filemenu.add_command(label="Quit", command=self.destroy)
        menubar.add_cascade(label="File", menu=filemenu)
        self.config(menu=menubar)

    def _build_network_frame(self):
        frame = ttk.LabelFrame(self, text="Network", padding=8)
        frame.pack(fill="x", padx=8, pady=(8, 4))

        self.iface_var = tk.StringVar(value="eth0")
        self.redundant_iface_var = tk.StringVar(value="")
        self.cycle_var = tk.StringVar(value="250")
        self.cycles_var = tk.StringVar(value="12000")
        self.dc_var = tk.BooleanVar(value=True)
        self.shift_var = tk.StringVar(value="0")
        self.kp_var = tk.StringVar(value="100")
        self.ki_var = tk.StringVar(value="20")

        ttk.Label(frame, text="Interface").grid(row=0, column=0, sticky="e")
        ttk.Entry(frame, textvariable=self.iface_var, width=10).grid(row=0, column=1, padx=(2, 12))
        ttk.Label(frame, text="Cycle time (us)").grid(row=0, column=2, sticky="e")
        ttk.Entry(frame, textvariable=self.cycle_var, width=8).grid(row=0, column=3, padx=(2, 12))
        ttk.Label(frame, text="Cycles (0=inf)").grid(row=0, column=4, sticky="e")
        ttk.Entry(frame, textvariable=self.cycles_var, width=8).grid(row=0, column=5, padx=(2, 12))
        ttk.Checkbutton(frame, text="Distributed Clock (SYNC0)", variable=self.dc_var).grid(
            row=0, column=6, padx=(2, 12))
        ttk.Label(frame, text="SYNC0 shift (us)").grid(row=0, column=7, sticky="e")
        ttk.Entry(frame, textvariable=self.shift_var, width=8).grid(row=0, column=8, padx=2)
        ttk.Label(frame, text="Sync Kp div").grid(row=1, column=0, sticky="e", pady=(4, 0))
        ttk.Entry(frame, textvariable=self.kp_var, width=8).grid(row=1, column=1, padx=2, pady=(4, 0))
        ttk.Label(frame, text="Sync Ki div").grid(row=1, column=2, sticky="e", pady=(4, 0))
        ttk.Entry(frame, textvariable=self.ki_var, width=8).grid(row=1, column=3, padx=2, pady=(4, 0))
        ttk.Label(frame, text="Redundant iface").grid(row=1, column=4, sticky="e", pady=(4, 0))
        ttk.Entry(frame, textvariable=self.redundant_iface_var, width=10).grid(row=1, column=5, padx=2, pady=(4, 0))

    def _build_body(self):
        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, padx=8, pady=4)

        # Left: slave list.
        left = ttk.LabelFrame(body, text="Slaves (bus order)", padding=6)
        left.pack(side="left", fill="y")
        self.slave_list = tk.Listbox(left, width=26, height=20, exportselection=False)
        self.slave_list.pack(fill="y", expand=True)
        self.slave_list.bind("<<ListboxSelect>>", self._on_select_slave)
        btns = ttk.Frame(left)
        btns.pack(fill="x", pady=(6, 0))
        ttk.Button(btns, text="Add", command=self._add_slave).pack(side="left")
        ttk.Button(btns, text="Duplicate", command=self._dup_slave).pack(side="left", padx=4)
        ttk.Button(btns, text="Remove", command=self._remove_slave).pack(side="left")

        # Right: slave detail.
        right = ttk.Frame(body)
        right.pack(side="left", fill="both", expand=True, padx=(8, 0))

        head = ttk.Frame(right)
        head.pack(fill="x")
        ttk.Label(head, text="Name").grid(row=0, column=0, sticky="e")
        self.sname_var = tk.StringVar()
        ttk.Entry(head, textvariable=self.sname_var, width=28).grid(row=0, column=1, padx=(2, 12))
        self.sname_var.trace_add("write", lambda *a: self._on_name_change())
        ttk.Label(head, text="Mode of operation").grid(row=0, column=2, sticky="e")
        self.mode_cb = ttk.Combobox(head, values=[m[1] for m in MODES], state="readonly", width=26)
        self.mode_cb.grid(row=0, column=3, padx=2)
        ttk.Button(head, text="Load mode template", command=self._load_template).grid(
            row=0, column=4, padx=8)

        maps = ttk.Frame(right)
        maps.pack(fill="both", expand=True, pady=(8, 0))
        self.rx_editor = PdoEditor(maps, "RxPDO  (master -> slave, command)", "rx")
        self.rx_editor.pack(side="left", fill="both", expand=True)
        self.tx_editor = PdoEditor(maps, "TxPDO  (slave -> master, feedback)", "tx")
        self.tx_editor.pack(side="left", fill="both", expand=True, padx=(8, 0))

    # ---- slave model ----------------------------------------------------
    def _commit_current(self):
        if self.current is None:
            return
        s = self.slaves[self.current]
        s["name"] = self.sname_var.get().strip()
        s["mode_of_operation"] = mode_value(self.mode_cb.get()) if self.mode_cb.get() else 8
        s["rxpdo"] = self.rx_editor.get_entries()
        s["txpdo"] = self.tx_editor.get_entries()

    def _load_slave(self, i):
        self.current = i
        s = self.slaves[i]
        self.sname_var.set(s.get("name", ""))
        self.mode_cb.set(mode_label(s.get("mode_of_operation", 8)))
        self.rx_editor.set_entries(s.get("rxpdo", []))
        self.tx_editor.set_entries(s.get("txpdo", []))

    def _refresh_slave_list(self):
        self.slave_list.delete(0, "end")
        for i, s in enumerate(self.slaves):
            label = "%d: %s [%s]" % (i + 1, s.get("name", "slave"),
                                     mode_label(s.get("mode_of_operation", 8)).split()[0])
            self.slave_list.insert("end", label)
        if self.current is not None and 0 <= self.current < len(self.slaves):
            self.slave_list.selection_clear(0, "end")
            self.slave_list.selection_set(self.current)

    def _on_select_slave(self, _event):
        sel = self.slave_list.curselection()
        if not sel:
            return
        i = sel[0]
        if i == self.current:
            return
        self._commit_current()
        self._load_slave(i)

    def _on_name_change(self):
        if self.current is None:
            return
        self.slaves[self.current]["name"] = self.sname_var.get().strip()
        # Keep the list label in sync without losing selection.
        i = self.current
        label = "%d: %s [%s]" % (i + 1, self.sname_var.get().strip() or "slave",
                                 mode_label(self.slaves[i].get("mode_of_operation", 8)).split()[0])
        self.slave_list.delete(i)
        self.slave_list.insert(i, label)
        self.slave_list.selection_set(i)

    def _add_slave(self):
        self._commit_current()
        self.slaves.append({
            "name": "Elmo Platinum",
            "mode_of_operation": 8,
            "rxpdo": [],
            "txpdo": [],
        })
        self._refresh_slave_list()
        self.slave_list.selection_clear(0, "end")
        self.slave_list.selection_set(len(self.slaves) - 1)
        self._load_slave(len(self.slaves) - 1)

    def _dup_slave(self):
        if self.current is None:
            return
        self._commit_current()
        clone = json.loads(json.dumps(self.slaves[self.current]))
        self.slaves.insert(self.current + 1, clone)
        self._refresh_slave_list()
        self._load_slave(self.current + 1)
        self._refresh_slave_list()

    def _remove_slave(self):
        if self.current is None:
            return
        del self.slaves[self.current]
        if not self.slaves:
            self.current = None
            self.sname_var.set("")
            self.mode_cb.set("")
            self.rx_editor.set_entries([])
            self.tx_editor.set_entries([])
        else:
            self.current = max(0, self.current - 1)
            self._load_slave(self.current)
        self._refresh_slave_list()

    def _load_template(self):
        if self.current is None:
            return
        mode = mode_value(self.mode_cb.get()) if self.mode_cb.get() else 8
        template = MODE_TEMPLATES.get(mode)
        if not template:
            messagebox.showinfo("No template",
                                "No ready-made PDO template for %s." % mode_label(mode))
            return
        if not messagebox.askyesno("Load template",
                                   "Replace this slave's RxPDO/TxPDO with the %s template?"
                                   % mode_label(mode).split()[0]):
            return
        self.rx_editor.set_entries(
            [{"index": i, "subindex": s, "bitlen": b, "name": n} for i, s, b, n in template["rx"]])
        self.tx_editor.set_entries(
            [{"index": i, "subindex": s, "bitlen": b, "name": n} for i, s, b, n in template["tx"]])

    # ---- file operations ------------------------------------------------
    def _gather_config(self):
        self._commit_current()
        try:
            cycle_us = int(self.cycle_var.get())
            cycles = int(self.cycles_var.get())
            shift = int(self.shift_var.get())
            kp_div = int(self.kp_var.get())
            ki_div = int(self.ki_var.get())
        except ValueError:
            raise ValueError("Cycle time, cycles, SYNC0 shift and sync divisors must be integers.")
        if kp_div <= 0 or ki_div <= 0:
            raise ValueError("Sync Kp/Ki divisors must be > 0.")
        return {
            "version": CONFIG_VERSION,
            "network": {
                "interface": self.iface_var.get().strip(),
                "redundant_interface": self.redundant_iface_var.get().strip(),
                "cycle_time_us": cycle_us,
                "number_of_cycles": cycles,
                "distributed_clock": bool(self.dc_var.get()),
                "sync0_shift_us": shift,
                "sync_kp_div": kp_div,
                "sync_ki_div": ki_div,
            },
            "slaves": [
                {
                    "position": i + 1,
                    "name": s.get("name", ""),
                    "mode_of_operation": s.get("mode_of_operation", 8),
                    "rxpdo": s.get("rxpdo", []),
                    "txpdo": s.get("txpdo", []),
                }
                for i, s in enumerate(self.slaves)
            ],
        }

    def _apply_config(self, cfg):
        net = cfg.get("network", {})
        self.iface_var.set(net.get("interface", "eth0"))
        self.redundant_iface_var.set(net.get("redundant_interface", ""))
        self.cycle_var.set(str(net.get("cycle_time_us", 250)))
        self.cycles_var.set(str(net.get("number_of_cycles", 0)))
        self.dc_var.set(bool(net.get("distributed_clock", True)))
        self.shift_var.set(str(net.get("sync0_shift_us", 0)))
        self.kp_var.set(str(net.get("sync_kp_div", 100)))
        self.ki_var.set(str(net.get("sync_ki_div", 20)))
        self.slaves = []
        for s in cfg.get("slaves", []):
            self.slaves.append({
                "name": s.get("name", ""),
                "mode_of_operation": s.get("mode_of_operation", 8),
                "rxpdo": s.get("rxpdo", []),
                "txpdo": s.get("txpdo", []),
            })
        self.current = None
        if self.slaves:
            self._refresh_slave_list()
            self.slave_list.selection_set(0)
            self._load_slave(0)
        else:
            self.sname_var.set("")
            self.mode_cb.set("")
            self.rx_editor.set_entries([])
            self.tx_editor.set_entries([])
        self._refresh_slave_list()

    def _new_config(self, confirm=True):
        if confirm and not messagebox.askyesno("New", "Discard the current configuration?"):
            return
        self.path = None
        self._apply_config({
            "network": {"interface": "eth0", "redundant_interface": "", "cycle_time_us": 250,
                        "number_of_cycles": 12000, "distributed_clock": True,
                        "sync0_shift_us": 0},
            "slaves": [{"name": "Elmo Platinum", "mode_of_operation": 8,
                        "rxpdo": [{"index": i, "subindex": s, "bitlen": b, "name": n}
                                  for i, s, b, n in MODE_TEMPLATES[8]["rx"]],
                        "txpdo": [{"index": i, "subindex": s, "bitlen": b, "name": n}
                                  for i, s, b, n in MODE_TEMPLATES[8]["tx"]]}],
        })

    def _open(self):
        path = filedialog.askopenfilename(
            title="Open configuration",
            filetypes=[("JSON config", "*.json"), ("All files", "*.*")])
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8") as fh:
                cfg = json.load(fh)
        except (OSError, ValueError) as exc:
            messagebox.showerror("Open failed", str(exc))
            return
        self._apply_config(cfg)
        self.path = path
        self.title("EtherCAT Application Config Builder - %s" % path)

    def _save(self):
        if not self.path:
            return self._save_as()
        self._write(self.path)

    def _save_as(self):
        path = filedialog.asksaveasfilename(
            title="Save configuration",
            defaultextension=".json",
            initialfile="ethercat_config.json",
            filetypes=[("JSON config", "*.json"), ("All files", "*.*")])
        if not path:
            return
        self._write(path)

    def _write(self, path):
        try:
            cfg = self._gather_config()
        except ValueError as exc:
            messagebox.showerror("Invalid configuration", str(exc))
            return
        try:
            with open(path, "w", encoding="utf-8") as fh:
                json.dump(cfg, fh, indent=2)
                fh.write("\n")
        except OSError as exc:
            messagebox.showerror("Save failed", str(exc))
            return
        self.path = path
        self.title("EtherCAT Application Config Builder - %s" % path)
        messagebox.showinfo("Saved", "Configuration written to:\n%s" % path)

    def _preview(self):
        try:
            cfg = self._gather_config()
        except ValueError as exc:
            messagebox.showerror("Invalid configuration", str(exc))
            return
        win = tk.Toplevel(self)
        win.title("Configuration preview")
        win.geometry("560x600")
        text = tk.Text(win, wrap="none")
        text.pack(fill="both", expand=True)
        text.insert("1.0", json.dumps(cfg, indent=2))
        text.configure(state="disabled")


if __name__ == "__main__":
    App().mainloop()
