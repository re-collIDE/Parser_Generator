let wasmModule = null;
let wasmModulePromise = null;

function escapeHtml(value) {
    return String(value ?? '').replace(/[&<>"']/g, char => ({
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#39;'
    }[char]));
}

function renderTokenBadges(tokens) {
    return tokens.map(token => `<span class="px-2 py-0.5 bg-secondary-container text-on-secondary-container rounded-full label-text text-[10px] font-bold">${escapeHtml(token)}</span>`).join('');
}

function formatProduction(production) {
    return `${escapeHtml(production.lhs)} → ${production.rhs.map(symbol => escapeHtml(symbol)).join(' ')}`;
}

function normalizeProduction(production) {
    return {
        ...production,
        rhs: production.rhs.length === 1 && production.rhs[0] === 'ε' ? [] : [...production.rhs]
    };
}

function loadWasmModule() {
    if (wasmModule) {
        return Promise.resolve(wasmModule);
    }

    if (wasmModulePromise) {
        return wasmModulePromise;
    }

    wasmModulePromise = new Promise((resolve, reject) => {
        const initializeModule = () => {
            const moduleFactory = globalThis.Module;

            if (typeof moduleFactory === 'function') {
                Promise.resolve(moduleFactory())
                    .then(instance => {
                        wasmModule = instance;
                        console.log('WASM module loaded successfully.');
                        resolve(instance);
                    })
                    .catch(reject);
                return;
            }

            if (moduleFactory && typeof moduleFactory === 'object') {
                wasmModule = moduleFactory;
                console.log('WASM module loaded successfully.');
                resolve(moduleFactory);
                return;
            }

            reject(new Error('WASM module factory was not found after loading wasm/module.js.'));
        };

        if (globalThis.Module) {
            initializeModule();
            return;
        }

        const existingScript = document.querySelector('script[data-wasm-module="true"]');
        if (existingScript) {
            existingScript.addEventListener('load', initializeModule, { once: true });
            existingScript.addEventListener('error', () => reject(new Error('Failed to load wasm/module.js.')), { once: true });
            return;
        }

        const script = document.createElement('script');
        script.src = 'wasm/module.js';
        script.dataset.wasmModule = 'true';
        script.onload = initializeModule;
        script.onerror = () => reject(new Error('Failed to load wasm/module.js.'));
        document.head.appendChild(script);
    }).catch(error => {
        wasmModulePromise = null;
        throw error;
    });

    return wasmModulePromise;
}

async function callWasmProcessor(exportName, grammar) {
    const module = await loadWasmModule();
    const processor = module[exportName];

    if (typeof processor !== 'function') {
        throw new Error(`Missing WASM export: ${exportName}`);
    }

    let stringOnWasmHeap = 0;
    let resultPtr = 0;

    try {
        const lengthBytes = module.lengthBytesUTF8(grammar) + 1;
        stringOnWasmHeap = module._malloc(lengthBytes);
        module.stringToUTF8(grammar, stringOnWasmHeap, lengthBytes);

        resultPtr = processor(stringOnWasmHeap);
        const resultStr = module.UTF8ToString(resultPtr);
        const parsed = JSON.parse(resultStr);

        if (parsed.error) {
            throw new Error(parsed.error);
        }

        return parsed;
    } finally {
        if (resultPtr) {
            module._free_json(resultPtr);
        }
        if (stringOnWasmHeap) {
            module._free(stringOnWasmHeap);
        }
    }
}

function isNonTerminalSymbol(symbol) {
    return /^[A-Z]/.test(symbol);
}

