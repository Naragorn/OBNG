export const meta = {
  name: 're-slice',
  description: 'Map, refute, rebuild and review one slice of Oblivion.exe functions',
  phases: [
    { title: 'Map', detail: 'one worker per function reads the Ghidra export; all finish before anyone refutes' },
    { title: 'Refute', detail: 'three skeptics per function, distinct lenses; core refuted -> fix and re-refute, overstated -> demote' },
    { title: 'Rebuild', detail: 'tests first, pure logic, confirmed functions only, never the map-only ones' },
    { title: 'Review', detail: 'two adversarial reviewers per rebuild; one bounded fix round' },
    { title: 'Build', detail: 'clang -m32 build and ctest' },
  ],
}

// args: { slice, root, subsystem, sharedHeaders: [..],
//         functions: [{address, name, known, questions, mapOnly?, map?, emulatorGate?}],
//         stages?: { <stage>: {model, effort} } overriding STAGES below }
// A function with `map` (the MAP_SCHEMA object of a mapping that already held
// in an earlier run) skips the map and refute stages and goes to the rebuild.
// With `emulatorGate` the rebuild also authors the layer-1 emulator case and
// asserts the original's results in its tests (docs/re-workflow.md).
const slice = args.slice
const root = args.root
const subsystem = args.subsystem
const sharedHeaders = args.sharedHeaders || []

// Model and effort per stage. The money sits where a mistake costs the most
// downstream work: a wrong map costs three refuters and a fix round, a weak
// instruction-fidelity refuter lets a plausible wrong reading through to the
// rebuild. The consistency lens and the tests review are search- and
// checklist-shaped and run on a different model on purpose, so the skeptics
// do not share one blind spot. The build is mechanical. See docs/re-workflow.md.
const STAGES = Object.assign({
  map:               { model: 'opus',   effort: 'high' },
  refuteFidelity:    { model: 'opus',   effort: 'high' },
  refuteConsistency: { model: 'sonnet', effort: 'medium' },
  refuteBehaviour:   { model: 'opus',   effort: 'medium' },
  fix:               { model: 'opus',   effort: 'high' },
  demote:            { model: 'opus',   effort: 'medium' },
  rebuild:           { model: 'opus',   effort: 'high' },
  reviewCorrectness: { model: 'opus',   effort: 'high' },
  reviewTests:       { model: 'sonnet', effort: 'medium' },
  reviewFix:         { model: 'opus',   effort: 'high' },
  build:             { model: 'haiku',  effort: 'low' },
  buildFix:          { model: 'opus',   effort: 'high' },
}, args.stages || {})
const opts = (stage, label, phase, schema) => ({ label, phase, schema, ...STAGES[stage] })

const LENS_STAGE = { fidelity: 'refuteFidelity', consistency: 'refuteConsistency', behaviour: 'refuteBehaviour' }
const REVIEW_STAGE = { correctness: 'reviewCorrectness', tests: 'reviewTests' }

const exportDir = (f) => `${root}/re/export/${slice}/${f.address}`
const mdPath = (f) => `${root}/re/functions/${f.address}.md`
// A file-safe stem from a proposed name: "LoadingMenu::Tick" -> "LoadingMenu_Tick".
const stem = (name, address) => {
  const s = String(name || '').replace(/::/g, '_').replace(/[^A-Za-z0-9_]/g, '')
  return s || `Fn_${address.replace(/^0x/, '')}`
}

const RULES = `Rules that bind you (from ${root}/AGENTS.md):
- Every claim carries a mark: read (seen in the instructions, with the address), derived (follows from read facts, say which), unverified. A claim without a mark is not a finding.
- Ghidra pseudocode is a reading aid, never the proof: when pseudo.c and asm.s disagree, the instructions win and you say so.
- Keep the address next to every name. Names are proposals until the refutation held.
- The export under ${root}/re/export/${slice}/<address>/ is your binary evidence: meta.json (identity, callers, callees, strings, data refs), asm.s (every instruction with its bytes), pseudo.c, data.json (the bytes and typed readings of every referenced data address, and every computed jump's table with its case targets), bytes.bin (the raw body). Do NOT call REA, Ghidra or any MCP analysis tool. Do not run the game.
- English only in files you write. Clean Code. No workarounds.`

