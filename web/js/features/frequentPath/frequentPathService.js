import { state } from "../../core/state.js";
import {
    qs,
    renderInfoPanel,
    requestJson,
    updateModeStatus,
    formatCount,
    formatFloat2
} from "../../core/utils.js";
import { stopAllTaxiMode } from "../trajectory/trajectoryService.js";
import { clearTrajectoryOverlays, addTrajectoryOverlay } from "../../map/map.js";
import { resetDensityState } from "../density/densityStore.js";

function getFrequentPathState() {
    if (!state.frequentPath) {
        state.frequentPath = {
            lastResult: null
        };
    }
    return state.frequentPath;
}

function ensureInputs() {
    const k = Number(qs("frequent-path-k").value);
    const minLengthMeters = Number(qs("frequent-path-length").value);

    if (!Number.isInteger(k) || k <= 0 || k > 100) {
        throw new Error("k 必须是 1 到 100 之间的整数");
    }
    if (!Number.isFinite(minLengthMeters) || minLengthMeters < 0) {
        throw new Error("最小长度必须大于等于 0");
    }

    return { k, minLengthMeters };
}

function clearFrequentPathOverlays() {
    clearTrajectoryOverlays();
}

function colorByRank(rank) {
    const palette = [
        "#2f80ed",
        "#27ae60",
        "#f2994a",
        "#eb5757",
        "#9b51e0",
        "#56ccf2",
        "#f2c94c",
        "#6fcf97"
    ];
    return palette[(rank - 1) % palette.length];
}

function renderPathOnMap(record) {
    if (!record?.points?.length || record.points.length < 2) {
        return;
    }

    const points = record.points.map((point) => new BMap.Point(point.lon, point.lat));
    const polyline = new BMap.Polyline(points, {
        strokeColor: colorByRank(record.rank),
        strokeWeight: Math.max(2, 6 - Math.min(record.rank, 4)),
        strokeOpacity: 0.9
    });
    addTrajectoryOverlay(polyline);
}

function renderPathSummary(paths) {
    const rows = paths.map((path) => [
        `#${path.rank}`,
        `频次 ${formatCount(path.frequency)} | 长度 ${formatFloat2(path.lengthMeters)} m`
    ]);
    renderInfoPanel("frequent-path-info", rows, "暂无结果");
}

async function runFrequentPathQuery() {
    const params = ensureInputs();
    const fp = getFrequentPathState();

    stopAllTaxiMode(true);
    resetDensityState();
    clearFrequentPathOverlays();

    const data = await requestJson("/api/frequent-paths", {
        method: "POST",
        body: JSON.stringify({
            k: params.k,
            minLengthMeters: params.minLengthMeters
        })
    });

    fp.lastResult = data;
    const paths = Array.isArray(data.paths) ? data.paths : [];

    for (const path of paths) {
        renderPathOnMap(path);
    }

    renderPathSummary(paths);
    updateModeStatus(`频繁路径 ${paths.length} 条`);
}

export {
    runFrequentPathQuery,
    clearFrequentPathOverlays
};