function parseGrammarText(grammar) {
    const productions = [];
    const nonTerminalOrder = [];
    const nonTerminalSet = new Set();
    const symbolOrder = [];
    const symbolSet = new Set();

    const registerSymbol = symbol => {
        if (symbol && !symbolSet.has(symbol)) {
            symbolSet.add(symbol);
            symbolOrder.push(symbol);
        }
    };

    const registerNonTerminal = symbol => {
        if (!nonTerminalSet.has(symbol)) {
            nonTerminalSet.add(symbol);
            nonTerminalOrder.push(symbol);
        }
        registerSymbol(symbol);
    };

    const lines = grammar.split(/\r?\n/).map(line => line.trim()).filter(Boolean);
    if (lines.length === 0) {
        throw new Error('The grammar does not contain any productions.');
    }

    lines.forEach(line => {
        const arrow = line.includes('->') ? '->' : line.includes('→') ? '→' : null;
        if (!arrow) {
            throw new Error(`Invalid production: ${line}`);
        }

        const [rawLhs, rawRhs] = line.split(arrow);
        const lhs = rawLhs?.trim();
        const rhsText = rawRhs?.trim() ?? '';

        if (!lhs) {
            throw new Error(`Missing left-hand side in production: ${line}`);
        }

        registerNonTerminal(lhs);

        const alternatives = rhsText.length > 0 ? rhsText.split('|') : [''];
        alternatives.forEach(alternative => {
            const tokens = alternative.trim().split(/\s+/).filter(Boolean);
            const rhs = tokens.length === 0 || (tokens.length === 1 && ['ε', 'e', 'epsilon'].includes(tokens[0]))
                ? []
                : tokens.filter(token => !['ε', 'e', 'epsilon'].includes(token));

            rhs.forEach(symbol => {
                registerSymbol(symbol);
                if (isNonTerminalSymbol(symbol)) {
                    registerNonTerminal(symbol);
                }
            });

            productions.push({ lhs, rhs });
        });
    });

    const terminals = symbolOrder.filter(symbol => !nonTerminalSet.has(symbol));

    return {
        startSymbol: productions[0].lhs,
        productions,
        terminals,
        nonTerminals: nonTerminalOrder
    };
}

function buildLL1Data(grammar) {
    const parsed = parseGrammarText(grammar);
    const nonTerminalSet = new Set(parsed.nonTerminals);
    const nullable = new Map(parsed.nonTerminals.map(symbol => [symbol, false]));
    const first = new Map();
    const follow = new Map(parsed.nonTerminals.map(symbol => [symbol, new Set()]));

    parsed.terminals.forEach(symbol => {
        first.set(symbol, new Set([symbol]));
    });
    parsed.nonTerminals.forEach(symbol => {
        if (!first.has(symbol)) {
            first.set(symbol, new Set());
        }
    });

    const isNullableSymbol = symbol => nonTerminalSet.has(symbol) && nullable.get(symbol);
    const getFirstSet = symbol => first.get(symbol) ?? new Set();

    let changed = true;
    while (changed) {
        changed = false;

        parsed.productions.forEach(production => {
            let allNullable = true;

            production.rhs.forEach(symbol => {
                if (!allNullable) {
                    return;
                }

                getFirstSet(symbol).forEach(token => {
                    if (!first.get(production.lhs).has(token)) {
                        first.get(production.lhs).add(token);
                        changed = true;
                    }
                });

                if (!isNullableSymbol(symbol)) {
                    allNullable = false;
                }
            });

            if (allNullable && !nullable.get(production.lhs)) {
                nullable.set(production.lhs, true);
                changed = true;
            }
        });
    }

    follow.get(parsed.startSymbol).add('$');
    changed = true;

    while (changed) {
        changed = false;

        parsed.productions.forEach(production => {
            production.rhs.forEach((symbol, index) => {
                if (!nonTerminalSet.has(symbol)) {
                    return;
                }

                let suffixNullable = true;

                for (let offset = index + 1; offset < production.rhs.length; offset += 1) {
                    const nextSymbol = production.rhs[offset];
                    getFirstSet(nextSymbol).forEach(token => {
                        if (!follow.get(symbol).has(token)) {
                            follow.get(symbol).add(token);
                            changed = true;
                        }
                    });

                    if (!isNullableSymbol(nextSymbol)) {
                        suffixNullable = false;
                        break;
                    }
                }

                if (suffixNullable) {
                    follow.get(production.lhs).forEach(token => {
                        if (!follow.get(symbol).has(token)) {
                            follow.get(symbol).add(token);
                            changed = true;
                        }
                    });
                }
            });
        });
    }

    const productionFirst = production => {
        const symbols = new Set();
        let productionNullable = true;

        production.rhs.forEach(symbol => {
            if (!productionNullable) {
                return;
            }

            getFirstSet(symbol).forEach(token => symbols.add(token));
            if (!isNullableSymbol(symbol)) {
                productionNullable = false;
            }
        });

        return {
            symbols,
            nullable: productionNullable
        };
    };

    const tableMap = new Map();
    let isLL1 = true;

    parsed.productions.forEach((production, prodIndex) => {
        const info = productionFirst(production);
        const targetTerminals = [...info.symbols];

        if (info.nullable) {
            targetTerminals.push(...follow.get(production.lhs));
        }

        targetTerminals.forEach(terminal => {
            const key = `${production.lhs}::${terminal}`;
            const existing = tableMap.get(key);
            if (existing !== undefined && existing !== prodIndex) {
                isLL1 = false;
                return;
            }
            tableMap.set(key, prodIndex);
        });
    });

    return {
        start_symbol: parsed.startSymbol,
        terminals: parsed.terminals,
        non_terminals: parsed.nonTerminals,
        productions: parsed.productions.map(production => ({
            lhs: production.lhs,
            rhs: production.rhs.length > 0 ? production.rhs : ['ε']
        })),
        first_sets: Object.fromEntries(parsed.nonTerminals.map(symbol => [
            symbol,
            [...first.get(symbol), ...(nullable.get(symbol) ? ['ε'] : [])]
        ])),
        follow_sets: Object.fromEntries(parsed.nonTerminals.map(symbol => [symbol, [...follow.get(symbol)]])),
        ll1_table: [...tableMap.entries()].map(([cell, prodIndex]) => {
            const [nt, terminal] = cell.split('::');
            return { nt, t: terminal, prod_idx: prodIndex };
        }),
        is_ll1: isLL1
    };
}