const MAP_SCHEMA = {
  type: 'object',
  properties: {
    address: { type: 'string' },
    proposedName: { type: 'string', description: 'An identifier only: Class::Name or Name. No address, no words like proposal.' },
    purpose: { type: 'string' },
    callingConvention: { type: 'string' },
    thisType: { type: 'string' },
    parameters: { type: 'array', items: { type: 'object', properties: {
      index: { type: 'integer' }, type: { type: 'string' }, name: { type: 'string' }, evidence: { type: 'string' } },
      required: ['index', 'type', 'name', 'evidence'] } },
    returns: { type: 'string' },
    structAccesses: { type: 'array', items: { type: 'object', properties: {
      base: { type: 'string' }, offset: { type: 'string' }, size: { type: 'integer' },
      field: { type: 'string' }, evidence: { type: 'string' }, mark: { type: 'string' } },
      required: ['base', 'offset', 'size', 'field', 'evidence', 'mark'] } },
    branches: { type: 'array', items: { type: 'object', properties: {
      name: { type: 'string' }, condition: { type: 'string' }, at: { type: 'string' } },
      required: ['name', 'condition', 'at'] } },
    claims: { type: 'array', items: { type: 'object', properties: {
      text: { type: 'string' }, mark: { type: 'string', enum: ['read', 'derived', 'unverified'] }, evidence: { type: 'string' } },
      required: ['text', 'mark', 'evidence'] } },
    openQuestions: { type: 'array', items: { type: 'string' } },
    layoutRequests: { type: 'array', items: { type: 'string' } },
    mdPath: { type: 'string' },
  },
  required: ['address', 'proposedName', 'purpose', 'callingConvention', 'parameters', 'returns',
    'structAccesses', 'branches', 'claims', 'openQuestions', 'layoutRequests', 'mdPath'],
}

// A refutation separates what sinks a mapping from what only needs trimming:
// coreRefuted - the convention, a parameter, a struct access, the control flow
//               or a read-claim is wrong against the instructions or against a
//               settled source (xOBSE, OBVR's GameAddresses.h, knowledge/);
// overstated  - a derived claim says more than the evidence carries; it gets
//               demoted, the function is not sunk by it;
// needsDecision - two files of this slice name one offset differently, or a
//               value is missing from the export; the architect decides.
const VERDICT_SCHEMA = {
  type: 'object',
  properties: {
    coreRefuted: { type: 'boolean' },
    verdicts: { type: 'array', items: { type: 'object', properties: {
      claim: { type: 'string' }, verdict: { type: 'string', enum: ['holds', 'core-refuted', 'overstated', 'unverifiable'] }, evidence: { type: 'string' } },
      required: ['claim', 'verdict', 'evidence'] } },
    overstated: { type: 'array', items: { type: 'object', properties: {
      claim: { type: 'string' }, correction: { type: 'string' } }, required: ['claim', 'correction'] } },
    needsDecision: { type: 'array', items: { type: 'string' } },
    summary: { type: 'string' },
  },
  required: ['coreRefuted', 'verdicts', 'overstated', 'needsDecision', 'summary'],
}

const REBUILD_SCHEMA = {
  type: 'object',
  properties: {
    files: { type: 'array', items: { type: 'string' } },
    flows: { type: 'array', items: { type: 'object', properties: {
      branch: { type: 'string' }, test: { type: 'string' } }, required: ['branch', 'test'] } },
    layoutRequests: { type: 'array', items: { type: 'string' } },
    ran: { type: 'string', description: 'The exact build and test commands run and what they printed, in short.' },
    notes: { type: 'string' },
  },
  required: ['files', 'flows', 'layoutRequests', 'ran', 'notes'],
}

const REVIEW_SCHEMA = {
  type: 'object',
  properties: {
    approved: { type: 'boolean' },
    issues: { type: 'array', items: { type: 'object', properties: {
      file: { type: 'string' }, line: { type: 'integer' }, severity: { type: 'string', enum: ['blocker', 'major', 'minor'] }, text: { type: 'string' } },
      required: ['file', 'severity', 'text'] } },
    summary: { type: 'string' },
  },
  required: ['approved', 'issues', 'summary'],
}

