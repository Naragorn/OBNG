export const meta = {
  name: 're-slice',
  description: 'Map, refute, rebuild and review one slice of Oblivion.exe functions',
  phases: [
    { title: 'Map', detail: 'one worker per function reads the Ghidra export' },
    { title: 'Refute', detail: 'three skeptics per function, distinct lenses; one fix round' },
    { title: 'Rebuild', detail: 'tests first, pure logic, confirmed functions only' },
    { title: 'Review', detail: 'two adversarial reviewers per rebuild; one fix round' },
    { title: 'Build', detail: 'clang -m32 build and ctest' },
  ],
}

// args: { slice, root, subsystem, sharedHeaders: [..], functions: [{address, name, known, questions}],
//         stages?: { <stage>: {model, effort} } overriding STAGES below }
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
  rebuild:           { model: 'opus',   effort: 'high' },
  reviewCorrectness: { model: 'opus',   effort: 'high' },
  reviewTests:       { model: 'sonnet', effort: 'medium' },
  build:             { model: 'haiku',  effort: 'low' },
  buildFix:          { model: 'opus',   effort: 'high' },
}, args.stages || {})
const opts = (stage, label, phase, schema) => ({ label, phase, schema, ...STAGES[stage] })

const LENS_STAGE = { fidelity: 'refuteFidelity', consistency: 'refuteConsistency', behaviour: 'refuteBehaviour' }
const REVIEW_STAGE = { correctness: 'reviewCorrectness', tests: 'reviewTests' }

const exportDir = (f) => `${root}/re/export/${slice}/${f.address}`
const mdPath = (f) => `${root}/re/functions/${f.address}.md`

const RULES = `Rules that bind you (from ${root}/AGENTS.md):
- Every claim carries a mark: read (seen in the instructions, with the address), derived (follows from read facts, say which), unverified. A claim without a mark is not a finding.
- Ghidra pseudocode is a reading aid, never the proof: when pseudo.c and asm.s disagree, the instructions win and you say so.
- Keep the address next to every name. Names are proposals until the refutation held.
- Do NOT call REA, Ghidra or any MCP analysis tool; the export under ${root}/re/export/${slice}/ is your binary evidence. Do not run the game.
- English only in files you write. Clean Code. No workarounds.`