function buildLR0Data(grammar) {
    const parsed = parseGrammarText(grammar);
    let augmentedStart = `${parsed.startSymbol}'`;
    while (parsed.nonTerminals.includes(augmentedStart)) {
        augmentedStart += "'";
    }

    return {
        start_symbol: parsed.startSymbol,
        augmented_start_symbol: augmentedStart,
        terminals: parsed.terminals,
        non_terminals: parsed.nonTerminals,
        augmented_grammar: [
            { lhs: augmentedStart, rhs: [parsed.startSymbol] },
            ...parsed.productions.map(production => ({
                lhs: production.lhs,
                rhs: production.rhs.length > 0 ? production.rhs : ['ε']
            }))
        ]
    };
}

async function callWasmProcessLL1(grammar) {
    return buildLL1Data(grammar);
}

async function callWasmProcessLR0(grammar) {
    return buildLR0Data(grammar);
}

function buildLR0Automaton(data) {
    const grammar = data.augmented_grammar.map((production, index) => ({
        index,
        lhs: production.lhs,
        rhs: normalizeProduction(production).rhs
    }));

    const productionsByLhs = new Map();
    grammar.forEach(production => {
        if (!productionsByLhs.has(production.lhs)) {
            productionsByLhs.set(production.lhs, []);
        }
        productionsByLhs.get(production.lhs).push(production);
    });

    const nonTerminalSet = new Set(grammar.map(production => production.lhs));
    const terminals = [...data.terminals, '$'];
    const gotoSymbols = [...data.non_terminals];

    const itemKey = item => `${item.productionIndex}:${item.dot}`;
    const canonicalItemKey = items => [...items].map(itemKey).sort().join('|');

    function closure(items) {
        const result = new Map();
        const queue = [...items];

        queue.forEach(item => {
            result.set(itemKey(item), item);
        });

        while (queue.length > 0) {
            const current = queue.shift();
            const production = grammar[current.productionIndex];
            const nextSymbol = production.rhs[current.dot];

            if (!nextSymbol || !nonTerminalSet.has(nextSymbol)) {
                continue;
            }

            const expansions = productionsByLhs.get(nextSymbol) ?? [];
            expansions.forEach(expansion => {
                const nextItem = { productionIndex: expansion.index, dot: 0 };
                const key = itemKey(nextItem);
                if (!result.has(key)) {
                    result.set(key, nextItem);
                    queue.push(nextItem);
                }
            });
        }

        return [...result.values()].sort((a, b) => a.productionIndex - b.productionIndex || a.dot - b.dot);
    }

    function goto(items, symbol) {
        const shifted = items.flatMap(item => {
            const production = grammar[item.productionIndex];
            if (production.rhs[item.dot] === symbol) {
                return [{ productionIndex: item.productionIndex, dot: item.dot + 1 }];
            }
            return [];
        });

        return shifted.length > 0 ? closure(shifted) : [];
    }

    const states = [];
    const transitions = [];
    const stateIndexByKey = new Map();
    const queue = [];
    const allSymbols = [...new Set([...terminals.filter(symbol => symbol !== '$'), ...gotoSymbols])];

    const startState = closure([{ productionIndex: 0, dot: 0 }]);
    const startKey = canonicalItemKey(startState);
    stateIndexByKey.set(startKey, 0);
    states.push(startState);
    transitions.push(new Map());
    queue.push(0);

    while (queue.length > 0) {
        const stateIndex = queue.shift();
        const items = states[stateIndex];

        allSymbols.forEach(symbol => {
            const nextState = goto(items, symbol);
            if (nextState.length === 0) {
                return;
            }

            const key = canonicalItemKey(nextState);
            let targetIndex = stateIndexByKey.get(key);

            if (targetIndex === undefined) {
                targetIndex = states.length;
                stateIndexByKey.set(key, targetIndex);
                states.push(nextState);
                transitions.push(new Map());
                queue.push(targetIndex);
            }

            transitions[stateIndex].set(symbol, targetIndex);
        });
    }

    const actionRows = states.map(() => ({}));
    const gotoRows = states.map(() => ({}));
    const conflicts = [];

    function setAction(stateIndex, symbol, value, description) {
        const existing = actionRows[stateIndex][symbol];
        if (existing && existing !== value) {
            conflicts.push(`State ${stateIndex}, symbol ${symbol}: ${existing} / ${value} (${description})`);
            actionRows[stateIndex][symbol] = 'conflict';
            return;
        }
        actionRows[stateIndex][symbol] = value;
    }

    states.forEach((items, stateIndex) => {
        const transitionMap = transitions[stateIndex];

        transitionMap.forEach((targetIndex, symbol) => {
            if (terminals.includes(symbol) && symbol !== '$') {
                setAction(stateIndex, symbol, `s${targetIndex}`, 'shift transition');
            } else if (gotoSymbols.includes(symbol)) {
                gotoRows[stateIndex][symbol] = String(targetIndex);
            }
        });

        items.forEach(item => {
            const production = grammar[item.productionIndex];
            const atEnd = item.dot >= production.rhs.length;

            if (!atEnd) {
                return;
            }

            if (item.productionIndex === 0) {
                setAction(stateIndex, '$', 'acc', 'accept');
                return;
            }

            terminals.forEach(symbol => {
                setAction(stateIndex, symbol, `r${item.productionIndex}`, `reduce ${production.lhs} → ${production.rhs.join(' ') || 'ε'}`);
            });
        });
    });

    return {
        grammar,
        states: states.map((items, index) => ({
            index,
            items: items.map(item => ({
                ...item,
                production: grammar[item.productionIndex]
            })),
            transitions: [...transitions[index].entries()].map(([symbol, target]) => ({ symbol, target }))
        })),
        terminals,
        gotoSymbols,
        actionRows,
        gotoRows,
        conflicts
    };
}