const FIX_SCHEMA = {
  type: 'object',
  properties: {
    changed: { type: 'array', items: { type: 'string' } },
    resolved: { type: 'array', items: { type: 'string' } },
    notResolved: { type: 'array', items: { type: 'object', properties: {
      issue: { type: 'string' }, why: { type: 'string' } }, required: ['issue', 'why'] } },
    ran: { type: 'string' },
  },
  required: ['changed', 'resolved', 'notResolved', 'ran'],
}

const BUILD_SCHEMA = {
  type: 'object',
  properties: {
    ok: { type: 'boolean' },
    configured: { type: 'boolean' },
    built: { type: 'boolean' },
    testsRun: { type: 'integer' },
    testsFailed: { type: 'integer' },
    output: { type: 'string' },
  },
  required: ['ok', 'configured', 'built', 'testsRun', 'testsFailed', 'output'],
}

const knownText = (f) => `What is already known about ${f.address} (${f.name}):\n${f.known || '(nothing beyond the seed name)'}\nQuestions this slice should answer:\n${(f.questions || []).map(q => '- ' + q).join('\n') || '(none)'}`

function mapPrompt(f) {
  return `You map one function of Oblivion.exe 1.2.0.416 (ImageBase 0x00400000, 32-bit x86, MSVC 2003-era) for OBNG, slice "${slice}".
Function: ${f.address}, seed name "${f.name}".
Read, in this order: ${exportDir(f)}/meta.json, ${exportDir(f)}/asm.s, ${exportDir(f)}/data.json, ${exportDir(f)}/pseudo.c, then ${root}/re/slices/${slice}/manifest.json. For cross-reference you may read ${root}/re/symbols/*.csv, ${root}/knowledge/*.md, ${root}/../OBVR/src/game/GameAddresses.h and the other functions' exports under ${root}/re/export/${slice}/. Other functions' files under ${root}/re/functions/ may still be in progress in this phase; cite them only as "in progress".
${knownText(f)}
${f.mapOnly ? 'This function is MAP ONLY: it will not be rebuilt in this run. Map it at the level of which subsystems it touches, in what order, what reads and writes the clock, and name the smaller callees worth their own slice.' : ''}
Write ${mdPath(f)} with these sections: Identity (address, seed name, proposed name), Purpose, Calling convention and parameters (which register holds this, how the stack is cleaned, ret N), Struct accesses (table: base, offset, size, proposed field, evidence, mark), Control flow (every branch with its condition and address - the rebuild turns each into a test flow), Claims (each marked), Open questions, Layout requests (fields a shared header would need).
Keep derived claims to what the instructions carry: say "when X, Y" rather than "always Y". A constant's value comes from data.json, not from a guess; if data.json has no entry for it, mark the dependent claim unverified.
${RULES}
Return the structured object; proposedName is an identifier only (Class::Name or Name); mdPath is the file you wrote.`
}

const LENSES = {
  fidelity: 'instruction fidelity: check every read-claim against asm.s - calling convention and stack cleanup (ret N, who pops), register use, signed/unsigned comparisons, float versus integer arithmetic and precision, the actual immediates and memory operands, loop bounds, constants against data.json. pseudo.c is not evidence for this lens.',
  consistency: `consistency: check the proposed offsets, field names, this-type and function name against ${root}/re/symbols/xobse.csv, ${root}/re/symbols/obvr.csv, ${root}/knowledge/*.md, ${root}/../OBVR/src/game/GameAddresses.h and the other functions of this slice (their exports under ${root}/re/export/${slice}/ and their finished files under ${root}/re/functions/ - the map phase is complete, those files are final for this run). A field named against a settled source (xOBSE, GameAddresses.h, knowledge/) is core-refuted. Two files of this slice naming one offset differently is needsDecision, not a refutation of this function.`,
  behaviour: 'behaviour: check whether the purpose claim is carried by the callers, callees, referenced strings, constants (data.json) and data references in meta.json and asm.s, or whether it rests on the seed name alone. Ask what else the code could be and why that reading is excluded.',
}