const MAP_SCHEMA = {
  type: 'object',
  properties: {
    address: { type: 'string' },
    proposedName: { type: 'string' },
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

const VERDICT_SCHEMA = {
  type: 'object',
  properties: {
    refuted: { type: 'boolean' },
    verdicts: { type: 'array', items: { type: 'object', properties: {
      claim: { type: 'string' }, verdict: { type: 'string', enum: ['holds', 'refuted', 'unverifiable'] }, evidence: { type: 'string' } },
      required: ['claim', 'verdict', 'evidence'] } },
    summary: { type: 'string' },
  },
  required: ['refuted', 'verdicts', 'summary'],
}

const REBUILD_SCHEMA = {
  type: 'object',
  properties: {
    files: { type: 'array', items: { type: 'string' } },
    flows: { type: 'array', items: { type: 'object', properties: {
      branch: { type: 'string' }, test: { type: 'string' } }, required: ['branch', 'test'] } },
    layoutRequests: { type: 'array', items: { type: 'string' } },
    notes: { type: 'string' },
  },
  required: ['files', 'flows', 'layoutRequests', 'notes'],
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
Read, in this order: ${exportDir(f)}/meta.json, ${exportDir(f)}/asm.s, ${exportDir(f)}/pseudo.c, then ${root}/re/slices/${slice}/manifest.json. For cross-reference you may read ${root}/re/symbols/*.csv, ${root}/knowledge/*.md, ${root}/../OBVR/src/game/GameAddresses.h and the other functions' exports under ${root}/re/export/${slice}/.
${knownText(f)}

Write ${mdPath(f)} with these sections: Identity (address, seed name, proposed name), Purpose, Calling convention and parameters (which register holds this, how the stack is cleaned, ret N), Struct accesses (table: base, offset, size, proposed field, evidence, mark), Control flow (every branch with its condition and address - the rebuild turns each into a test flow), Claims (each marked), Open questions, Layout requests (fields a shared header would need).
${RULES}
Return the structured object; mdPath is the file you wrote.`
}

const LENSES = {
  fidelity: 'instruction fidelity: check every read-claim against asm.s - calling convention and stack cleanup (ret N, who pops), register use, signed/unsigned comparisons, float versus integer arithmetic and precision, the actual immediates and memory operands, loop bounds. pseudo.c is not evidence for this lens.',
  consistency: `consistency: check the proposed offsets, field names, this-type and function name against ${root}/re/symbols/xobse.csv, ${root}/re/symbols/obvr.csv, ${root}/knowledge/*.md, ${root}/../OBVR/src/game/GameAddresses.h and the other functions of this slice under ${root}/re/export/${slice}/ and ${root}/re/functions/. A field named differently for the same offset elsewhere, or a size that contradicts a known layout, refutes.`,
  behaviour: 'behaviour: check whether the purpose claim is carried by the callers, callees, referenced strings, constants and data references in meta.json and asm.s, or whether it rests on the seed name alone. Ask what else the code could be and why that reading is excluded.',
}

function refutePrompt(f, lens, map) {
  return `You are a skeptic. Try to REFUTE the mapping of function ${f.address} ("${map.proposedName}") in Oblivion.exe 1.2.0.416, slice "${slice}".
Your lens - ${LENSES[lens]}
Read ${mdPath(f)} and the export under ${exportDir(f)}/ (meta.json, asm.s, pseudo.c).
The mapping's claims:
${map.claims.map(c => `- [${c.mark}] ${c.text} (evidence: ${c.evidence})`).join('\n')}
Struct accesses: ${JSON.stringify(map.structAccesses)}
Parameters: ${JSON.stringify(map.parameters)}; convention ${map.callingConvention}; returns ${map.returns}

Judge each claim marked read or derived: holds, refuted or unverifiable, with the instruction address or file that decides it. A claim marked unverified is not a refutation. refuted=true when at least one read- or derived-claim, a struct access, the convention or a parameter is wrong or unsupported by the instructions. If you cannot decide a read-claim from the evidence, count it as refuted and say what evidence would settle it.
${RULES}`
}

function fixPrompt(f, map, refutations) {
  return `The mapping of ${f.address} ("${map.proposedName}", slice "${slice}") was refuted. Rewrite ${mdPath(f)} so that every claim survives, or demote it to unverified with the open question written down.
Refutations:
${refutations.map((r, i) => `--- skeptic ${i + 1}: ${r.summary}\n${r.verdicts.filter(v => v.verdict !== 'holds').map(v => `  * ${v.verdict}: ${v.claim} -- ${v.evidence}`).join('\n')}`).join('\n')}
Evidence: ${exportDir(f)}/ (meta.json, asm.s, pseudo.c). Do not argue with a skeptic who cites an instruction; read it.
${RULES}
Return the corrected structured object.`
}

function rebuildPrompt(f, map) {
  return `Rebuild function ${f.address} ("${map.proposedName}") of Oblivion.exe 1.2.0.416 in modern C++ for OBNG, subsystem "${subsystem}", slice "${slice}".
Read ${mdPath(f)}, the export under ${exportDir(f)}/, ${root}/AGENTS.md, the shared headers ${sharedHeaders.map(h => root + '/' + h).join(', ') || '(none)'}, ${root}/tests/Check.h, ${root}/tests/CMakeLists.txt and one existing test under ${root}/tests/ for the style.
Tests first: write ${root}/tests/${map.proposedName.replace(/::/g, '_')}Test.cpp with one test flow per branch of the mapped control flow (${map.branches.map(b => b.name).join('; ')}), refusal and fallback paths included, then the code until the tests pass: the decision logic as pure functions over plain values in ${root}/src/engine/${subsystem}/<Name>Logic.h, the engine-facing shape (layout structs with static_assert on sizes and offsets, the original's calling convention) kept apart from the logic. A test file is picked up by CMake's glob; do not edit tests/CMakeLists.txt. Do NOT edit the shared headers; put missing fields into layoutRequests. Build and run your test yourself: cmake -B ${root}/build-tests-${f.address} ${root}/tests -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32 -G Ninja, then cmake --build, then ctest --test-dir ${root}/build-tests-${f.address} -R <your test> --output-on-failure. Report what you ran and what it showed; never claim a test you did not run.
${RULES}`
}

const REVIEW_LENSES = {
  correctness: (f, map, rebuild) => `Review the rebuild of ${f.address} ("${map.proposedName}") for CORRECTNESS against the binary: the files ${rebuild.files.join(', ')} against ${exportDir(f)}/asm.s, ${exportDir(f)}/pseudo.c and ${mdPath(f)}. Same branches, same arithmetic (integer widths, signedness, float precision, clamps), same edge handling, same side effects on the layout structs. A deviation that is not documented as intentional is a blocker. Try to find an input on which the rebuild and the original differ.`,
  tests: (f, map, rebuild) => `Review the rebuild of ${f.address} ("${map.proposedName}") for TESTS AND CODE: the files ${rebuild.files.join(', ')}. Every mapped branch (${map.branches.map(b => b.name).join('; ')}) must be exercised by a test that would fail if the branch were wrong; refusal and fallback paths included; the logic must be testable without the engine; the repository's style (${root}/AGENTS.md, ${root}/tests/Check.h) must be kept; no dead code, no workaround, nothing claimed that is not run. Run the test yourself (cmake -B ${root}/build-tests-review-${f.address} ${root}/tests -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32 -G Ninja; cmake --build; ctest --output-on-failure) and report what you saw.`,
}

function reviewPrompt(f, map, rebuild, lens) {
  return `${REVIEW_LENSES[lens](f, map, rebuild)}
approved=false when any blocker or major issue exists. Cite file and line for every issue.
${RULES}`
}

function reviewFixPrompt(f, map, rebuild, reviews) {
  return `The rebuild of ${f.address} ("${map.proposedName}") was rejected. Fix the files ${rebuild.files.join(', ')} so every issue below is resolved at its cause, keep the tests green (build and run them as the rebuild stage did, under ${root}/build-tests-${f.address}), and report what you changed and ran.
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

log(`slice ${slice}: ${args.functions.length} functions; stages ${JSON.stringify(STAGES)}`)

const entries = await pipeline(
  args.functions,
  // Stage 1: map
  async (f) => {
    const map = await agent(mapPrompt(f), opts('map', `map ${f.address}`, 'Map', MAP_SCHEMA))
    if (!map) return { f, state: 'map-failed' }
    return { f, map }
  },
  // Stage 2: refute, one fix round, re-refute
  async (e) => {
    if (!e.map) return e
    const { f } = e
    const round1 = (await parallel(Object.keys(LENSES).map(lens => () =>
      agent(refutePrompt(f, lens, e.map), opts(LENS_STAGE[lens], `refute:${lens} ${f.address}`, 'Refute', VERDICT_SCHEMA))
        .then(v => v && { lens, ...v })))).filter(Boolean)
    const refuted1 = round1.filter(v => v.refuted)
    if (refuted1.length < 2) return { ...e, state: 'confirmed', refutations: [round1] }

    const fixed = await agent(fixPrompt(f, e.map, refuted1), opts('fix', `fix ${f.address}`, 'Refute', MAP_SCHEMA))
    if (!fixed) return { ...e, state: 'disputed', refutations: [round1] }
    const round2 = (await parallel(['fidelity', 'consistency'].map(lens => () =>
      agent(refutePrompt(f, lens, fixed), opts(LENS_STAGE[lens], `re-refute:${lens} ${f.address}`, 'Refute', VERDICT_SCHEMA))
        .then(v => v && { lens, ...v })))).filter(Boolean)
    const state = round2.some(v => v.refuted) ? 'disputed' : 'confirmed'
    return { ...e, map: fixed, state, refutations: [round1, round2] }
  },
  // Stage 3: rebuild
  async (e) => {
    if (e.state !== 'confirmed') return e
    const rebuild = await agent(rebuildPrompt(e.f, e.map), opts('rebuild', `rebuild ${e.f.address}`, 'Rebuild', REBUILD_SCHEMA))
    if (!rebuild) return { ...e, state: 'rebuild-failed' }
    return { ...e, rebuild }
  },
  // Stage 4: review, one fix round
  async (e) => {
    if (!e.rebuild) return e
    const { f, map, rebuild } = e
    const round1 = (await parallel(Object.keys(REVIEW_LENSES).map(lens => () =>
      agent(reviewPrompt(f, map, rebuild, lens), opts(REVIEW_STAGE[lens], `review:${lens} ${f.address}`, 'Review', REVIEW_SCHEMA))
        .then(r => r && { lens, ...r })))).filter(Boolean)
    const rejected = round1.filter(r => !r.approved)
    if (!rejected.length) return { ...e, state: 'reconstructed', reviews: [round1] }

    await agent(reviewFixPrompt(f, map, rebuild, rejected), opts('fix', `review-fix ${f.address}`, 'Review'))
    const round2 = (await parallel(rejected.map(r => () =>
      agent(reviewPrompt(f, map, rebuild, r.lens), opts(REVIEW_STAGE[r.lens], `re-review:${r.lens} ${f.address}`, 'Review', REVIEW_SCHEMA))
        .then(x => x && { lens: r.lens, ...x })))).filter(Boolean)
    const state = round2.every(r => r.approved) ? 'reconstructed' : 'review-failed'
    return { ...e, state, reviews: [round1, round2] }
  },
)

// Stage 5: one build over everything (a real barrier: the build needs all files)
const rebuilt = entries.filter(Boolean).filter(e => e.rebuild)
let build = null
if (rebuilt.length) {
  build = await agent(buildPrompt(), opts('build', 'build+ctest', 'Build', BUILD_SCHEMA))
  if (build && !build.ok) {
    const files = rebuilt.flatMap(e => e.rebuild.files)
    await agent(buildFixPrompt(build, files), opts('buildFix', 'build-fix', 'Build'))
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
  refutations: (e.refutations || []).map(r => r.map(v => ({ lens: v.lens, refuted: v.refuted, summary: v.summary }))),
  rebuildFiles: e.rebuild ? e.rebuild.files : [],
  flows: e.rebuild ? e.rebuild.flows : [],
  layoutRequests: [...(e.map ? e.map.layoutRequests : []), ...(e.rebuild ? e.rebuild.layoutRequests : [])],
  openQuestions: e.map ? e.map.openQuestions : [],
  reviews: (e.reviews || []).map(r => r.map(x => ({ lens: x.lens, approved: x.approved, summary: x.summary }))),
}))

const counts = {}
for (const l of ledger) counts[l.state] = (counts[l.state] || 0) + 1
log(`slice ${slice} done: ${JSON.stringify(counts)}; build ${build ? (build.ok ? 'green' : 'RED') : 'skipped'}`)

return { slice, counts, build, ledger }