function renderLL1(data) {
    const detailsEl = document.getElementById('ll1-grammar-details');
    if (detailsEl) {
        detailsEl.innerHTML = `
            <div class="p-3 border border-outline-variant/20 flex justify-between items-center bg-surface-container-low/30">
                <span class="label-text text-xs font-bold uppercase text-outline">Start Symbol</span>
                <span class="font-mono text-sm font-bold text-primary">${escapeHtml(data.start_symbol || '')}</span>
            </div>
            <div class="p-3 border border-outline-variant/20 flex justify-between items-center bg-surface-container-low/30">
                <span class="label-text text-xs font-bold uppercase text-outline">Terminals</span>
                <span class="font-mono text-sm font-bold text-primary">${escapeHtml(data.terminals.join(', '))}</span>
            </div>
            <div class="p-3 border border-outline-variant/20 flex justify-between items-center bg-surface-container-low/30">
                <span class="label-text text-xs font-bold uppercase text-outline">Non-Terminals</span>
                <span class="font-mono text-sm font-bold text-primary">${escapeHtml(data.non_terminals.join(', '))}</span>
            </div>
            <div class="p-3 border border-outline-variant/20 flex justify-between items-center bg-surface-container-low/30">
                <span class="label-text text-xs font-bold uppercase text-outline">LL(1) Status</span>
                <span class="font-mono text-sm font-bold ${data.is_ll1 ? 'text-primary' : 'text-error'}">${data.is_ll1 ? 'Compatible' : 'Conflict Detected'}</span>
            </div>
            <div class="p-3 border border-outline-variant/20 bg-surface-container-low/30">
                <span class="label-text text-xs font-bold uppercase text-outline block mb-3">Productions</span>
                <div class="space-y-1 font-mono text-xs text-secondary pl-2 border-l-2 border-primary/20">
                    ${data.productions.map(production => `<div>${formatProduction(production)}</div>`).join('')}
                </div>
            </div>
        `;
    }

    const firstEl = document.getElementById('ll1-first-sets');
    if (firstEl) {
        firstEl.innerHTML = Object.entries(data.first_sets).map(([nonTerminal, set]) => `
            <div class="flex items-center border-b border-surface-container py-2">
                <span class="w-12 font-bold font-mono text-on-surface">${escapeHtml(nonTerminal)}</span>
                <span class="material-symbols-outlined text-[14px] mx-4 text-outline">arrow_forward</span>
                <span class="flex gap-2 flex-wrap">
                    ${renderTokenBadges(set)}
                </span>
            </div>
        `).join('');
    }

    const followEl = document.getElementById('ll1-follow-sets');
    if (followEl) {
        followEl.innerHTML = Object.entries(data.follow_sets).map(([nonTerminal, set]) => `
            <div class="flex items-center border-b border-surface-container py-2">
                <span class="w-12 font-bold font-mono text-on-surface">${escapeHtml(nonTerminal)}</span>
                <span class="material-symbols-outlined text-[14px] mx-4 text-outline">arrow_forward</span>
                <span class="flex gap-2 flex-wrap">
                    ${renderTokenBadges(set)}
                </span>
            </div>
        `).join('');
    }

    const tableEl = document.getElementById('ll1-parsing-table');
    if (tableEl) {
        const terminals = [...data.terminals, '$'];
        const entryByCell = new Map(data.ll1_table.map(entry => [`${entry.nt}::${entry.t}`, entry]));

        const rows = data.non_terminals.map(nonTerminal => `
            <tr class="group">
                <td class="p-4 font-bold border-b border-surface-container bg-surface-container-low/20 text-primary">${escapeHtml(nonTerminal)}</td>
                ${terminals.map(terminal => {
                    const entry = entryByCell.get(`${nonTerminal}::${terminal}`);
                    if (!entry) {
                        return '<td class="p-4 border-b border-surface-container text-center bg-error-container/5 text-error italic opacity-40">err</td>';
                    }

                    const production = data.productions[entry.prod_idx];
                    return `<td class="p-4 border-b border-surface-container text-center">${formatProduction(production)}</td>`;
                }).join('')}
            </tr>
        `).join('');

        tableEl.innerHTML = `
            <div class="mb-4 text-xs font-mono ${data.is_ll1 ? 'text-outline' : 'text-error'}">
                ${data.is_ll1 ? 'No LL(1) table conflicts were detected.' : 'This grammar produces at least one LL(1) table conflict.'}
            </div>
            <table class="w-full text-left border-collapse">
                <thead>
                    <tr>
                        <th class="p-4 bg-surface-container-low border-b border-outline-variant"></th>
                        ${terminals.map(terminal => `<th class="p-4 bg-surface-container-low border-b border-outline-variant text-[11px] font-bold font-mono text-center">${escapeHtml(terminal)}</th>`).join('')}
                    </tr>
                </thead>
                <tbody class="text-xs font-mono">
                    ${rows}
                </tbody>
            </table>
        `;
    }
}