function refutePrompt(f, lens, map) {
  return `You are a skeptic. Try to REFUTE the mapping of function ${f.address} ("${map.proposedName}") in Oblivion.exe 1.2.0.416, slice "${slice}".
Your lens - ${LENSES[lens]}
Read ${mdPath(f)} and the export under ${exportDir(f)}/ (meta.json, asm.s, data.json, pseudo.c).
The mapping's claims:
${map.claims.map(c => `- [${c.mark}] ${c.text} (evidence: ${c.evidence})`).join('\n')}
Struct accesses: ${JSON.stringify(map.structAccesses)}
Parameters: ${JSON.stringify(map.parameters)}; convention ${map.callingConvention}; returns ${map.returns}

Judge each claim marked read or derived, with the instruction address, data.json entry or file that decides it:
- core-refuted: the convention, a parameter, a struct access, a branch, or a read-claim is wrong against the instructions or against a settled source. This sinks the mapping.
- overstated: a derived claim says more than the evidence carries (an "always" that is a "when", an "exactly" that is rounded). Give the correction. This does not sink the mapping.
- unverifiable: the evidence to decide it is not in the export. Say what would settle it. This does not sink the mapping.
- holds.
coreRefuted=true only when at least one verdict is core-refuted. A claim marked unverified is never a refutation. Cross-file naming differences inside this slice go into needsDecision.
${RULES}`
}

function fixPrompt(f, map, refutations) {
  return `The mapping of ${f.address} ("${map.proposedName}", slice "${slice}") was refuted on its core. Rewrite ${mdPath(f)} so that every claim survives, or demote it to unverified with the open question written down.
Refutations:
${refutations.map((r, i) => `--- skeptic ${i + 1} (${r.lens}): ${r.summary}\n${r.verdicts.filter(v => v.verdict !== 'holds').map(v => `  * ${v.verdict}: ${v.claim} -- ${v.evidence}`).join('\n')}`).join('\n')}
Evidence: ${exportDir(f)}/ (meta.json, asm.s, data.json, pseudo.c). Do not argue with a skeptic who cites an instruction; read it.
${RULES}
Return the corrected structured object (proposedName an identifier only).`
}

function demotePrompt(f, map, refutations) {
  const overstated = refutations.flatMap(r => r.overstated.map(o => `- ${o.claim}\n  correction: ${o.correction}`))
  const unverifiable = refutations.flatMap(r => r.verdicts.filter(v => v.verdict === 'unverifiable').map(v => `- ${v.claim} -- ${v.evidence}`))
  return `The mapping of ${f.address} ("${map.proposedName}", slice "${slice}") held on its core. Trim it: rewrite the overstated derived claims in ${mdPath(f)} to the corrections below, and mark the unverifiable ones unverified with what would settle them. Change nothing else.
Overstated:
${overstated.join('\n') || '(none)'}
Unverifiable:
${unverifiable.join('\n') || '(none)'}
${RULES}
Return the corrected structured object (proposedName an identifier only).`
}

