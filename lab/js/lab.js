/* ============================================================
   STARKDB LAB — Terminal Simulator, DB Engine, B-Tree Viz
   ============================================================ */
(function () {
  'use strict';

  /* ---- IN-MEMORY STARKDB ENGINE ---- */
  const DB = {
    numeric: {},
    strings: {},
    types: {},
    typed: {},
    txn: null,
    txnLog: [],

    addN(key, value) {
      key = parseInt(key, 10);
      if (isNaN(key) || key < 0) return { ok: false, msg: 'Error: key must be a non-negative integer (0 to 4294967295)' };
      const wasUpdate = this.numeric.hasOwnProperty(key);
      if (this.txnActive()) {
        this.txnLog.push({ op: 'addn', key, value, prev: this.numeric[key] });
      }
      this.numeric[key] = value;
      return { ok: true, msg: wasUpdate ? `Updated numeric key ${key}` : `Added numeric key ${key}` };
    },

    getN(key) {
      key = parseInt(key, 10);
      if (isNaN(key)) return { ok: false, msg: 'Error: key must be an integer' };
      if (!this.numeric.hasOwnProperty(key)) return { ok: false, msg: `Key ${key} not found` };
      return { ok: true, msg: `${key} = ${this.numeric[key]}` };
    },

    delN(key) {
      key = parseInt(key, 10);
      if (isNaN(key)) return { ok: false, msg: 'Error: key must be an integer' };
      if (!this.numeric.hasOwnProperty(key)) return { ok: false, msg: `Key ${key} not found, nothing to delete` };
      if (this.txnActive()) {
        this.txnLog.push({ op: 'deln', key, prev: this.numeric[key] });
      }
      delete this.numeric[key];
      return { ok: true, msg: `Deleted numeric key ${key}` };
    },

    existsN(key) {
      key = parseInt(key, 10);
      if (isNaN(key)) return { ok: false, msg: 'Error: key must be an integer' };
      return { ok: true, msg: this.numeric.hasOwnProperty(key) ? 'true' : 'false' };
    },

    addS(key, value) {
      if (!key || !value) return { ok: false, msg: 'Error: both key and value are required for string keys' };
      const wasUpdate = this.strings.hasOwnProperty(key);
      if (this.txnActive()) {
        this.txnLog.push({ op: 'adds', key, value, prev: this.strings[key] });
      }
      this.strings[key] = value;
      const h = djb2(key);
      return { ok: true, msg: wasUpdate ? `Updated string key "${key}" (hash: ${h})` : `Added string key "${key}" (hash: ${h})` };
    },

    getS(key) {
      if (!this.strings.hasOwnProperty(key)) return { ok: false, msg: `Key "${key}" not found` };
      return { ok: true, msg: `"${key}" = ${this.strings[key]}` };
    },

    delS(key) {
      if (!this.strings.hasOwnProperty(key)) return { ok: false, msg: `Key "${key}" not found, nothing to delete` };
      if (this.txnActive()) {
        this.txnLog.push({ op: 'dels', key, prev: this.strings[key] });
      }
      delete this.strings[key];
      return { ok: true, msg: `Deleted string key "${key}"` };
    },

    existStr(key) {
      return { ok: true, msg: this.strings.hasOwnProperty(key) ? 'true' : 'false' };
    },

    defineType(name, fields) {
      if (this.types[name]) return { ok: false, msg: `Error: type "${name}" already defined` };
      if (!fields || fields.length === 0) return { ok: false, msg: 'Error: type must have at least one field' };
      this.types[name] = { fields };
      this.typed[name] = {};
      return { ok: true, msg: `Defined type "${name}" with ${fields.length} field(s)` };
    },

    undefineType(name) {
      if (!this.types[name]) return { ok: false, msg: `Error: type "${name}" not found` };
      delete this.types[name];
      delete this.typed[name];
      return { ok: true, msg: `Undefined type "${name}" and removed all its records` };
    },

    describeType(name) {
      if (!this.types[name]) return { ok: false, msg: `Error: type "${name}" not found` };
      let out = `Type: ${name}\nFields:\n`;
      this.types[name].fields.forEach(f => {
        out += `  ${f.name} = ${f.type === 'int' ? 'int' : 'string(' + f.size + ')'}\n`;
      });
      return { ok: true, msg: out.trim() };
    },

    addTyped(typeName, key, fieldVals) {
      if (!this.types[typeName]) return { ok: false, msg: `Error: type "${typeName}" not defined` };
      key = parseInt(key, 10);
      if (isNaN(key)) return { ok: false, msg: 'Error: key must be an integer' };
      const schema = this.types[typeName].fields;
      const record = {};
      schema.forEach(f => {
        if (fieldVals.hasOwnProperty(f.name)) {
          record[f.name] = fieldVals[f.name];
        } else {
          record[f.name] = f.type === 'int' ? '0' : '';
        }
      });
      if (this.txnActive()) {
        this.txnLog.push({ op: 'add_typed', typeName, key, prev: this.typed[typeName][key] ? { ...this.typed[typeName][key] } : null });
      }
      this.typed[typeName][key] = record;
      return { ok: true, msg: `Added ${typeName}:${key}` };
    },

    getTyped(typeName, key) {
      if (!this.types[typeName]) return { ok: false, msg: `Error: type "${typeName}" not defined` };
      key = parseInt(key, 10);
      if (isNaN(key)) return { ok: false, msg: 'Error: key must be an integer' };
      if (!this.typed[typeName].hasOwnProperty(key)) return { ok: false, msg: `${typeName}:${key} not found` };
      const rec = this.typed[typeName][key];
      const parts = [];
      for (const [k, v] of Object.entries(rec)) {
        const f = this.types[typeName].fields.find(ff => ff.name === k);
        if (f && f.type === 'string') {
          parts.push(`${k}="${v}"`);
        } else {
          parts.push(`${k}=${v}`);
        }
      }
      return { ok: true, msg: parts.join(' ') };
    },

    txnActive() { return this.txn !== null; },

    begin() {
      if (this.txnActive()) return { ok: false, msg: 'Error: already in a transaction' };
      this.txn = true;
      this.txnLog = [];
      return { ok: true, msg: 'Transaction started. All writes are buffered until commit.' };
    },

    commit() {
      if (!this.txnActive()) return { ok: false, msg: 'Error: no active transaction' };
      this.txn = null;
      this.txnLog = [];
      return { ok: true, msg: 'Transaction committed. All changes saved.' };
    },

    rollback() {
      if (!this.txnActive()) return { ok: false, msg: 'Error: no active transaction' };
      for (let i = this.txnLog.length - 1; i >= 0; i--) {
        const entry = this.txnLog[i];
        if (entry.op === 'addn') {
          if (entry.prev === undefined) delete this.numeric[entry.key];
          else this.numeric[entry.key] = entry.prev;
        } else if (entry.op === 'deln') {
          this.numeric[entry.key] = entry.prev;
        } else if (entry.op === 'adds') {
          if (entry.prev === undefined) delete this.strings[entry.key];
          else this.strings[entry.key] = entry.prev;
        } else if (entry.op === 'dels') {
          this.strings[entry.key] = entry.prev;
        } else if (entry.op === 'add_typed') {
          if (entry.prev === null) delete this.typed[entry.typeName][entry.key];
          else this.typed[entry.typeName][entry.key] = entry.prev;
        }
      }
      this.txn = null;
      this.txnLog = [];
      return { ok: true, msg: 'Transaction rolled back. All changes discarded.' };
    },

    stats() {
      const nc = Object.keys(this.numeric).length;
      const sc = Object.keys(this.strings).length;
      let tc = 0;
      for (const t in this.typed) tc += Object.keys(this.typed[t]).length;
      const total = nc + sc + tc;
      return {
        ok: true,
        msg: [
          `Keys:        ${total} (numeric: ${nc}, string: ${sc}, typed: ${tc})`,
          `Types:       ${Object.keys(this.types).length}`,
          `Data size:   ~${estimateSize(this)} bytes`,
          `Index pages: ~${Math.max(1, Math.ceil(total / 31))}`,
          `B-tree height: ~${Math.max(1, Math.ceil(Math.log2(total + 1) / Math.log2(31)))}`,
          `Transaction: ${this.txnActive() ? 'active' : 'none'}`,
        ].join('\n'),
      };
    },

    sync() {
      return { ok: true, msg: 'Synced. All changes flushed to disk.' };
    },

    reset() {
      this.numeric = {};
      this.strings = {};
      this.types = {};
      this.typed = {};
      this.txn = null;
      this.txnLog = [];
    },

    getDbState() {
      return {
        numeric: { ...this.numeric },
        strings: { ...this.strings },
        types: { ...this.types },
        typed: deepClone(this.typed),
      };
    },
  };

  function djb2(str) {
    let hash = 5381;
    for (let i = 0; i < str.length; i++) hash = ((hash << 5) + hash + str.charCodeAt(i)) >>> 0;
    return hash;
  }

  function estimateSize(db) {
    let size = 0;
    for (const k in db.numeric) size += String(k).length + String(db.numeric[k]).length + 8;
    for (const k in db.strings) size += k.length + String(db.strings[k]).length + 8;
    for (const t in db.typed) {
      for (const k in db.typed[t]) {
        const rec = db.typed[t][k];
        for (const f in rec) size += String(rec[f]).length + 4;
      }
    }
    return size;
  }

  function deepClone(obj) {
    return JSON.parse(JSON.stringify(obj));
  }

  /* ---- COMMAND PARSER ---- */
  function parseCommand(input) {
    const trimmed = input.trim();
    if (!trimmed) return null;

    const parts = tokenize(trimmed);
    if (!parts || parts.length === 0) return null;

    const cmd = parts[0].toLowerCase();

    if (cmd === 'help') return { cmd: 'help' };
    if (cmd === 'clear' || cmd === 'cls') return { cmd: 'clear' };
    if (cmd === 'exit') return { cmd: 'exit' };
    if (cmd === 'stats') return { cmd: 'stats' };
    if (cmd === 'sync') return { cmd: 'sync' };
    if (cmd === 'demo') return { cmd: 'demo' };
    if (cmd === 'begin') return { cmd: 'begin' };
    if (cmd === 'commit') return { cmd: 'commit' };
    if (cmd === 'rollback') return { cmd: 'rollback' };
    if (cmd === 'reset') return { cmd: 'reset' };

    if (cmd === 'addn' && parts.length >= 3) {
      return { cmd: 'addn', key: parts[1], value: parts.slice(2).join(' ') };
    }
    if (cmd === 'getn' && parts.length >= 2) {
      return { cmd: 'getn', key: parts[1] };
    }
    if (cmd === 'deln' && parts.length >= 2) {
      return { cmd: 'deln', key: parts[1] };
    }
    if (cmd === 'existsn' && parts.length >= 2) {
      return { cmd: 'existsn', key: parts[1] };
    }

    if (cmd === 'adds' && parts.length >= 3) {
      return { cmd: 'adds', key: parts[1], value: parts.slice(2).join(' ') };
    }
    if (cmd === 'gets' && parts.length >= 2) {
      return { cmd: 'gets', key: parts[1] };
    }
    if (cmd === 'dels' && parts.length >= 2) {
      return { cmd: 'dels', key: parts[1] };
    }
    if (cmd === 'exist_str' && parts.length >= 2) {
      return { cmd: 'exist_str', key: parts[1] };
    }

    if (cmd === 'undefine' && parts.length >= 2) {
      return { cmd: 'undefine', name: parts[1] };
    }
    if (cmd === 'desc' && parts.length >= 2) {
      return { cmd: 'desc', name: parts[1] };
    }

    if (cmd === 'define') {
      const defMatch = trimmed.match(/^define\s+(\w+)\s*\{\s*(.+?)\s*\}$/i);
      if (defMatch) {
        const name = defMatch[1];
        const fieldsStr = defMatch[2];
        const fields = parseFields(fieldsStr);
        if (fields) return { cmd: 'define', name, fields };
      }
    }

    if (cmd === 'add') {
      const addMatch = trimmed.match(/^add\s+(\w+)\s+(\d+)\s+(.+)/i);
      if (addMatch) {
        const fieldVals = parseFieldValues(addMatch[3]);
        if (fieldVals) return { cmd: 'add', typeName: addMatch[1], key: addMatch[2], fieldVals };
      }
    }

    if (cmd === 'get') {
      const getMatch = trimmed.match(/^get\s+(\w+)\s+(\d+)/i);
      if (getMatch) return { cmd: 'get', typeName: getMatch[1], key: getMatch[2] };
    }

    if (cmd === 'tutorial') return { cmd: 'tutorial' };
    if (cmd === 'challenge') return { cmd: 'challenge' };

    return { cmd: 'unknown', input: trimmed };
  }

  function tokenize(input) {
    const tokens = [];
    let i = 0;
    while (i < input.length) {
      if (input[i] === ' ') { i++; continue; }
      if (input[i] === '"' || input[i] === "'") {
        const quote = input[i];
        let j = i + 1;
        while (j < input.length && input[j] !== quote) j++;
        tokens.push(input.substring(i + 1, j));
        i = j + 1;
        continue;
      }
      let j = i;
      while (j < input.length && input[j] !== ' ') j++;
      tokens.push(input.substring(i, j));
      i = j;
    }
    return tokens;
  }

  function parseFields(str) {
    const fields = [];
    const fieldTokens = tokenize(str);
    let i = 0;
    while (i < fieldTokens.length) {
      const name = fieldTokens[i];
      if (i + 1 >= fieldTokens.length) return null;
      const typeStr = fieldTokens[i + 1];
      const smatch = typeStr.match(/^string\((\d+)\)$/i);
      if (smatch) {
        fields.push({ name, type: 'string', size: parseInt(smatch[1], 10) });
      } else if (typeStr.toLowerCase() === 'int') {
        fields.push({ name, type: 'int', size: 4 });
      } else {
        return null;
      }
      i += 2;
    }
    return fields.length > 0 ? fields : null;
  }

  function parseFieldValues(str) {
    const result = {};
    const regex = /(\w+)\s*=\s*(?:"([^"]*)"|'([^']*)'|(\S+))/g;
    let match;
    while ((match = regex.exec(str)) !== null) {
      const fieldName = match[1];
      const value = match[2] !== undefined ? match[2] : (match[3] !== undefined ? match[3] : match[4]);
      result[fieldName] = value;
    }
    return Object.keys(result).length > 0 ? result : null;
  }

  /* ---- COMMAND EXECUTOR ---- */
  function executeCommand(parsed) {
    if (!parsed) return [];
    const cmd = parsed.cmd;

    if (cmd === 'help') return helpOutput();
    if (cmd === 'clear') return [{ type: 'clear' }];
    if (cmd === 'exit') return [{ type: 'info', text: 'Exiting lab session. All in-memory data will be lost.' }, { type: 'clear-lab' }];
    if (cmd === 'stats') {
      const r = DB.stats();
      return [{ type: r.ok ? 'info' : 'error', text: r.msg }];
    }
    if (cmd === 'sync') {
      const r = DB.sync();
      return [{ type: 'success', text: r.msg }];
    }
    if (cmd === 'begin') {
      const r = DB.begin();
      return [{ type: r.ok ? 'success' : 'error', text: r.msg }];
    }
    if (cmd === 'commit') {
      const r = DB.commit();
      return [{ type: r.ok ? 'success' : 'error', text: r.msg }];
    }
    if (cmd === 'rollback') {
      const r = DB.rollback();
      return [{ type: r.ok ? 'success' : 'error', text: r.msg }];
    }
    if (cmd === 'addn') {
      const r = DB.addN(parsed.key, parsed.value);
      return [{ type: r.ok ? 'success' : 'error', text: r.msg }];
    }
    if (cmd === 'getn') {
      const r = DB.getN(parsed.key);
      return [{ type: r.ok ? 'output' : 'error', text: r.msg }];
    }
    if (cmd === 'deln') {
      const r = DB.delN(parsed.key);
      return [{ type: r.ok ? 'success' : 'error', text: r.msg }];
    }
    if (cmd === 'existsn') {
      const r = DB.existsN(parsed.key);
      return [{ type: 'output', text: r.msg }];
    }
    if (cmd === 'adds') {
      const r = DB.addS(parsed.key, parsed.value);
      return [{ type: r.ok ? 'success' : 'error', text: r.msg }];
    }
    if (cmd === 'gets') {
      const r = DB.getS(parsed.key);
      return [{ type: r.ok ? 'output' : 'error', text: r.msg }];
    }
    if (cmd === 'dels') {
      const r = DB.delS(parsed.key);
      return [{ type: r.ok ? 'success' : 'error', text: r.msg }];
    }
    if (cmd === 'exist_str') {
      const r = DB.existStr(parsed.key);
      return [{ type: 'output', text: r.msg }];
    }
    if (cmd === 'define') {
      const r = DB.defineType(parsed.name, parsed.fields);
      return [{ type: r.ok ? 'success' : 'error', text: r.msg }];
    }
    if (cmd === 'undefine') {
      const r = DB.undefineType(parsed.name);
      return [{ type: r.ok ? 'success' : 'error', text: r.msg }];
    }
    if (cmd === 'desc') {
      const r = DB.describeType(parsed.name);
      return [{ type: r.ok ? 'output' : 'error', text: r.msg }];
    }
    if (cmd === 'add') {
      const r = DB.addTyped(parsed.typeName, parsed.key, parsed.fieldVals);
      return [{ type: r.ok ? 'success' : 'error', text: r.msg }];
    }
    if (cmd === 'get') {
      const r = DB.getTyped(parsed.typeName, parsed.key);
      return [{ type: r.ok ? 'output' : 'error', text: r.msg }];
    }
    if (cmd === 'demo') {
      return runDemo();
    }
    if (cmd === 'reset') {
      DB.reset();
      return [{ type: 'success', text: 'Database reset. All data cleared.' }];
    }
    if (cmd === 'tutorial') {
      return tutorialOutput();
    }
    if (cmd === 'challenge') {
      return challengeOutput();
    }
    if (cmd === 'unknown') {
      return [{ type: 'error', text: `Unknown command: "${parsed.input}". Type "help" for available commands.` }];
    }
    return [{ type: 'error', text: 'Invalid command syntax. Type "help" for usage.' }];
  }

  function helpOutput() {
    return [
      { type: 'output', text: '=== STARKDB COMMAND REFERENCE ===' },
      { type: 'output', text: '' },
      { type: 'output', text: '-- General --' },
      { type: 'output', text: '  help              Show this help' },
      { type: 'output', text: '  stats             Show database statistics' },
      { type: 'output', text: '  sync              Force flush to disk' },
      { type: 'output', text: '  clear             Clear terminal' },
      { type: 'output', text: '  exit              Reset and exit session' },
      { type: 'output', text: '  demo              Run interactive demo' },
      { type: 'output', text: '  tutorial          Show tutorial guide' },
      { type: 'output', text: '  challenge         View practice challenges' },
      { type: 'output', text: '' },
      { type: 'output', text: '-- Numeric Keys --' },
      { type: 'output', text: '  addn <key> <val>  Add/update numeric key (0 to 4294967295)' },
      { type: 'output', text: '  getn <key>        Retrieve value by numeric key' },
      { type: 'output', text: '  deln <key>        Delete numeric key' },
      { type: 'output', text: '  existsn <key>     Check if numeric key exists' },
      { type: 'output', text: '' },
      { type: 'output', text: '-- String Keys --' },
      { type: 'output', text: '  adds <key> <val>  Add/update string key (djb2 hashed)' },
      { type: 'output', text: '  gets <key>        Retrieve value by string key' },
      { type: 'output', text: '  dels <key>        Delete string key' },
      { type: 'output', text: '  exist_str <key>   Check if string key exists' },
      { type: 'output', text: '' },
      { type: 'output', text: '-- Type System --' },
      { type: 'output', text: '  define <name> { <field> <type> ... }   Define a new type' },
      { type: 'output', text: '  undefine <name>   Delete a type and all its records' },
      { type: 'output', text: '  desc <name>       Describe a type\'s fields' },
      { type: 'output', text: '  add <type> <key> <f=v...>   Add typed record' },
      { type: 'output', text: '  get <type> <key>  Retrieve typed record' },
      { type: 'output', text: '' },
      { type: 'output', text: '-- Transactions --' },
      { type: 'output', text: '  begin             Start transaction' },
      { type: 'output', text: '  commit            Commit transaction' },
      { type: 'output', text: '  rollback          Rollback transaction' },
      { type: 'output', text: '' },
      { type: 'output', text: 'Field types: int, string(N)   Example: name string(32) hp int' },
    ];
  }

  function runDemo() {
    DB.reset();
    const lines = [];
    const add = (cmd, type, fn) => {
      const r = fn();
      lines.push({ type: 'cmd-echo', text: cmd });
      if (r) lines.push({ type: type || 'output', text: r.msg });
    };
    lines.push({ type: 'info', text: '=== RUNNING STARKDB DEMO ===' });
    add('stark> define player { name string(32) hp int level int gold int }', 'success', () => DB.defineType('player', [
      { name: 'name', type: 'string', size: 32 },
      { name: 'hp', type: 'int', size: 4 },
      { name: 'level', type: 'int', size: 4 },
      { name: 'gold', type: 'int', size: 4 },
    ]));
    add('stark> add player 1 name="Hero" hp=100 level=5 gold=250', 'success', () => DB.addTyped('player', '1', { name: 'Hero', hp: '100', level: '5', gold: '250' }));
    add('stark> add player 2 name="Mage" hp=80 level=7 gold=450', 'success', () => DB.addTyped('player', '2', { name: 'Mage', hp: '80', level: '7', gold: '450' }));
    add('stark> addn 1 "Save Slot 1 - Hero"', 'success', () => DB.addN('1', 'Save Slot 1 - Hero'));
    add('stark> adds "chapter" "3"', 'success', () => DB.addS('chapter', '3'));
    add('stark> adds "difficulty" "hard"', 'success', () => DB.addS('difficulty', 'hard'));
    add('stark> get player 1', 'output', () => DB.getTyped('player', '1'));
    add('stark> stats', 'info', () => DB.stats());
    lines.push({ type: 'info', text: '=== DEMO COMPLETE ===' });
    lines.push({ type: 'info', text: 'Your database now has 2 players, 1 save slot, and 2 settings.' });
    lines.push({ type: 'info', text: 'Try: get player 2, existsn 1, gets "chapter", or desc player' });
    return lines;
  }

  function tutorialOutput() {
    return [
      { type: 'output', text: '=== STARKDB LAB TUTORIAL ===' },
      { type: 'output', text: '' },
      { type: 'output', text: 'Welcome to the STARKDB interactive lab! Here you can:' },
      { type: 'output', text: '' },
      { type: 'output', text: '1. Try any STARKDB command in the terminal above.' },
      { type: 'output', text: '2. See your data visually in the "DB Inspector" tab.' },
      { type: 'output', text: '3. Visualize B-tree operations in the "B-Tree" tab.' },
      { type: 'output', text: '4. Explore how string keys are hashed in "Hash" tab.' },
      { type: 'output', text: '5. Solve practice challenges in the "Challenges" tab.' },
      { type: 'output', text: '' },
      { type: 'output', text: 'Quick Start:' },
      { type: 'output', text: '  Try: addn 1 "Hello World"' },
      { type: 'output', text: '  Try: getn 1' },
      { type: 'output', text: '  Try: define hero { name string(32) hp int level int }' },
      { type: 'output', text: '  Try: add hero 1 name="Aragorn" hp=150 level=10' },
      { type: 'output', text: '  Try: get hero 1' },
      { type: 'output', text: '  Try: stats' },
      { type: 'output', text: '' },
      { type: 'output', text: 'Type "demo" for a guided demonstration!' },
    ];
  }

  function challengeOutput() {
    return [
      { type: 'output', text: '=== PRACTICE CHALLENGES ===' },
      { type: 'output', text: '' },
      { type: 'output', text: 'Challenge 1 — Your First Database' },
      { type: 'output', text: '  Add 3 numeric keys: 1="apple", 2="banana", 3="cherry"' },
      { type: 'output', text: '  Then retrieve them all and check stats.' },
      { type: 'output', text: '' },
      { type: 'output', text: 'Challenge 2 — String Key Settings' },
      { type: 'output', text: '  Store 3 game settings using string keys:' },
      { type: 'output', text: '  "volume"=80, "fullscreen"=true, "difficulty"=normal' },
      { type: 'output', text: '' },
      { type: 'output', text: 'Challenge 3 — Define a Game Entity Type' },
      { type: 'output', text: '  Define a type "item" with fields:' },
      { type: 'output', text: '  name string(64), damage int, defense int, type string(16)' },
      { type: 'output', text: '  Add 3 items and retrieve them.' },
      { type: 'output', text: '' },
      { type: 'output', text: 'Challenge 4 — Use Transactions' },
      { type: 'output', text: '  Define a "player" type with hp, level, gold fields.' },
      { type: 'output', text: '  Use begin/add/commit to update a player atomically.' },
      { type: 'output', text: '  Then use begin/add/rollback to simulate a failed update.' },
      { type: 'output', text: '' },
      { type: 'output', text: 'Challenge 5 — Build a Save System' },
      { type: 'output', text: '  Define "player" and "inventory" types.' },
      { type: 'output', text: '  Add 1 player and 3 inventory items.' },
      { type: 'output', text: '  Store save metadata with string keys.' },
      { type: 'output', text: '  View everything with stats and the DB Inspector.' },
      { type: 'output', text: '' },
      { type: 'output', text: 'Type "reset" to start fresh for any challenge!' },
    ];
  }

  /* ---- B-TREE VISUALIZER ---- */
  class BTreeNode {
    constructor(isLeaf) {
      this.keys = [];
      this.children = [];
      this.isLeaf = isLeaf;
    }
  }

  class BTreeViz {
    constructor(degree) {
      this.degree = degree || 3;
      this.root = new BTreeNode(true);
      this.animQueue = [];
    }

    insert(key) {
      const root = this.root;
      if (root.keys.length === 2 * this.degree - 1) {
        const newRoot = new BTreeNode(false);
        newRoot.children.push(this.root);
        this.splitChild(newRoot, 0);
        this.root = newRoot;
        this.insertNonFull(this.root, key);
      } else {
        this.insertNonFull(root, key);
      }
    }

    insertNonFull(node, key) {
      if (node.isLeaf) {
        let i = node.keys.length - 1;
        while (i >= 0 && key < node.keys[i]) i--;
        node.keys.splice(i + 1, 0, key);
      } else {
        let i = node.keys.length - 1;
        while (i >= 0 && key < node.keys[i]) i--;
        i++;
        if (node.children[i].keys.length === 2 * this.degree - 1) {
          this.splitChild(node, i);
          if (key > node.keys[i]) i++;
        }
        this.insertNonFull(node.children[i], key);
      }
    }

    splitChild(parent, index) {
      const child = parent.children[index];
      const newNode = new BTreeNode(child.isLeaf);
      const mid = this.degree - 1;

      parent.keys.splice(index, 0, child.keys[mid]);
      parent.children.splice(index + 1, 0, newNode);

      newNode.keys = child.keys.splice(mid + 1);
      child.keys.splice(mid);

      if (!child.isLeaf) {
        newNode.children = child.children.splice(mid + 1);
      }
    }

    search(key, node) {
      node = node || this.root;
      let i = 0;
      while (i < node.keys.length && key > node.keys[i]) i++;
      if (i < node.keys.length && key === node.keys[i]) return { node, index: i };
      if (node.isLeaf) return null;
      return this.search(key, node.children[i]);
    }

    delete(key) {
      const result = this.deleteFromNode(this.root, key);
      if (this.root.keys.length === 0 && !this.root.isLeaf) {
        this.root = this.root.children[0];
      }
      return result;
    }

    deleteFromNode(node, key) {
      let idx = 0;
      while (idx < node.keys.length && key > node.keys[idx]) idx++;

      if (idx < node.keys.length && key === node.keys[idx]) {
        if (node.isLeaf) {
          node.keys.splice(idx, 1);
          return true;
        }
        return this.deleteFromInternal(node, idx);
      }

      if (node.isLeaf) return false;

      const isLastChild = idx === node.children.length;
      if (node.children[idx].keys.length < this.degree) {
        this.fill(node, idx);
      }
      if (isLastChild && idx > node.keys.length) {
        return this.deleteFromNode(node.children[idx - 1], key);
      }
      return this.deleteFromNode(node.children[idx], key);
    }

    deleteFromInternal(node, idx) {
      const key = node.keys[idx];
      if (node.children[idx].keys.length >= this.degree) {
        const pred = this.getPredecessor(node, idx);
        node.keys[idx] = pred;
        return this.deleteFromNode(node.children[idx], pred);
      }
      if (node.children[idx + 1].keys.length >= this.degree) {
        const succ = this.getSuccessor(node, idx);
        node.keys[idx] = succ;
        return this.deleteFromNode(node.children[idx + 1], succ);
      }
      this.merge(node, idx);
      return this.deleteFromNode(node.children[idx], key);
    }

    fill(node, idx) {
      if (idx > 0 && node.children[idx - 1].keys.length >= this.degree) {
        this.borrowFromPrev(node, idx);
      } else if (idx < node.keys.length && node.children[idx + 1].keys.length >= this.degree) {
        this.borrowFromNext(node, idx);
      } else {
        if (idx < node.keys.length) this.merge(node, idx);
        else this.merge(node, idx - 1);
      }
    }

    getPredecessor(node, idx) {
      let cur = node.children[idx];
      while (!cur.isLeaf) cur = cur.children[cur.children.length - 1];
      return cur.keys[cur.keys.length - 1];
    }

    getSuccessor(node, idx) {
      let cur = node.children[idx + 1];
      while (!cur.isLeaf) cur = cur.children[0];
      return cur.keys[0];
    }

    borrowFromPrev(node, idx) {
      const child = node.children[idx];
      const sibling = node.children[idx - 1];
      child.keys.unshift(node.keys[idx - 1]);
      if (!child.isLeaf) child.children.unshift(sibling.children.pop());
      node.keys[idx - 1] = sibling.keys.pop();
    }

    borrowFromNext(node, idx) {
      const child = node.children[idx];
      const sibling = node.children[idx + 1];
      child.keys.push(node.keys[idx]);
      if (!child.isLeaf) child.children.push(sibling.children.shift());
      node.keys[idx] = sibling.keys.shift();
    }

    merge(node, idx) {
      const child = node.children[idx];
      const sibling = node.children[idx + 1];
      child.keys.push(node.keys[idx]);
      for (let i = 0; i < sibling.keys.length; i++) child.keys.push(sibling.keys[i]);
      if (!child.isLeaf) {
        for (let i = 0; i < sibling.children.length; i++) child.children.push(sibling.children[i]);
      }
      node.keys.splice(idx, 1);
      node.children.splice(idx + 1, 1);
    }

    getLayers(root) {
      root = root || this.root;
      const layers = [];
      const queue = [{ node: root, depth: 0 }];
      while (queue.length > 0) {
        const { node, depth } = queue.shift();
        if (!layers[depth]) layers[depth] = [];
        layers[depth].push(node);
        if (!node.isLeaf) {
          for (const child of node.children) {
            queue.push({ node: child, depth: depth + 1 });
          }
        }
      }
      return layers;
    }
  }

  /* ---- B-TREE RENDERER ---- */
  const BTreeRenderer = {
    tree: null,
    svgNS: 'http://www.w3.org/2000/svg',

    init(container) {
      this.container = container;
      this.tree = new BTreeViz(3);
      this.render();
    },

    insertKey(key) {
      this.tree.insert(key);
      this.render();
      return `Inserted ${key} into B-Tree`;
    },

    deleteKey(key) {
      const found = this.tree.search(key);
      if (!found) return `Key ${key} not found in B-Tree`;
      this.tree.delete(key);
      this.render();
      return `Deleted ${key} from B-Tree`;
    },

    searchKey(key) {
      const found = this.tree.search(key);
      if (found) {
        this.render(found.node, found.index);
        return `Found key ${key} in B-Tree`;
      }
      this.render();
      return `Key ${key} not found in B-Tree`;
    },

    render(highlightNode, highlightIndex) {
      if (!this.container) return;
      this.container.innerHTML = '';
      const width = this.container.clientWidth || 700;
      const height = 400;

      const svg = document.createElementNS(this.svgNS, 'svg');
      svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
      svg.setAttribute('width', '100%');
      svg.setAttribute('height', height);
      svg.style.display = 'block';
      this.container.appendChild(svg);

      const layers = this.tree.getLayers();
      if (layers.length === 0 || (layers.length === 1 && layers[0][0].keys.length === 0)) {
        const text = document.createElementNS(this.svgNS, 'text');
        text.setAttribute('x', width / 2);
        text.setAttribute('y', height / 2);
        text.setAttribute('text-anchor', 'middle');
        text.setAttribute('fill', '#7070a0');
        text.setAttribute('font-family', 'Space Mono, monospace');
        text.setAttribute('font-size', '14');
        text.textContent = 'Empty tree — insert a key to see the B-Tree';
        svg.appendChild(text);
        return;
      }

      const layerH = height / (layers.length + 1);
      const positions = [];

      for (let d = 0; d < layers.length; d++) {
        const nodes = layers[d];
        const y = layerH * (d + 1);
        const spacing = width / (nodes.length + 1);
        positions[d] = [];

        for (let n = 0; n < nodes.length; n++) {
          const node = nodes[n];
          const x = spacing * (n + 1);
          positions[d].push({ x, y, node, keys: node.keys });
        }
      }

      for (let d = 0; d < layers.length - 1; d++) {
        for (let p = 0; p < positions[d].length; p++) {
          const parent = positions[d][p];
          let childOffset = 0;
          for (let c = 0; c < layers[d + 1].length; c++) {
            if (layers[d + 1][c] === parent.node.children[childOffset]) {
              const child = positions[d + 1][c];
              const line = document.createElementNS(this.svgNS, 'line');
              line.setAttribute('x1', parent.x);
              line.setAttribute('y1', parent.y + 22);
              line.setAttribute('x2', child.x);
              line.setAttribute('y2', child.y - 22);
              line.setAttribute('stroke', '#2a2a38');
              line.setAttribute('stroke-width', '1.5');
              line.setAttribute('opacity', '0.6');
              svg.appendChild(line);
              childOffset++;
            }
          }
        }
      }

      for (let d = 0; d < positions.length; d++) {
        for (let p = 0; p < positions[d].length; p++) {
          const { x, y, node } = positions[d][p];
          const keyCount = node.keys.length;
          const boxW = Math.max(40, keyCount * 32 + 16);
          const boxH = 36;

          const rect = document.createElementNS(this.svgNS, 'rect');
          rect.setAttribute('x', x - boxW / 2);
          rect.setAttribute('y', y - boxH / 2);
          rect.setAttribute('width', boxW);
          rect.setAttribute('height', boxH);
          rect.setAttribute('rx', '6');
          rect.setAttribute('ry', '6');

          const isHighlighted = node === highlightNode;
          if (isHighlighted) {
            rect.setAttribute('fill', 'rgba(232,255,71,0.12)');
            rect.setAttribute('stroke', '#e8ff47');
            rect.setAttribute('stroke-width', '2');
          } else if (node.isLeaf) {
            rect.setAttribute('fill', 'rgba(71,255,232,0.06)');
            rect.setAttribute('stroke', 'rgba(71,255,232,0.3)');
            rect.setAttribute('stroke-width', '1');
          } else {
            rect.setAttribute('fill', 'rgba(196,127,255,0.06)');
            rect.setAttribute('stroke', 'rgba(196,127,255,0.3)');
            rect.setAttribute('stroke-width', '1');
          }

          svg.appendChild(rect);

          for (let k = 0; k < keyCount; k++) {
            const kx = x - ((keyCount - 1) * 32) / 2 + k * 32;
            const text = document.createElementNS(this.svgNS, 'text');
            text.setAttribute('x', kx);
            text.setAttribute('y', y + 5);
            text.setAttribute('text-anchor', 'middle');
            text.setAttribute('font-family', 'Space Mono, monospace');
            text.setAttribute('font-size', '13');
            text.setAttribute('fill', isHighlighted && k === highlightIndex ? '#0a0a0f' : '#e8e8f0');
            text.setAttribute('font-weight', isHighlighted && k === highlightIndex ? '700' : '400');
            text.textContent = node.keys[k];

            if (isHighlighted && k === highlightIndex) {
              const bg = document.createElementNS(this.svgNS, 'rect');
              bg.setAttribute('x', kx - 14);
              bg.setAttribute('y', y - boxH / 2 + 3);
              bg.setAttribute('width', 28);
              bg.setAttribute('height', boxH - 6);
              bg.setAttribute('rx', '3');
              bg.setAttribute('fill', '#e8ff47');
              svg.insertBefore(bg, text);
            }

            svg.appendChild(text);
          }
        }
      }

      const legendY = height - 20;
      const legendItems = [
        { label: 'Internal Node', color: 'rgba(196,127,255,0.3)' },
        { label: 'Leaf Node', color: 'rgba(71,255,232,0.3)' },
        { label: 'Degree: 3', color: null },
      ];
      let lx = 20;
      legendItems.forEach(item => {
        if (item.color) {
          const r = document.createElementNS(this.svgNS, 'rect');
          r.setAttribute('x', lx);
          r.setAttribute('y', legendY - 6);
          r.setAttribute('width', 10);
          r.setAttribute('height', 10);
          r.setAttribute('rx', '2');
          r.setAttribute('fill', item.color);
          r.setAttribute('stroke', item.color);
          r.setAttribute('stroke-width', '1');
          svg.appendChild(r);
          lx += 14;
        }
        const t = document.createElementNS(this.svgNS, 'text');
        t.setAttribute('x', lx);
        t.setAttribute('y', legendY + 3);
        t.setAttribute('font-family', 'Space Mono, monospace');
        t.setAttribute('font-size', '9');
        t.setAttribute('fill', '#7070a0');
        t.textContent = item.label;
        svg.appendChild(t);
        lx += (item.label.length * 6) + 20;
      });
    },

    reset() {
      this.tree = new BTreeViz(3);
      this.render();
    },
  };

  /* ---- HASH EXPLORER ---- */
  const HashExplorer = {
    container: null,

    init(container) {
      this.container = container;
    },

    compute(str) {
      if (!str) return;
      const steps = [];
      let hash = 5381;
      steps.push({ char: 'start', hash, calc: `hash = 5381` });
      for (let i = 0; i < str.length; i++) {
        const c = str[i];
        const code = c.charCodeAt(0);
        const prevHash = hash;
        hash = ((hash << 5) + hash + code) >>> 0;
        steps.push({ char: c, code, hash, calc: `(((${prevHash} << 5) + ${prevHash}) + ${code}) = ${hash}` });
      }
      this.render(steps);
      return hash;
    },

    render(steps) {
      if (!this.container) return;
      let html = '<div class="hash-steps">';
      html += '<div class="hash-title">djb2 Hash Algorithm — Step by Step</div>';
      html += '<div class="hash-formula">hash = ((hash &lt;&lt; 5) + hash) + char_code</div>';
      steps.forEach((step, i) => {
        if (i === 0) {
          html += `<div class="hash-step"><span class="hash-num">0</span><span class="hash-calc">${step.calc}</span></div>`;
        } else {
          html += `<div class="hash-step">`;
          html += `<span class="hash-num">${i}</span>`;
          html += `<span class="hash-char">'${step.char}'</span>`;
          html += `<span class="hash-code">ASCII: ${step.code}</span>`;
          html += `<span class="hash-calc">→ ${step.hash}</span>`;
          html += `</div>`;
        }
      });
      if (steps.length > 1) {
        const final = steps[steps.length - 1].hash;
        html += `<div class="hash-result">Final uint32 hash: <strong>${final}</strong> (stored in B-tree index)</div>`;
      }
      html += '</div>';
      this.container.innerHTML = html;
    },
  };

  /* ---- TERMINAL UI ---- */
  const Terminal = {
    container: null,
    outputEl: null,
    inputEl: null,
    history: [],
    historyIndex: -1,
    savedInput: '',

    init(container) {
      this.container = container;
      this.container.innerHTML = '';

      this.outputEl = document.createElement('div');
      this.outputEl.className = 'terminal-output';
      this.container.appendChild(this.outputEl);

      this.inputLineEl = document.createElement('div');
      this.inputLineEl.className = 'terminal-input-line';
      this.container.appendChild(this.inputLineEl);

      const promptEl = document.createElement('span');
      promptEl.className = 'terminal-prompt';
      promptEl.textContent = 'stark>';
      this.inputLineEl.appendChild(promptEl);

      this.inputEl = document.createElement('input');
      this.inputEl.type = 'text';
      this.inputEl.className = 'terminal-input';
      this.inputEl.setAttribute('aria-label', 'STARKDB command input');
      this.inputEl.setAttribute('autocomplete', 'off');
      this.inputEl.setAttribute('spellcheck', 'false');
      this.inputEl.addEventListener('keydown', (e) => this.handleKeyDown(e));
      this.inputEl.addEventListener('input', () => this.historyIndex = -1);
      this.inputLineEl.appendChild(this.inputEl);

      this.welcomeMsg();
      this.focus();
      this.container.addEventListener('click', () => this.focus());
    },

    welcomeMsg() {
      this.write([
        { type: 'info', text: '╔══════════════════════════════════════╗' },
        { type: 'info', text: '║   Welcome to the STARKDB Interactive  ║' },
        { type: 'info', text: '║           Learning Lab                ║' },
        { type: 'info', text: '╚══════════════════════════════════════╝' },
        { type: 'info', text: '' },
        { type: 'info', text: 'Type "help" for commands, "tutorial" for guide, or "demo" for a tour.' },
        { type: 'info', text: 'Your data is stored in-memory — it resets on page reload.' },
        { type: 'info', text: '' },
        { type: 'info', text: 'Use ↑/↓ arrows for command history.' },
        { type: 'info', text: '' },
      ]);
    },

    focus() {
      if (this.inputEl) this.inputEl.focus();
    },

    write(lines) {
      lines.forEach(line => {
        const div = document.createElement('div');
        div.className = 'terminal-line';
        if (line.type === 'cmd-echo') div.classList.add('cmd-echo');
        else if (line.type === 'success') div.classList.add('output-success');
        else if (line.type === 'error') div.classList.add('output-error');
        else if (line.type === 'info') div.classList.add('output-info');
        else if (line.type === 'output') div.classList.add('output-text');
        else if (line.type === 'warning') div.classList.add('output-warning');
        div.textContent = line.text;
        this.outputEl.appendChild(div);
      });
      this.scrollToBottom();
      this.emitDbChange();
    },

    execute(input) {
      const trimmed = input.trim();
      if (!trimmed) return;

      this.history.push(trimmed);
      this.historyIndex = this.history.length;

      this.write([{ type: 'cmd-echo', text: 'stark> ' + trimmed }]);

      const parsed = parseCommand(trimmed);
      if (parsed && parsed.cmd === 'clear') {
        this.outputEl.innerHTML = '';
        return;
      }
      if (parsed && parsed.cmd === 'exit') {
        this.outputEl.innerHTML = '';
        DB.reset();
        this.welcomeMsg();
        return;
      }

      const results = executeCommand(parsed);
      if (results && results.length > 0) {
        this.write(results);
      }
    },

    handleKeyDown(e) {
      if (e.key === 'Enter') {
        e.preventDefault();
        const input = this.inputEl.value;
        this.inputEl.value = '';
        this.execute(input);
      } else if (e.key === 'ArrowUp') {
        e.preventDefault();
        if (this.history.length === 0) return;
        if (this.historyIndex === this.history.length) {
          this.savedInput = this.inputEl.value;
        }
        if (this.historyIndex > 0) {
          this.historyIndex--;
          this.inputEl.value = this.history[this.historyIndex];
        }
        setTimeout(() => {
          this.inputEl.selectionStart = this.inputEl.value.length;
        }, 0);
      } else if (e.key === 'ArrowDown') {
        e.preventDefault();
        if (this.historyIndex < this.history.length - 1) {
          this.historyIndex++;
          this.inputEl.value = this.history[this.historyIndex];
        } else if (this.historyIndex === this.history.length - 1) {
          this.historyIndex = this.history.length;
          this.inputEl.value = this.savedInput || '';
        }
        setTimeout(() => {
          this.inputEl.selectionStart = this.inputEl.value.length;
        }, 0);
      } else if (e.key === 'Tab') {
        e.preventDefault();
        const val = this.inputEl.value.toLowerCase();
        const commands = ['addn', 'adds', 'add', 'getn', 'gets', 'get', 'deln', 'dels',
          'existsn', 'exist_str', 'define', 'undefine', 'desc',
          'begin', 'commit', 'rollback', 'stats', 'help', 'demo', 'clear', 'sync', 'tutorial', 'challenge', 'exit'];
        const matched = commands.filter(c => c.startsWith(val));
        if (matched.length === 1) {
          this.inputEl.value = matched[0] + ' ';
        } else if (matched.length > 1) {
          this.write([{ type: 'info', text: matched.join('  ') }]);
        }
      }
    },

    scrollToBottom() {
      if (this.outputEl) this.outputEl.scrollTop = this.outputEl.scrollHeight;
    },

    emitDbChange() {
      window.dispatchEvent(new CustomEvent('starkdb:change', { detail: DB.getDbState() }));
    },
  };

  /* ---- DB INSPECTOR PANEL ---- */
  function updateInspector() {
    const el = document.getElementById('db-inspector');
    if (!el) return;
    const state = DB.getDbState();
    let html = '';

    if (Object.keys(state.numeric).length > 0) {
      html += '<div class="inspector-section"><div class="inspector-title accent">Numeric Keys</div>';
      for (const [k, v] of Object.entries(state.numeric)) {
        html += `<div class="inspector-row"><span class="inspector-key">${esc(k)}</span><span class="inspector-arrow">→</span><span class="inspector-val">${esc(v)}</span></div>`;
      }
      html += '</div>';
    }

    if (Object.keys(state.strings).length > 0) {
      html += '<div class="inspector-section"><div class="inspector-title accent2">String Keys</div>';
      for (const [k, v] of Object.entries(state.strings)) {
        html += `<div class="inspector-row"><span class="inspector-key">"${esc(k)}"</span><span class="inspector-arrow">→</span><span class="inspector-val">${esc(v)}</span></div>`;
      }
      html += '</div>';
    }

    if (Object.keys(state.types).length > 0) {
      html += '<div class="inspector-section"><div class="inspector-title accent3">Typed Records</div>';
      for (const [typeName, typeDef] of Object.entries(state.types)) {
        html += `<div class="inspector-type-name">${esc(typeName)} <span class="inspector-fields">(${typeDef.fields.map(f => esc(f.name)).join(', ')})</span></div>`;
        const records = state.typed[typeName] || {};
        for (const [k, rec] of Object.entries(records)) {
          const vals = [];
          for (const [fk, fv] of Object.entries(rec)) vals.push(`${esc(fk)}=${esc(fv)}`);
          html += `<div class="inspector-row"><span class="inspector-key">${esc(k)}</span><span class="inspector-arrow">→</span><span class="inspector-val">${vals.join(' ')}</span></div>`;
        }
      }
      html += '</div>';
    }

    const nc = Object.keys(state.numeric).length;
    const sc = Object.keys(state.strings).length;
    let tc = 0;
    for (const t in state.typed) tc += Object.keys(state.typed[t]).length;
    const total = nc + sc + tc;

    if (total === 0) {
      html = '<div class="inspector-empty">Database is empty. Try a command like:<br><code>addn 1 "Hello"</code> or <code>demo</code></div>';
    }

    el.innerHTML = html;
  }

  function esc(s) {
    const str = String(s);
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  /* ---- CHALLENGES PANEL ---- */
  function setupChallenges() {
    const el = document.getElementById('challenge-content');
    if (!el) return;
    const challenges = [
      {
        title: 'Challenge 1: Your First Keys',
        desc: 'Add 3 numeric keys and retrieve them all.',
        hint: 'addn 1 "apple"\naddn 2 "banana"\naddn 3 "cherry"\ngetn 1\ngetn 2\ngetn 3',
        validate() {
          return DB.numeric['1'] === 'apple' && DB.numeric['2'] === 'banana' && DB.numeric['3'] === 'cherry';
        },
      },
      {
        title: 'Challenge 2: Game Settings',
        desc: 'Store 3 settings with string keys: "volume"=80, "fullscreen"=true, "difficulty"=normal',
        hint: 'adds "volume" 80\nadds "fullscreen" true\nadds "difficulty" normal',
        validate() {
          return DB.strings['volume'] === '80' && DB.strings['fullscreen'] === 'true' && DB.strings['difficulty'] === 'normal';
        },
      },
      {
        title: 'Challenge 3: Define an Item Type',
        desc: 'Define type "item" with: name string(64), damage int, defense int, type string(16). Then add a sword and retrieve it.',
        hint: 'define item { name string(64) damage int defense int type string(16) }\nadd item 1 name="Iron Sword" damage=15 defense=0 type="weapon"\nget item 1',
        validate() {
          const t = DB.types['item'];
          return t && t.fields.length === 4 && DB.typed['item'] && DB.typed['item']['1'] && DB.typed['item']['1'].name === 'Iron Sword';
        },
      },
      {
        title: 'Challenge 4: Atomic Update',
        desc: 'Define "player" with hp, level, gold. Add one player. Then use begin/add/commit to update them atomically.',
        hint: 'define player { hp int level int gold int }\nadd player 1 hp=100 level=1 gold=0\nbegin\nadd player 1 hp=75 level=2 gold=100\ncommit\nget player 1',
        validate() {
          const p = DB.typed['player'] && DB.typed['player']['1'];
          return p && p.hp === '75' && p.level === '2' && p.gold === '100';
        },
      },
      {
        title: 'Challenge 5: Transaction Rollback',
        desc: 'Start a transaction, make a bad update to player 1, roll back, and verify the original data is restored.',
        hint: 'begin\nadd player 1 hp=999 level=999 gold=999\nrollback\nget player 1',
        validate() {
          const p = DB.typed['player'] && DB.typed['player']['1'];
          return p && p.hp === '75' && p.level === '2' && p.gold === '100' && DB.txn === null;
        },
      },
    ];

    challenges.forEach((ch, i) => {
      const card = document.createElement('div');
      card.className = 'challenge-card';
      card.innerHTML = `
        <div class="challenge-header">
          <span class="challenge-num">${i + 1}</span>
          <div>
            <div class="challenge-title">${ch.title}</div>
            <div class="challenge-desc">${ch.desc}</div>
          </div>
          <button class="challenge-check" data-idx="${i}" aria-label="Check challenge ${i + 1}">Check</button>
        </div>
        <div class="challenge-hint" id="challenge-hint-${i}" style="display:none">
          <pre><code>${ch.hint}</code></pre>
        </div>
        <button class="challenge-hint-btn" data-idx="${i}">Show Hint</button>
      `;
      el.appendChild(card);
    });

    el.addEventListener('click', function (e) {
      const checkBtn = e.target.closest('.challenge-check');
      if (checkBtn) {
        const idx = parseInt(checkBtn.dataset.idx);
        if (challenges[idx].validate()) {
          checkBtn.textContent = 'Done!';
          checkBtn.classList.add('done');
          checkBtn.disabled = true;
        } else {
          checkBtn.textContent = 'Not Yet';
          checkBtn.classList.add('fail');
          setTimeout(() => {
            checkBtn.textContent = 'Check';
            checkBtn.classList.remove('fail');
          }, 800);
        }
      }
      const hintBtn = e.target.closest('.challenge-hint-btn');
      if (hintBtn) {
        const idx = parseInt(hintBtn.dataset.idx);
        const hintEl = document.getElementById('challenge-hint-' + idx);
        if (hintEl.style.display === 'none') {
          hintEl.style.display = 'block';
          hintBtn.textContent = 'Hide Hint';
        } else {
          hintEl.style.display = 'none';
          hintBtn.textContent = 'Show Hint';
        }
      }
    });
  }

  /* ---- B-TREE PANEL CONTROLS ---- */
  function setupBTreePanel() {
    const insertBtn = document.getElementById('btree-insert-btn');
    const deleteBtn = document.getElementById('btree-delete-btn');
    const searchBtn = document.getElementById('btree-search-btn');
    const resetBtn = document.getElementById('btree-reset-btn');
    const insertInput = document.getElementById('btree-insert-input');
    const deleteInput = document.getElementById('btree-delete-input');
    const searchInput = document.getElementById('btree-search-input');
    const feedback = document.getElementById('btree-feedback');

    function showFeedback(msg) {
      if (feedback) {
        feedback.textContent = msg;
        feedback.style.opacity = '1';
        setTimeout(() => { feedback.style.opacity = '0'; }, 2000);
      }
    }

    if (insertBtn && insertInput) {
      insertBtn.addEventListener('click', () => {
        const val = parseInt(insertInput.value);
        if (isNaN(val)) { showFeedback('Enter a valid number'); return; }
        const msg = BTreeRenderer.insertKey(val);
        showFeedback(msg);
        insertInput.value = '';
      });
      insertInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') { insertBtn.click(); }
      });
    }

    if (deleteBtn && deleteInput) {
      deleteBtn.addEventListener('click', () => {
        const val = parseInt(deleteInput.value);
        if (isNaN(val)) { showFeedback('Enter a valid number'); return; }
        const msg = BTreeRenderer.deleteKey(val);
        showFeedback(msg);
        deleteInput.value = '';
      });
      deleteInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') { deleteBtn.click(); }
      });
    }

    if (searchBtn && searchInput) {
      searchBtn.addEventListener('click', () => {
        const val = parseInt(searchInput.value);
        if (isNaN(val)) { showFeedback('Enter a valid number'); return; }
        const msg = BTreeRenderer.searchKey(val);
        showFeedback(msg);
        searchInput.value = '';
      });
      searchInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') { searchBtn.click(); }
      });
    }

    if (resetBtn) {
      resetBtn.addEventListener('click', () => {
        BTreeRenderer.reset();
        showFeedback('B-Tree reset');
      });
    }
  }

  /* ---- HASH PANEL ---- */
  function setupHashPanel() {
    const input = document.getElementById('hash-input');
    const button = document.getElementById('hash-btn');
    if (!input || !button) return;

    button.addEventListener('click', () => {
      HashExplorer.compute(input.value);
    });
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') HashExplorer.compute(input.value);
    });
    input.addEventListener('input', () => {
      const val = input.value;
      if (val.length > 0) {
        document.getElementById('hash-quick').textContent = 'Live: ' + djb2(val);
      } else {
        document.getElementById('hash-quick').textContent = 'Type a string to see its djb2 hash';
      }
    });
  }

  /* ---- TAB SWITCHING IN LAB ---- */
  function setupLabTabs() {
    document.addEventListener('click', function (e) {
      const btn = e.target.closest('.lab-tab-btn');
      if (!btn) return;
      const container = btn.closest('.lab-tabs-container');
      if (!container) return;
      const tabId = btn.dataset.tab;

      container.querySelectorAll('.lab-tab-btn').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');

      const panels = container.querySelectorAll('.lab-tab-panel');
      panels.forEach(p => p.classList.remove('active'));
      const target = container.querySelector('.lab-tab-panel[data-tab="' + tabId + '"]');
      if (target) target.classList.add('active');
      if (tabId === 'btree') {
        requestAnimationFrame(() => BTreeRenderer.render());
      }
    });
  }

  /* ---- INIT ---- */
  document.addEventListener('DOMContentLoaded', function () {
    const termContainer = document.getElementById('lab-terminal');
    if (termContainer) {
      Terminal.init(termContainer);
    }

    const btreeContainer = document.getElementById('btree-canvas');
    if (btreeContainer) {
      BTreeRenderer.init(btreeContainer);
      setupBTreePanel();
    }

    const hashContainer = document.getElementById('hash-explorer');
    if (hashContainer) {
      HashExplorer.init(hashContainer);
      setupHashPanel();
    }

    setupLabTabs();
    setupChallenges();

    window.addEventListener('starkdb:change', updateInspector);

    document.getElementById('lab-quick-commands')?.addEventListener('click', function (e) {
      const quickBtn = e.target.closest('.quick-cmd');
      if (!quickBtn) return;
      const cmd = quickBtn.dataset.cmd;
      if (cmd) {
        Terminal.execute(cmd);
        Terminal.focus();
      }
    });
  });
})();
