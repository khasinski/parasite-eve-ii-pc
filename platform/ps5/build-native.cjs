// SPDX-License-Identifier: CC0-1.0
// Explicit local source build only: no packaging, deployment or execution.
const fs = require('fs'), path = require('path'), cp = require('child_process'), crypto = require('crypto');
if (process.argv.length !== 4) throw Error('Usage: node build-native.cjs <tools-and-inputs.json> <PE2-build-output>');
const config = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const out = path.resolve(process.argv[3]), source = path.resolve(__dirname, '../..');
for (const key of ['prefix', 'recipeSupport', 'linkTemplate', 'sdlSource', 'sdlBuild', 'sdlInstall', 'webpSource', 'cmake', 'ninja']) {
    if (typeof config[key] !== 'string' || !path.isAbsolute(config[key]) || !fs.existsSync(config[key]))
        throw Error('Missing absolute local input: ' + key);
}
// This property is a deliberate, scoped native RADV admission path. A stock
// SDL header/library pair cannot substitute for the matching altered build.
const nativeProperty = 'CPE_PROP_GPU_DEVICE_CREATE_PS5_RADV_NONCONFORMANT_BOOLEAN';
for (const file of [path.join(config.sdlSource, 'include/SDL3/SDL_gpu.h'), path.join(config.sdlInstall, 'include/SDL3/SDL_gpu.h')]) {
    if (!fs.existsSync(file) || !fs.readFileSync(file, 'utf8').includes(nativeProperty))
        throw Error('Native SDL source and installed headers must expose ' + nativeProperty + ': ' + file);
}
const vulkanSource = path.join(config.sdlSource, 'src/gpu/vulkan/SDL_gpu_vulkan.c');
if (!fs.existsSync(vulkanSource) || !fs.readFileSync(vulkanSource, 'utf8').includes(nativeProperty))
    throw Error('Native SDL Vulkan source must consume the explicit PS5 RADV property');
for (const file of [path.join(config.sdlInstall, 'lib/libSDL3.a'), path.join(config.sdlBuild, 'include-config-release/build_config/SDL_build_config.h')]) {
    if (!fs.existsSync(file)) throw Error('Missing matching native SDL library/configuration: ' + file);
}
const sdlArchive = path.join(config.sdlInstall, 'lib/libSDL3.a');
if (!fs.readFileSync(sdlArchive).includes(Buffer.from('CPE.gpu.device.create.ps5.radv.nonconformant')))
    throw Error('Installed SDL archive lacks the native RADV property implementation; rebuild/install the matching altered SDL source');
const norm = s => s.replaceAll('\\', '/'), commands = [];
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function run(exe, args, name) {
    commands.push({ exe, args });
    const r = cp.spawnSync(exe, args, { cwd: source, windowsHide: true, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
    const file = path.join(out, name + '.log');
    if (fs.existsSync(file)) fs.copyFileSync(file, file + '.previous-' + Date.now());
    fs.writeFileSync(file, JSON.stringify({ exe, args }) + '\n' + (r.stdout || '') + (r.stderr || ''));
    fs.writeFileSync(path.join(out, 'BUILD-COMMANDS.json'), JSON.stringify(commands, null, 2));
    if (r.status !== 0) throw Error(name + ' failed: ' + r.status + '; see ' + file);
}
// Preparation owns the output-boundary check; invoke it before creating files.
const preparation = cp.spawnSync(process.execPath, [path.join(__dirname, 'prepare-native.cjs'), out,
    config.prefix, config.recipeSupport, config.linkTemplate], { windowsHide: true, stdio: 'inherit' });
if (preparation.status !== 0) process.exit(preparation.status || 1);
const sdk = norm(path.join(config.prefix, 'deps/ps5-payload-sdk'));
const llvm = norm(path.join(config.prefix, 'deps/llvm/bin'));
run(config.cmake, ['-S', source, '-B', path.join(out, 'game'), '-G', 'Ninja',
    '-DCMAKE_MAKE_PROGRAM=' + norm(config.ninja),
    '-DCMAKE_TOOLCHAIN_FILE=' + norm(path.join(source, 'cmake/ps5/toolchain.cmake')),
    '-DPS5_PAYLOAD_SDK=' + sdk, '-DPE2_LLVM_BIN=' + llvm,
    '-DPE2_PS5_HOST=ON', '-DPE_BUILD_GAME=ON', '-DPE_ALL_OVERLAYS=ON', '-DPE_ALL_VARIANTS=ON',
    '-DPSYZ_DIR=' + norm(path.join(out, 'deps/psyz')),
    '-DPE2_PS5_SUPPORT_DIR=' + norm(path.join(out, 'support')),
    '-DPE2_PS5_LINK_ARGS=' + norm(path.join(out, 'native-link.rsp')),
    '-DPE2_PS5_SDL_SOURCE=' + norm(config.sdlSource), '-DPE2_PS5_SDL_BUILD=' + norm(config.sdlBuild),
    '-DPE2_PS5_PROVIDER_SYSROOT=' + norm(path.join(config.prefix, 'deps/radv-sdk/ps5-payload-sdk/target')),
    '-DSDL3_DIR=' + norm(path.join(config.sdlInstall, 'lib/cmake/SDL3')),
    '-DFETCHCONTENT_SOURCE_DIR_PSYZ_WEBP=' + norm(config.webpSource),
    '-DFETCHCONTENT_FULLY_DISCONNECTED=ON', '-DWEBP_ENABLE_SIMD=OFF',
    '-DCMAKE_BUILD_TYPE=RelWithDebInfo'], 'configure-native');
run(config.cmake, ['--build', path.join(out, 'game'), '--target', 'parasite-eve-ii-pc', '--parallel', '4'], 'build-native');
const elf = path.join(out, 'game/parasite-eve-ii-ps5.elf');
run(path.join(llvm, 'llvm-readelf.exe'), ['--file-header', '--program-headers', '--sections', '--dynamic', elf], 'elf-layout');
run(path.join(llvm, 'llvm-readelf.exe'), ['--dyn-symbols', elf], 'elf-imports');
fs.writeFileSync(path.join(out, 'BUILD-RESULT.json'), JSON.stringify({ elf, sha256: sha(elf), bytes: fs.statSync(elf).size,
    linkArgsSha256: sha(path.join(out, 'native-link.rsp')), sdkLink: true, nativeConversion: false,
    sdlInputs: { sourceHeaderSha256: sha(path.join(config.sdlSource, 'include/SDL3/SDL_gpu.h')),
        installedHeaderSha256: sha(path.join(config.sdlInstall, 'include/SDL3/SDL_gpu.h')),
        vulkanSourceSha256: sha(vulkanSource), archiveSha256: sha(sdlArchive),
        configSha256: sha(path.join(config.sdlBuild, 'include-config-release/build_config/SDL_build_config.h')) },
    runtime: false, gameplay: false, retainedInputs: ['public SDK', 'Mesa/RADV archive', 'native SDL archive/configuration'],
    sourceAndDependencyClosure: false, commands }, null, 2));
console.log('PE2 SDK ELF linked. No conversion, packaging, runtime or gameplay claim.');