function rebuildPrompt(f, map) {
  const name = stem(map.proposedName, f.address)
  return `Rebuild function ${f.address} ("${map.proposedName}") of Oblivion.exe 1.2.0.416 in modern C++ for OBNG, subsystem "${subsystem}", slice "${slice}".
Read ${mdPath(f)}, the export under ${exportDir(f)}/ (asm.s, data.json, pseudo.c), ${root}/AGENTS.md, the shared headers ${sharedHeaders.map(h => root + '/' + h).join(', ') || '(none)'}, ${root}/tests/Check.h, ${root}/tests/CMakeLists.txt and one existing test under ${root}/tests/ for the style.
Files, exactly these names: ${root}/src/engine/${subsystem}/${name}Logic.h (the decision logic as pure functions over plain values), ${root}/src/engine/${subsystem}/${name}.h (the engine-facing shape: layout structs with static_assert on sizes and offsets, the original's calling convention; includes the shared headers rather than redefining their types), ${root}/tests/${name}Test.cpp.
Tests first: one test flow per branch of the mapped control flow (${map.branches.map(b => b.name).join('; ')}), refusal and fallback paths included; then the code until the tests pass. Integer widths, signedness, float precision and clamps as the instructions have them; the game runs its x87 at 24-bit precision control, rounding to nearest (measured in the game, knowledge/frame-clock.md): where the original's result depends on that, model the rounding exactly where the instructions round (say so in a comment) and test it.${f.emulatorGate ? `
Emulator gate (layer 1, docs/re-workflow.md "the gate outside the workflow"): also write ${root}/tools/difftest/cases/${f.address}.json - cases that drive every branch above with concrete memory and register inputs; callees as traced stubs (read the docstring of ${root}/tools/difftest/emulate.py: fixed-return, per-call-return and raw-code stubs; an external DLL callee becomes a raw-code stub that writes a chosen output), an "fpcw" of 0x007F (24-bit, nearest) where x87 is involved; run \`. ~/obvr-venv/bin/activate && python3 ${root}/tools/difftest/emulate.py ${exportDir(f)} ${root}/tools/difftest/cases/${f.address}.json --json > ${root}/tools/difftest/expected/${f.address}.json\` (every case must return cleanly: "returned" true, stack_delta 0, no errors), and transcribe the original's results (EAX, the recorded writes with their bit patterns, the traced calls with their arguments) as literal expectations into ${root}/tests/${name}Test.cpp, so the rebuild is checked against what the ORIGINAL produced, not against your reading. Existing examples: ${root}/tools/difftest/cases/0x0047D170.json, 0x004E1580.json and the tests that cite them. A difference between the original's result and the rebuild's is a finding to report, never something to paper over in the test.` : ''} A test file is picked up by CMake's glob; do not edit tests/CMakeLists.txt. Do NOT edit the shared headers; put missing fields into layoutRequests. Build and run your test yourself: cmake -B ${root}/build-tests-${f.address} ${root}/tests -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32 -G Ninja, then cmake --build, then ctest --test-dir ${root}/build-tests-${f.address} -R ${name}Test --output-on-failure. Report in "ran" what you ran and what it showed; never claim a test you did not run.
${RULES}`
}

const REVIEW_LENSES = {
  correctness: (f, map, rebuild) => `Review the rebuild of ${f.address} ("${map.proposedName}") for CORRECTNESS against the binary: the files ${rebuild.files.join(', ')} against ${exportDir(f)}/asm.s, ${exportDir(f)}/data.json, ${exportDir(f)}/pseudo.c and ${mdPath(f)}. Same branches, same arithmetic (integer widths, signedness, float precision, clamps), same edge handling, same side effects on the layout structs. A deviation that is not documented as intentional is a blocker. Try to find an input on which the rebuild and the original differ.`,
  tests: (f, map, rebuild) => `Review the rebuild of ${f.address} ("${map.proposedName}") for TESTS AND CODE: the files ${rebuild.files.join(', ')}. Every mapped branch (${map.branches.map(b => b.name).join('; ')}) must be exercised by a test that would fail if the branch were wrong; refusal and fallback paths included; the logic must be testable without the engine; the repository's style (${root}/AGENTS.md, ${root}/tests/Check.h) must be kept; no dead code, no workaround, nothing claimed that is not run. Run the test yourself (cmake -B ${root}/build-tests-review-${f.address} ${root}/tests -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32 -G Ninja; cmake --build; ctest --output-on-failure) and report what you saw.`,
}

function reviewPrompt(f, map, rebuild, lens) {
  return `${REVIEW_LENSES[lens](f, map, rebuild)}
approved=false when any blocker or major issue exists. Cite file and line for every issue.
${RULES}`
}

