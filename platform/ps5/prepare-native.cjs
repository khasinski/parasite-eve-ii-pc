// SPDX-License-Identifier: CC0-1.0
// Rebuild platform support in PE2-owned output; read reference inputs only.
const fs = require('fs'), path = require('path'), cp = require('child_process'), crypto = require('crypto');
if (process.argv.length !== 6) throw Error('Usage: node prepare-native.cjs <PE2-output> <public-prefix> <PE1-recipe-support> <PE1-link-template>');
const [out, base, support, template] = process.argv.slice(2).map(x => path.resolve(x));
const sourceRoot = path.resolve(__dirname, '../..');
const projectRoot = path.resolve(sourceRoot, '..');
if (out === base || out.startsWith(base + path.sep) || out === projectRoot || out === sourceRoot || out.startsWith(sourceRoot + path.sep) || !out.startsWith(projectRoot + path.sep))
    throw Error('Output must be a separate directory beneath the PE2 project');
const norm = s => s.replaceAll('\\', '/');
const sdk = path.join(base, 'deps/ps5-payload-sdk'), llvm = path.join(base, 'deps/llvm/bin');
const rsdk = path.join(base, 'deps/radv-sdk/ps5-payload-sdk');
const platform = path.join(base, 'upstream/PS5_PayloadSDK-b83202be73e930050e00de0f1b4d9ec46c0391bf');
const psxs = path.join(base, 'upstream/PSXS5-5c17ea31aec5791c12451e915386fa98d670adb4');
const f = s => path.join(out, s), receipts = [];
fs.mkdirSync(f('build-vulkan/obj'), { recursive: true });
fs.mkdirSync(f('support'), { recursive: true });
function hash(file) { return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'); }
function input(file) { receipts.push({ path: norm(file), sha256: hash(file) }); return file; }
function run(exe, args, name, environment = {}) {
    const result = cp.spawnSync(exe, args, { cwd: out, windowsHide: true, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024,
        env: { ...process.env, ...environment } });
    fs.writeFileSync(f(name + '.log'), JSON.stringify({ exe, args }) + '\n' + (result.stdout || '') + (result.stderr || ''));
    if (result.status !== 0) throw Error(name + ': exit ' + result.status + '; inspect log');
}
// Export the exact committed dependency, not a possibly dirty submodule tree.
// Apply the PE2 compatibility patch only to this build's private source copy.
const psyzPin = '512780a18a2d205e06f8193b95519470132a3435';
const psyzRepo = path.join(sourceRoot, 'third_party/psyz');
const revision = cp.spawnSync('git', ['-C', psyzRepo, 'rev-parse', 'HEAD'], { encoding: 'utf8', windowsHide: true });
if (revision.status !== 0 || revision.stdout.trim() !== psyzPin)
    throw Error('Initialize the unchanged PE2 PsyZ submodule at ' + psyzPin + ' before preparing native support');
const stagedPsyz = f('deps/psyz');
if (fs.existsSync(stagedPsyz)) throw Error('PsyZ staging already exists; use a fresh native output directory');
fs.mkdirSync(stagedPsyz, { recursive: true });
run('git', ['-c', 'core.autocrlf=false', '-c', 'core.eol=lf', '-C', psyzRepo, 'archive', '--format=tar', '-o', f('psyz-source.tar'), psyzPin, 'psyz', 'decomp', 'LICENSE'], 'export-pinned-psyz');
run('tar', ['-xf', f('psyz-source.tar'), '-C', stagedPsyz], 'extract-pinned-psyz');
const compatibilityPatch = input(path.join(__dirname, 'patches/psyz-pe2-native-host.patch'));
const patchManifest = JSON.parse(fs.readFileSync(input(path.join(__dirname, 'patches/psyz-pe2-native-host.json')), 'utf8'));
if (patchManifest.base !== psyzPin || patchManifest.lineEndings !== 'LF' || patchManifest.patchSha256 !== hash(compatibilityPatch))
    throw Error('PsyZ compatibility patch manifest mismatch');
for (const file of patchManifest.files) {
    if (!['psyz/CMakeLists.txt', 'psyz/include/malloc.h', 'psyz/src/platform/sdl3_gpu.c'].includes(file.path) ||
        hash(path.join(stagedPsyz, file.path)) !== file.baseSha256)
        throw Error('PsyZ pinned source mismatch: ' + file.path);
}
// Prevent an enclosing unrelated checkout from changing git-apply path scope.
const stageEnvironment = { GIT_CEILING_DIRECTORIES: out };
// git-apply also honors global autocrlf even outside a repository. Override
// it for these commands only; verify exact LF bytes, not normalized content.
run('git', ['-c', 'core.autocrlf=false', '-c', 'core.eol=lf', '-C', stagedPsyz, 'apply', '--check', compatibilityPatch], 'check-psyz-compatibility', stageEnvironment);
run('git', ['-c', 'core.autocrlf=false', '-c', 'core.eol=lf', '-C', stagedPsyz, 'apply', compatibilityPatch], 'apply-psyz-compatibility', stageEnvironment);
for (const file of patchManifest.files) {
    if (hash(path.join(stagedPsyz, file.path)) !== file.patchedSha256)
        throw Error('PsyZ staged patch result mismatch: ' + file.path);
}
const flags = ['-target', 'x86_64-sie-ps5', '--sysroot=' + sdk + '/target', '-isystem', sdk + '/target/include',
    '-O2', '-g', '-fPIC', '-fno-stack-protector', '-fno-plt', '-femulated-tls', '-ffunction-sections', '-fdata-sections'];
function compile(file, name, extra = []) {
    input(file);
    run(path.join(llvm, 'clang.exe'), [...flags, ...extra, '-c', file, '-o', f('build-vulkan/obj/' + name + '.o')], 'compile-' + name);
}
for (const name of ['app_crt.cpp', 'unsupported.c', 'application-static-dlfcn.c', 'SDL-application-filesystem.c']) {
    const src = input(path.join(support, name));
    fs.copyFileSync(src, f('support/' + name));
}
const cxx = ['-std=c++20', '-fno-exceptions', '-fno-rtti', '-I' + rsdk + '/target/include/c++/v1'];
compile(f('support/app_crt.cpp'), 'app_crt', cxx);
fs.copyFileSync(f('build-vulkan/obj/app_crt.o'), f('support/app_crt.o'));
compile(f('support/unsupported.c'), 'unsupported', ['-std=c11']);
compile(path.join(psxs, 'tooling/native/app_cpp_runtime.cpp'), 'cpp_runtime', cxx);
for (const name of ['emutls', 'udivmodti4', 'udivti3', 'divti3', 'modti3', 'umodti3', 'x86'])
    compile(path.join(base, 'deps/compiler-rt/' + name + '.c'), name, ['-std=c11']);
// PE2 has no PE1 allocation-observation wrapper. Use the canonical allocator
// wrappers already supplied by libps5platform rather than defining a second
// partial heap wrapper whose archive member would collide with that owner.
const radv = input(path.join(base, 'deps/radv-release/lib/libvulkan_radeon.ps5.a'));
run(path.join(llvm, 'llvm-objcopy.exe'), [...['opendir', 'fdopendir', 'readdir', 'rewinddir', 'dirfd', 'closedir'].map(n => '--redefine-sym=' + n + '=ps5_' + n), radv, f('build-vulkan/radv-game.a')], 'derive-radv');
fs.copyFileSync(input(path.join(sdk, 'target/lib/libc.a')), f('build-vulkan/libc-native.a'));
run(path.join(llvm, 'llvm-ar.exe'), ['d', f('build-vulkan/libc-native.a'), 'dlfcn.o'], 'derive-libc');
let rsp = fs.readFileSync(input(template), 'utf8').replaceAll('@BASE@/build-vulkan/', norm(out) + '/build-vulkan/').replaceAll('@ROOT@', norm(out)).replaceAll('@BASE@', norm(base));
rsp = rsp.split(/\r?\n/).filter(line => !line.includes('/obj/game_heap_wrap.o')).join('\n');
// PE2's runtime asset registry contains pointers and must survive section GC.
// Keep its records inside RELRO, with explicit loader traversal boundaries.
const layoutFile = input(path.join(psxs, 'tooling/native/ps5-pie.ld'));
let layout = fs.readFileSync(layoutFile, 'utf8');
const anchor = '    .got : { *(.got) } : relro';
if (!layout.includes(anchor)) throw Error('Native layout anchor changed; review before deriving');
layout = '/* Altered for PE2: retain runtime disc-binding registry in RELRO. */\n' + layout.replace(anchor,
    '    .data.rel.ro.peassets : ALIGN(8) {\n        __start_peassets = .;\n        KEEP(*(peassets))\n        __stop_peassets = .;\n    } : relro\n' + anchor);
fs.writeFileSync(f('support/ps5-pie.ld'), layout);
const unwindFile = input(path.join(psxs, 'tooling/psbc/ps5-pie-unwind.ld'));
const unwind = fs.readFileSync(unwindFile, 'utf8').replace('INCLUDE ps5-pie.ld', 'INCLUDE "' + norm(f('support/ps5-pie.ld')) + '"');
fs.writeFileSync(f('support/ps5-pie-unwind.ld'), unwind);
rsp = rsp.replace(norm(unwindFile), norm(f('support/ps5-pie-unwind.ld')));
if (rsp.includes(norm(base) + '/build-') || /@[A-Z]+@/.test(rsp)) throw Error('Unresolved template or shared build output');
fs.writeFileSync(f('native-link.rsp'), rsp);
fs.copyFileSync(path.join(__dirname, 'application-stage.c'), f('support/application-stage.c'));
fs.writeFileSync(f('PREPARE-RECEIPT.json'), JSON.stringify({ sourceRoot, base, psyzPin, stagedPsyz, receipts, runtime: false, retainedMesaArchive: true,
    notes: 'Platform support rebuilt; SDK/Mesa/SDL retained read-only inputs. No game/console execution.' }, null, 2));
console.log('PE2 native support prepared: ' + out);
