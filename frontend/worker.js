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

const allocateWasmString = (str) => {
    if (!str) return 0;
    const len = Module.lengthBytesUTF8(str) + 1;
    const ptr = Module._malloc(len);
    Module.stringToUTF8(str, ptr, len);
    return ptr;
};

const allocateChannelBuffers = (channels, blockSize) => {
    const channelPtrs = new Uint32Array(channels);
    const ptrsArrayPtr = Module._malloc(channels * 4);
    for (let c = 0; c < channels; c++) {
        channelPtrs[c] = Module._malloc(blockSize * 4);
    }
    return { channelPtrs, ptrsArrayPtr };
};

const freeChannelBuffers = (channelPtrs, ptrsArrayPtr) => {
    for (let c = 0; c < channelPtrs.length; c++) {
        Module._free(channelPtrs[c]);
    }
    Module._free(ptrsArrayPtr);
};

const calculateProgress = (currentSample, totalSamples, sampleRate, startTime) => {
    const elapsedSec = (performance.now() - startTime) / 1000;
    const speedFactor = elapsedSec > 0 ? (currentSample / sampleRate) / elapsedSec : 0;
    const etaSec = currentSample > 0 && elapsedSec > 0 ? (elapsedSec * (totalSamples - currentSample)) / currentSample : 0;
    return {
        progress: (currentSample / totalSamples) * 100,
        speedFactor,
        elapsedSec,
        etaSec
    };
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
        cutoff,
        title,
        artist,
        album,
        mode
    } = e.data;

    if (!Module) await initModule();

    try {
        const pcmViews = pcmData.map(buf => new Float32Array(buf));
        const totalSamples = pcmViews[0].length;

        const titlePtr = allocateWasmString(title);
        const artistPtr = allocateWasmString(artist);
        const albumPtr = allocateWasmString(album);

        const ctx = Module._faac_wasm_init(
            sampleRate,
            channels,
            bitrate || 0,
            quality || 0,
            objectType !== undefined ? objectType : 0, // AUTO
            useTns ? 1 : 0,
            pnsLevel !== undefined ? pnsLevel : 4,
            jointMode !== undefined ? jointMode : 3, // JOINT_MIXED
            cutoff || 0,
            totalSamples,
            titlePtr,
            artistPtr,
            albumPtr
        );

        if (titlePtr) Module._free(titlePtr);
        if (artistPtr) Module._free(artistPtr);
        if (albumPtr) Module._free(albumPtr);

        if (!ctx) {
            const errPtr = Module._faac_wasm_get_last_error();
            const errMsg = errPtr ? Module.UTF8ToString(errPtr) : "Failed to initialize encoder";
            postMessage({ type: 'error', data: errMsg });
            return;
        }

        // Query resolved encoder properties from C libfaac instance
        const resolvedAot = Module._faac_wasm_get_object_type(ctx);
        const resolvedBw = Module._faac_wasm_get_bandwidth(ctx);
        const resolvedBitrate = Module._faac_wasm_get_bitrate(ctx);

        const blockSize = 1024;
        const { channelPtrs, ptrsArrayPtr } = allocateChannelBuffers(channels, blockSize);
        const startTime = performance.now();

        for (let i = 0; i < totalSamples; i += blockSize) {
            const actualBlockSize = Math.min(blockSize, totalSamples - i);

            for (let c = 0; c < channels; c++) {
                const subView = pcmViews[c].subarray(i, i + actualBlockSize);
                Module.HEAPF32.set(subView, channelPtrs[c] / 4);
            }

            Module.HEAP32.set(channelPtrs, ptrsArrayPtr / 4);
            const status = Module._faac_wasm_encode(ctx, ptrsArrayPtr, actualBlockSize);

            if (status < 0) {
                const errPtr = Module._faac_wasm_get_last_error();
                const errMsg = errPtr ? Module.UTF8ToString(errPtr) : "Encoding error encountered";
                postMessage({ type: 'error', data: errMsg });
                break;
            }

            const progInfo = calculateProgress(i + actualBlockSize, totalSamples, sampleRate, startTime);
            postMessage({ type: 'progress', ...progInfo });
        }

        freeChannelBuffers(channelPtrs, ptrsArrayPtr);
        Module._faac_wasm_close(ctx);

        const outData = Module.FS.readFile('output.bin');
        const result = new Uint8Array(outData);

        // Format concise, complete encode settings summary using resolved AOT and Bandwidth
        const rateLabel = mode === 'VBR' ? `VBR q${quality}` : `ABR ${bitrate || Math.round((resolvedBitrate * channels)/1000)}k`;
        const aotMap = { 2: 'LC', 5: 'HE-v1' };
        const jointMap = { 0: 'Stereo', 1: 'M/S', 2: 'IS', 3: 'Mixed' };
        const aotLabel = aotMap[resolvedAot] || (objectType === 0 ? 'Auto' : 'LC');
        const jointLabel = jointMap[jointMode] || 'Mixed';
        const cutoffLabel = resolvedBw > 0 ? `${(resolvedBw/1000).toFixed(1)}kHz` : 'Cutoff: Auto';

        const settingsStr = `${rateLabel} • ${aotLabel} • ${jointLabel} • TNS:${useTns ? 'On' : 'Off'} • PNS:${pnsLevel} • ${cutoffLabel}`;

        postMessage({ type: 'done', data: result, settingsStr }, [result.buffer]);

        Module.FS.unlink('output.bin');

    } catch (err) {
        postMessage({ type: 'error', data: err.message });
    }
};