function reviewFixPrompt(f, map, rebuild, reviews) {
  return `The rebuild of ${f.address} ("${map.proposedName}") was rejected. Fix ONLY the issues listed below, each at its cause, in the files ${rebuild.files.join(', ')}; no refactoring beyond them, no new files, no edits to shared headers. Keep the tests green: build and run them once as the rebuild stage did (cmake -B ${root}/build-tests-${f.address} ${root}/tests -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32 -G Ninja; cmake --build; ctest --test-dir ${root}/build-tests-${f.address} --output-on-failure). An issue you cannot resolve at its cause goes into notResolved with the reason; do not paper over it.
Issues:
${reviews.flatMap(r => r.issues.map(i => `- [${i.severity}] ${i.file}:${i.line || '?'} ${i.text}`)).join('\n')}
${RULES}`
}

function buildPrompt() {
  return `Build and test OBNG's rebuilt code, all of it, from a clean directory:
cd ${root} && rm -rf build-tests && cmake -B build-tests tests -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32 -G Ninja && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
Report exactly what happened; do not fix anything. output: the last 60 lines that matter (errors, the ctest summary).`
}

function buildFixPrompt(build, files) {
  return `The OBNG test build failed. Fix the cause in the rebuilt files (${files.join(', ')}) or their tests - never by weakening a test - and rebuild as the build stage did (cd ${root} && rm -rf build-tests && cmake -B build-tests tests -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32 -G Ninja && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure). Report what was wrong, why, what you changed and what the rebuild showed.
Build output:
${build.output}
${RULES}`
}

// ---- the run ----

log(`slice ${slice}: ${args.functions.length} functions (${args.functions.filter(f => f.mapOnly).length} map-only, ${args.functions.filter(f => f.map).length} premapped); stages ${JSON.stringify(STAGES)}`)

// Stage 1: map. A barrier on purpose: the consistency skeptics read the other
// functions' files, which must be final before anyone cites them.
const maps = await parallel(args.functions.map(f => () => f.map
  ? Promise.resolve(f.map)
  : agent(mapPrompt(f), opts('map', `map ${f.address}`, 'Map', MAP_SCHEMA))))
const mapped = args.functions.map((f, i) => maps[i] ? { f, map: maps[i] } : { f, state: 'map-failed' })
log(`map: ${mapped.filter(e => e.map).length} of ${args.functions.length} mapped`)

const entries = await pipeline(
  mapped,
  // Stage 2: refute. Core refuted by two of three -> fix, re-refute by two;
  // otherwise overstated/unverifiable claims are demoted in one pass.
  async (e) => {
    if (!e.map) return e
    const { f } = e
    if (f.map) return { ...e, state: 'confirmed', refutations: [], decisions: [], premapped: true }
    const round1 = (await parallel(Object.keys(LENSES).map(lens => () =>
      agent(refutePrompt(f, lens, e.map), opts(LENS_STAGE[lens], `refute:${lens} ${f.address}`, 'Refute', VERDICT_SCHEMA))
        .then(v => v && { lens, ...v })))).filter(Boolean)
    const decisions = round1.flatMap(v => v.needsDecision)
    const coreRefuted1 = round1.filter(v => v.coreRefuted)

    if (coreRefuted1.length >= 2) {
      const fixed = await agent(fixPrompt(f, e.map, coreRefuted1), opts('fix', `fix ${f.address}`, 'Refute', MAP_SCHEMA))
      if (!fixed) return { ...e, state: 'disputed', refutations: [round1], decisions }
      const round2 = (await parallel(['fidelity', 'consistency'].map(lens => () =>
        agent(refutePrompt(f, lens, fixed), opts(LENS_STAGE[lens], `re-refute:${lens} ${f.address}`, 'Refute', VERDICT_SCHEMA))
          .then(v => v && { lens, ...v })))).filter(Boolean)
      const state = round2.some(v => v.coreRefuted) ? 'disputed' : 'confirmed'
      return { ...e, map: fixed, state, refutations: [round1, round2], decisions: decisions.concat(round2.flatMap(v => v.needsDecision)) }
    }

    const needsTrim = round1.some(v => v.overstated.length || v.verdicts.some(x => x.verdict === 'unverifiable'))
    if (needsTrim) {
      const trimmed = await agent(demotePrompt(f, e.map, round1), opts('demote', `demote ${f.address}`, 'Refute', MAP_SCHEMA))
      if (trimmed) return { ...e, map: trimmed, state: 'confirmed', refutations: [round1], decisions, trimmed: true }
    }
    return { ...e, state: 'confirmed', refutations: [round1], decisions }
  },
  // Stage 3: rebuild (confirmed, not map-only)
  async (e) => {
    if (e.state !== 'confirmed') return e
    if (e.f.mapOnly) return { ...e, state: 'mapped' }
    const rebuild = await agent(rebuildPrompt(e.f, e.map), opts('rebuild', `rebuild ${e.f.address}`, 'Rebuild', REBUILD_SCHEMA))
    if (!rebuild) return { ...e, state: 'rebuild-failed' }
    return { ...e, rebuild }
  },
  // Stage 4: review, one bounded fix round, re-review by the rejecting lenses
  async (e) => {
    if (!e.rebuild) return e
    const { f, map, rebuild } = e
    const round1 = (await parallel(Object.keys(REVIEW_LENSES).map(lens => () =>
      agent(reviewPrompt(f, map, rebuild, lens), opts(REVIEW_STAGE[lens], `review:${lens} ${f.address}`, 'Review', REVIEW_SCHEMA))
        .then(r => r && { lens, ...r })))).filter(Boolean)
    const rejected = round1.filter(r => !r.approved)
    if (!rejected.length) return { ...e, state: 'reconstructed', reviews: [round1] }

    const fix = await agent(reviewFixPrompt(f, map, rebuild, rejected), opts('reviewFix', `review-fix ${f.address}`, 'Review', FIX_SCHEMA))
    const round2 = (await parallel(rejected.map(r => () =>
      agent(reviewPrompt(f, map, rebuild, r.lens), opts(REVIEW_STAGE[r.lens], `re-review:${r.lens} ${f.address}`, 'Review', REVIEW_SCHEMA))
        .then(x => x && { lens: r.lens, ...x })))).filter(Boolean)
    const state = round2.length && round2.every(r => r.approved) ? 'reconstructed' : 'review-failed'
    return { ...e, state, reviews: [round1, round2], reviewFix: fix }
  },
)

