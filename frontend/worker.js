importScripts('faac-wasm.js');

let Module;

const initModule = () => {
    return new Promise((resolve) => {
        faacModule().then((m) => {
            Module = m;
            resolve();
        });
    });
};

onmessage = async (e) => {
    const {
        pcmData,
        sampleRate,
        channels,
        bitrate,
        quality,
        objectType,
        useTns,
        pnsLevel,
        jointMode,
        cutoff
    } = e.data;

    if (!Module) await initModule();

    try {
        const ctx = Module._faac_wasm_init(
            sampleRate,
            channels,
            bitrate || 0,
            quality || 0,
            objectType || 2, // LOW
            useTns ? 1 : 0,
            pnsLevel || 0,
            jointMode !== undefined ? jointMode : 3, // JOINT_MIXED
            cutoff || 0,
            1 // use_mp4
        );

        const pcmViews = pcmData.map(buf => new Float32Array(buf));
        const totalSamples = pcmViews[0].length;
        const blockSize = 1024;

        // Interleave PCM data
        const interleaved = new Float32Array(blockSize * channels);
        const pcmPtr = Module._malloc(interleaved.byteLength);

        for (let i = 0; i < totalSamples; i += blockSize) {
            const actualBlockSize = Math.min(blockSize, totalSamples - i);

            for (let s = 0; i + s < totalSamples && s < blockSize; s++) {
                for (let c = 0; c < channels; c++) {
                    interleaved[s * channels + c] = pcmViews[c][i + s];
                }
            }

            Module.HEAPF32.set(interleaved, pcmPtr / 4);
            Module._faac_wasm_encode(ctx, pcmPtr, actualBlockSize * channels);

            postMessage({ type: 'progress', progress: (i / totalSamples) * 100 });
        }

        Module._faac_wasm_close(ctx);
        Module._free(pcmPtr);

        const outData = Module.FS.readFile('output.bin');
        const result = new Uint8Array(outData);
        postMessage({ type: 'done', data: result }, [result.buffer]);

        Module.FS.unlink('output.bin');

    } catch (err) {
        postMessage({ type: 'error', data: err.message });
    }
};