function renderLR0(data) {
    const automaton = buildLR0Automaton(data);

    const detailsEl = document.getElementById('lr0-grammar-details');
    if (detailsEl) {
        detailsEl.innerHTML = `
            <div class="p-3 border border-outline-variant/20 flex justify-between items-center bg-surface-container-low/30">
                <span class="label-text text-xs font-bold uppercase text-outline">Start Symbol</span>
                <span class="font-mono text-sm font-bold text-primary">${escapeHtml(data.start_symbol || '')}</span>
            </div>
            <div class="p-3 border border-outline-variant/20 flex justify-between items-center bg-surface-container-low/30">
                <span class="label-text text-xs font-bold uppercase text-outline">Augmented Start</span>
                <span class="font-mono text-sm font-bold text-primary">${escapeHtml(data.augmented_start_symbol || '')}</span>
            </div>
            <div class="p-3 border border-outline-variant/20 flex justify-between items-center bg-surface-container-low/30">
                <span class="label-text text-xs font-bold uppercase text-outline">Terminals</span>
                <span class="font-mono text-sm font-bold text-primary">${escapeHtml(data.terminals.join(', '))}</span>
            </div>
            <div class="p-3 border border-outline-variant/20 flex justify-between items-center bg-surface-container-low/30">
                <span class="label-text text-xs font-bold uppercase text-outline">Non-Terminals</span>
                <span class="font-mono text-sm font-bold text-primary">${escapeHtml(data.non_terminals.join(', '))}</span>
            </div>
            <div class="p-3 border border-outline-variant/20 flex justify-between items-center bg-surface-container-low/30">
                <span class="label-text text-xs font-bold uppercase text-outline">LR(0) Status</span>
                <span class="font-mono text-sm font-bold ${automaton.conflicts.length === 0 ? 'text-primary' : 'text-error'}">${automaton.conflicts.length === 0 ? 'Conflict-Free' : 'Conflict Detected'}</span>
            </div>
            <div class="p-3 border border-outline-variant/20 bg-surface-container-low/30">
                <span class="label-text text-xs font-bold uppercase text-outline block mb-3">Augmented Grammar</span>
                <div class="space-y-1 font-mono text-xs text-secondary pl-2 border-l-2 border-primary/20">
                    ${data.augmented_grammar.map(production => `<div>${formatProduction(production)}</div>`).join('')}
                </div>
            </div>
        `;
    }

    const itemsEl = document.getElementById('lr0-item-sets');
    if (itemsEl) {
        itemsEl.innerHTML = automaton.states.map(state => `
            <div class="bg-surface-container-low/30 border-l-4 border-primary p-5 group hover:bg-surface-container-low transition-all">
                <div class="flex justify-between items-start mb-3">
                    <span class="label-text text-[10px] font-bold text-primary uppercase">State I${state.index}</span>
                    <span class="material-symbols-outlined text-outline text-sm">settings_input_component</span>
                </div>
                <div class="font-mono text-xs space-y-1.5 text-on-surface-variant">
                    ${state.items.map(item => {
                        const rhs = [...item.production.rhs];
                        rhs.splice(item.dot, 0, '•');
                        const renderedRhs = rhs.length > 0 ? rhs.map(symbol => symbol === '•' ? '<span class="text-primary font-bold">•</span>' : escapeHtml(symbol)).join(' ') : '<span class="text-primary font-bold">•</span>';
                        return `<p>${escapeHtml(item.production.lhs)} → ${renderedRhs}</p>`;
                    }).join('')}
                </div>
                <div class="mt-4 pt-4 border-t border-outline-variant/10 flex flex-wrap gap-2">
                    ${state.transitions.length > 0 ? state.transitions.map(transition => `
                        <span class="px-2 py-0.5 bg-surface-container-high/50 border border-outline-variant/20 rounded text-[10px] font-mono font-bold text-primary">${escapeHtml(transition.symbol)} → I${transition.target}</span>
                    `).join('') : '<span class="text-[10px] font-mono text-outline">No outgoing transitions</span>'}
                </div>
            </div>
        `).join('');
    }

    const tableEl = document.getElementById('lr0-parsing-table');
    if (tableEl) {
        const terminalHeaders = automaton.terminals.map(terminal => `<th class="p-2 text-center font-mono text-[11px] border-b border-outline-variant/20${terminal === automaton.terminals[0] ? ' border-l' : ''}">${escapeHtml(terminal)}</th>`).join('');
        const gotoHeaders = automaton.gotoSymbols.map(symbol => `<th class="p-2 text-center font-mono text-[11px] border-b border-outline-variant/20${symbol === automaton.gotoSymbols[0] ? ' border-l' : ''}">${escapeHtml(symbol)}</th>`).join('');

        const rows = automaton.states.map(state => {
            const actionCells = automaton.terminals.map((terminal, index) => {
                const value = automaton.actionRows[state.index][terminal] ?? '';
                const classes = value.startsWith('s')
                    ? 'text-primary font-bold'
                    : value.startsWith('r')
                        ? 'text-secondary font-bold'
                        : value === 'acc'
                            ? 'text-primary font-bold'
                            : value === 'conflict'
                                ? 'text-error font-bold'
                                : '';
                return `<td class="p-4 text-center ${classes}${index === 0 ? ' border-l border-outline-variant/10' : ''}">${escapeHtml(value)}</td>`;
            }).join('');

            const gotoCells = automaton.gotoSymbols.map((symbol, index) => {
                const value = automaton.gotoRows[state.index][symbol] ?? '';
                return `<td class="p-4 text-center font-bold text-secondary${index === 0 ? ' border-l border-outline-variant/20' : ''}">${escapeHtml(value)}</td>`;
            }).join('');

            return `
                <tr class="border-b border-outline-variant/10 hover:bg-surface-container-low/20 transition-colors">
                    <td class="p-4 font-bold text-on-surface bg-surface-container-low/20">${state.index}</td>
                    ${actionCells}
                    ${gotoCells}
                </tr>
            `;
        }).join('');

        const conflictSummary = automaton.conflicts.length === 0
            ? '<div class="mb-4 text-xs font-mono text-outline">No LR(0) shift/reduce or reduce/reduce conflicts were detected.</div>'
            : `<div class="mb-4 text-xs font-mono text-error">${automaton.conflicts.map(conflict => `<div>${escapeHtml(conflict)}</div>`).join('')}</div>`;

        tableEl.innerHTML = `
            ${conflictSummary}
            <table class="w-full text-left border-collapse">
                <thead>
                    <tr class="bg-surface-container-low/50">
                        <th class="p-4 border-b-2 border-outline-variant/30 text-[10px] uppercase label-text font-bold text-on-surface" rowspan="2">State</th>
                        <th class="p-4 border-b-2 border-outline-variant/30 text-[10px] uppercase label-text font-bold text-center border-l border-outline-variant/20" colspan="${automaton.terminals.length}">ACTION</th>
                        <th class="p-4 border-b-2 border-outline-variant/30 text-[10px] uppercase label-text font-bold text-center border-l border-outline-variant/20" colspan="${automaton.gotoSymbols.length}">GOTO</th>
                    </tr>
                    <tr class="bg-surface-container-low/30">
                        ${terminalHeaders}
                        ${gotoHeaders}
                    </tr>
                </thead>
                <tbody class="text-xs font-mono">
                    ${rows}
                </tbody>
            </table>
        `;
    }
}