// Stage 5: one build over everything (a real barrier: the build needs all files)
const rebuilt = entries.filter(Boolean).filter(e => e.rebuild)
let build = null
if (rebuilt.length) {
  build = await agent(buildPrompt(), opts('build', 'build+ctest', 'Build', BUILD_SCHEMA))
  if (build && !build.ok) {
    const files = rebuilt.flatMap(e => e.rebuild.files)
    await agent(buildFixPrompt(build, files), opts('buildFix', 'build-fix', 'Build', FIX_SCHEMA))
    build = await agent(buildPrompt(), opts('build', 'build+ctest (after fix)', 'Build', BUILD_SCHEMA))
  }
} else {
  log('nothing was rebuilt; build stage skipped')
}

const ledger = entries.filter(Boolean).map(e => ({
  address: e.f.address,
  seedName: e.f.name,
  proposedName: e.map ? e.map.proposedName : null,
  state: e.state || 'unknown',
  trimmed: !!e.trimmed,
  refutations: (e.refutations || []).map(r => r.map(v => ({ lens: v.lens, coreRefuted: v.coreRefuted, overstated: v.overstated.length, summary: v.summary }))),
  decisions: e.decisions || [],
  rebuildFiles: e.rebuild ? e.rebuild.files : [],
  flows: e.rebuild ? e.rebuild.flows : [],
  layoutRequests: [...(e.map ? e.map.layoutRequests : []), ...(e.rebuild ? e.rebuild.layoutRequests : [])],
  openQuestions: e.map ? e.map.openQuestions : [],
  reviews: (e.reviews || []).map(r => r.map(x => ({ lens: x.lens, approved: x.approved, summary: x.summary }))),
  reviewFix: e.reviewFix ? { resolved: e.reviewFix.resolved, notResolved: e.reviewFix.notResolved } : null,
}))

const counts = {}
for (const l of ledger) counts[l.state] = (counts[l.state] || 0) + 1
log(`slice ${slice} done: ${JSON.stringify(counts)}; build ${build ? (build.ok ? 'green' : 'RED') : 'skipped'}`)

return { slice, counts, build, ledger }
