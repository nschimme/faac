importScripts('faac.js');

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
    const { pcmData, sampleRate, channels, quality } = e.data;

    if (!Module) await initModule();

    try {
        const ctx = Module._faac_wasm_init(sampleRate, channels, 0, quality, 1);
        const totalSamples = pcmData[0].length;
        const blockSize = 1024;

        // Interleave PCM data
        const interleaved = new Float32Array(blockSize * channels);
        const pcmPtr = Module._malloc(interleaved.byteLength);

        for (let i = 0; i < totalSamples; i += blockSize) {
            const actualBlockSize = Math.min(blockSize, totalSamples - i);

            for (let s = 0; i + s < totalSamples && s < blockSize; s++) {
                for (let c = 0; c < channels; c++) {
                    interleaved[s * channels + c] = pcmData[c][i + s];
                }
            }

            Module.HEAPF32.set(interleaved, pcmPtr / 4);
            Module._faac_wasm_encode(ctx, pcmPtr, actualBlockSize * channels);

            postMessage({ type: 'progress', progress: (i / totalSamples) * 100 });
        }

        Module._faac_wasm_close(ctx);
        Module._free(pcmPtr);

        const outData = Module.FS.readFile('output.bin');
        postMessage({ type: 'done', data: outData }, [outData.buffer]);

        // Cleanup virtual file
        Module.FS.unlink('output.bin');

    } catch (err) {
        postMessage({ type: 'error', data: err.message });
    }
};