async function runAnalysis(button, inputId, processor, renderer, label) {
    const grammarInput = document.getElementById(inputId);
    if (!grammarInput || !grammarInput.value.trim()) {
        return;
    }

    const originalLabel = button.textContent;
    button.disabled = true;
    button.classList.add('opacity-70', 'cursor-not-allowed');
    button.textContent = label;

    try {
        const result = await processor(grammarInput.value);
        renderer(result);
    } catch (error) {
        console.error(error);
        alert(error.message || 'An unexpected error occurred while processing the grammar.');
    } finally {
        button.disabled = false;
        button.classList.remove('opacity-70', 'cursor-not-allowed');
        button.textContent = originalLabel;
    }
}

document.addEventListener('DOMContentLoaded', () => {
    const navLinks = document.querySelectorAll('.nav-link, .nav-btn');
    const views = document.querySelectorAll('.view-section');
    const logoBtn = document.getElementById('logo-btn');

    function switchView(targetId) {
        views.forEach(view => {
            view.classList.remove('active');
            view.classList.add('hidden');
        });

        const targetView = document.getElementById(targetId);
        if (targetView) {
            targetView.classList.add('active');
            targetView.classList.remove('hidden');
        }

        document.querySelectorAll('.nav-link').forEach(link => {
            if (link.dataset.target === targetId) {
                link.classList.add('active');
                link.classList.remove('text-[#717c82]');
            } else {
                link.classList.remove('active');
                link.classList.add('text-[#717c82]');
                link.classList.remove('text-[#426086]', 'border-[#426086]');
            }
        });

        window.scrollTo(0, 0);
    }

    navLinks.forEach(link => {
        link.addEventListener('click', event => {
            event.preventDefault();
            const targetId = link.dataset.target;
            if (targetId) {
                switchView(targetId);
            }
        });
    });

    if (logoBtn) {
        logoBtn.addEventListener('click', () => {
            switchView('home-view');
        });
    }

    switchView('home-view');

    // License Pop-up Logic
    const licenseBtn = document.getElementById('license-btn');
    const licensePopup = document.getElementById('license-popup');
    const closeLicense = document.getElementById('close-license');
    let licenseTimeout;

    if (licenseBtn && licensePopup) {
        licenseBtn.addEventListener('click', (e) => {
            e.preventDefault();
            licensePopup.classList.remove('hidden');
            
            if (licenseTimeout) clearTimeout(licenseTimeout);
            
            licenseTimeout = setTimeout(() => {
                licensePopup.classList.add('hidden');
            }, 10000);
        });
    }

    if (closeLicense && licensePopup) {
        closeLicense.addEventListener('click', () => {
            licensePopup.classList.add('hidden');
            if (licenseTimeout) clearTimeout(licenseTimeout);
        });
    }

    // Virtual Keyboard Logic
    const keyboardToggle = document.getElementById('keyboard-toggle');
    const virtualKeyboard = document.getElementById('virtual-keyboard');
    const closeKeyboard = document.getElementById('close-keyboard');
    const kbKeys = document.querySelectorAll('.kb-key');
    let lastFocusedInput = document.getElementById('ll1-grammar-input');

    // Track focused input
    const inputs = ['ll1-grammar-input', 'lr0-grammar-input'];
    inputs.forEach(id => {
        const el = document.getElementById(id);
        if (el) {
            el.addEventListener('focus', () => {
                lastFocusedInput = el;
            });
        }
    });

    if (keyboardToggle && virtualKeyboard) {
        keyboardToggle.addEventListener('click', () => {
            virtualKeyboard.classList.toggle('hidden');
        });
    }

    if (closeKeyboard && virtualKeyboard) {
        closeKeyboard.addEventListener('click', () => {
            virtualKeyboard.classList.add('hidden');
        });
    }

    kbKeys.forEach(key => {
        key.addEventListener('click', () => {
            if (!lastFocusedInput) return;

            const char = key.dataset.char;
            const start = lastFocusedInput.selectionStart;
            const end = lastFocusedInput.selectionEnd;
            const text = lastFocusedInput.value;
            
            lastFocusedInput.value = text.substring(0, start) + char + text.substring(end);
            
            // Move cursor after the inserted character
            const newPos = start + char.length;
            lastFocusedInput.setSelectionRange(newPos, newPos);
            lastFocusedInput.focus();
        });
    });

    const ll1ProcessBtn = document.getElementById('ll1-process-btn');
    const lr0ProcessBtn = document.getElementById('lr0-process-btn');
    const ll1ClearBtn = document.getElementById('ll1-clear-btn');
    const lr0ClearBtn = document.getElementById('lr0-clear-btn');

    if (ll1ProcessBtn) {
        ll1ProcessBtn.addEventListener('click', () => {
            runAnalysis(ll1ProcessBtn, 'll1-grammar-input', callWasmProcessLL1, renderLL1, 'Processing...');
        });
    }

    if (lr0ProcessBtn) {
        lr0ProcessBtn.addEventListener('click', () => {
            runAnalysis(lr0ProcessBtn, 'lr0-grammar-input', callWasmProcessLR0, renderLR0, 'Analyzing...');
        });
    }

    if (ll1ClearBtn) {
        ll1ClearBtn.addEventListener('click', () => {
            document.getElementById('ll1-grammar-input').value = '';
        });
    }

    if (lr0ClearBtn) {
        lr0ClearBtn.addEventListener('click', () => {
            document.getElementById('lr0-grammar-input').value = '';
        });
    }

    loadWasmModule().catch(error => {
        console.error(error);
    });
});
