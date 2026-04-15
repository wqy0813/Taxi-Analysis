import { state } from "../../core/state.js";
import { qs, requestJson, formatDateTime, ensureTimeRange } from "../../core/utils.js";
import {
    clearDensityCacheOnly,
    clearDensityQueryState,
    ensureDensityBucketLoaded,
    updateDensityTimeLabel,
    drawDensityBucket,
    setDensityBucketIndex,
    getDensityCellByKey
} from "./densityStore.js";
import { stopAllTaxiMode } from "../trajectory/trajectoryService.js";
import { updateTrendSummary } from "./densityView.js";

function stopDensityPlayback() {
    if (!state.densityPlayTimer) {
        return;
    }
    clearInterval(state.densityPlayTimer);
    state.densityPlayTimer = null;
    qs("density-play").textContent = "\u64ad\u653e";
}

function startDensityPlayback() {
    if (!state.densityMeta || state.densityMeta.bucketCount <= 1) {
        return;
    }
    stopDensityPlayback();
    qs("density-play").textContent = "\u6682\u505c";
    state.densityPlayTimer = setInterval(() => {
        const next = (state.currentBucketIndex + 1) % state.densityMeta.bucketCount;
        setDensityBucketIndex(next);
        if (state.densitySelectedCellKey) {
            const selected = getDensityCellByKey(state.currentBucketIndex, state.densitySelectedCellKey);
            updateTrendSummary(state.densitySelectedCellKey, selected || null);
        }
    }, 750);
}

async function runDensityQuery() {
    stopAllTaxiMode(true);

    const { startTime, endTime } = ensureTimeRange(qs("density-start").value, qs("density-end").value);
    const cellSizeMeters = Number(qs("density-cell-size").value);
    const intervalMinutes = Number(qs("density-interval").value);

    if (!Number.isFinite(cellSizeMeters) || cellSizeMeters <= 0) {
        throw new Error("\u7f51\u683c\u5927\u5c0f\u5fc5\u987b\u5927\u4e8e 0");
    }
    if (!Number.isFinite(intervalMinutes) || intervalMinutes <= 0) {
        throw new Error("\u7c92\u5ea6\u5fc5\u987b\u5927\u4e8e 0");
    }

    const payload = {
        startTime,
        endTime,
        intervalMinutes,
        cellSizeMeters
    };

    if (state.region) {
        payload.minLon = state.region.minLon;
        payload.minLat = state.region.minLat;
        payload.maxLon = state.region.maxLon;
        payload.maxLat = state.region.maxLat;
    }

    const newParams = {
        startTime,
        endTime,
        intervalMinutes,
        cellSizeMeters,
        minLon: payload.minLon,
        minLat: payload.minLat,
        maxLon: payload.maxLon,
        maxLat: payload.maxLat
    };

    const paramsChanged = !state.densityQueryParams ||
        JSON.stringify(state.densityQueryParams) !== JSON.stringify(newParams);

    if (paramsChanged) {
        clearDensityCacheOnly();
        clearDensityQueryState();
    } else {
        clearDensityQueryState();
    }

    const meta = await requestJson("/api/density/meta", {
        method: "POST",
        body: JSON.stringify(payload)
    });

    state.densityMeta = meta;
    state.densityQueryId = meta.queryId;
    state.densityQueryParams = newParams;
    state.currentBucketIndex = 0;

    await ensureDensityBucketLoaded(0);

    const bucketSelect = qs("density-bucket");
    bucketSelect.innerHTML = "";

    meta.buckets.forEach((b, i) => {
        const option = document.createElement("option");
        option.value = String(i);
        option.textContent = `${formatDateTime(b.startTime)} - ${formatDateTime(b.endTime)}`;
        bucketSelect.appendChild(option);
    });

    qs("density-timeline").max = String(meta.bucketCount - 1);

    updateDensityTimeLabel();
    drawDensityBucket();
}

export {
    stopDensityPlayback,
    startDensityPlayback,
    runDensityQuery,
    setDensityBucketIndex
};
