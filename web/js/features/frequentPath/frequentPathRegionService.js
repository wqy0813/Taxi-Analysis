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
import {
    addTrajectoryOverlay,
    clearTrajectoryOverlays,
    pixelToPoint,
    setRegionSelectionMapLocked
} from "../../map/map.js";
import { resetDensityState } from "../density/densityStore.js";

function getFPR() {
    return state.frequentPathRegion;
}

function cloneRegion(region) {
    if (!region) return null;
    return {
        minLon: Number(region.minLon),
        minLat: Number(region.minLat),
        maxLon: Number(region.maxLon),
        maxLat: Number(region.maxLat)
    };
}

function offsetRegion(originRegion, deltaLon, deltaLat) {
    if (!originRegion) return null;
    return {
        minLon: originRegion.minLon + deltaLon,
        minLat: originRegion.minLat + deltaLat,
        maxLon: originRegion.maxLon + deltaLon,
        maxLat: originRegion.maxLat + deltaLat
    };
}

function lockMapIfNeeded() {
    const fpr = getFPR();
    setRegionSelectionMapLocked(Boolean(fpr.selecting || fpr.draggingTarget));
}

function ensureSelectionLayer() {
    const fpr = getFPR();
    if (fpr.selectionLayer) {
        return fpr.selectionLayer;
    }
    const mapElement = qs("map");
    const layer = document.createElement("div");
    layer.className = "selection-layer";
    layer.dataset.owner = "frequent-path-region";
    mapElement.appendChild(layer);
    fpr.selectionLayer = layer;
    return layer;
}

function getSelectionBox() {
    return qs("selection-box");
}

function showSelectionBox(startPixel, endPixel) {
    const box = getSelectionBox();
    if (!box) return;

    const left = Math.min(startPixel.x, endPixel.x);
    const top = Math.min(startPixel.y, endPixel.y);
    const width = Math.abs(startPixel.x - endPixel.x);
    const height = Math.abs(startPixel.y - endPixel.y);

    box.style.display = "block";
    box.style.left = `${left}px`;
    box.style.top = `${top}px`;
    box.style.width = `${width}px`;
    box.style.height = `${height}px`;
}

function hideSelectionBox() {
    const box = getSelectionBox();
    if (box) {
        box.style.display = "none";
    }
}

function regionFromPixels(startPixel, endPixel) {
    const leftTop = pixelToPoint(
        Math.min(startPixel.x, endPixel.x),
        Math.min(startPixel.y, endPixel.y)
    );
    const rightBottom = pixelToPoint(
        Math.max(startPixel.x, endPixel.x),
        Math.max(startPixel.y, endPixel.y)
    );

    if (!leftTop || !rightBottom) {
        throw new Error("无法读取框选区域");
    }

    return {
        minLon: Math.min(leftTop.lng, rightBottom.lng),
        minLat: Math.min(leftTop.lat, rightBottom.lat),
        maxLon: Math.max(leftTop.lng, rightBottom.lng),
        maxLat: Math.max(leftTop.lat, rightBottom.lat)
    };
}

function makePolygon(region, target) {
    const points = [
        new BMap.Point(region.minLon, region.maxLat),
        new BMap.Point(region.maxLon, region.maxLat),
        new BMap.Point(region.maxLon, region.minLat),
        new BMap.Point(region.minLon, region.minLat)
    ];

    const isA = target === "A";
    return new BMap.Polygon(points, {
        strokeColor: isA ? "#2f80ed" : "#eb5757",
        strokeWeight: 2,
        strokeOpacity: 0.95,
        fillColor: isA ? "#2f80ed" : "#eb5757",
        fillOpacity: 0.12
    });
}

function bindPolygonDrag(polygon, target) {
    if (!polygon) return;
    polygon.addEventListener("mousedown", (event) => {
        if (!event || !event.point) return;
        beginRegionDrag(target, event.point);
        event.domEvent?.preventDefault?.();
        event.domEvent?.stopPropagation?.();
    });
}

function renderRegion(target) {
    const fpr = getFPR();
    const region = target === "A" ? fpr.regionA : fpr.regionB;
    const polygonKey = target === "A" ? "polygonA" : "polygonB";

    if (fpr[polygonKey]) {
        state.map.removeOverlay(fpr[polygonKey]);
        fpr[polygonKey] = null;
    }

    if (!region) {
        updateRegionInfo();
        return;
    }

    const polygon = makePolygon(region, target);
    bindPolygonDrag(polygon, target);
    fpr[polygonKey] = polygon;
    state.map.addOverlay(polygon);
    updateRegionInfo();
}

function startSelectRegion(target) {
    const fpr = getFPR();
    stopAllTaxiMode(true);
    resetDensityState();
    fpr.selecting = true;
    fpr.selectingTarget = target;
    fpr.selectionStartPixel = null;
    fpr.selectionEndPixel = null;
    hideSelectionBox();
    lockMapIfNeeded();
    updateModeStatus(`框选频繁路径区域 ${target}`);
}

function cancelSelectRegion() {
    const fpr = getFPR();
    fpr.selecting = false;
    fpr.selectingTarget = null;
    fpr.selectionStartPixel = null;
    fpr.selectionEndPixel = null;
    hideSelectionBox();
    lockMapIfNeeded();
}

function beginRegionDrag(target, point) {
    const fpr = getFPR();
    const sourceRegion = target === "A" ? fpr.regionA : fpr.regionB;
    if (fpr.selecting || !point || !sourceRegion) return;

    fpr.draggingTarget = target;
    fpr.dragStartPoint = { lng: Number(point.lng), lat: Number(point.lat) };
    fpr.dragOriginRegion = cloneRegion(sourceRegion);
    hideSelectionBox();
    lockMapIfNeeded();
    updateModeStatus(`拖动频繁路径区域 ${target}`);
}

function updateRegionDrag(point) {
    const fpr = getFPR();
    if (!fpr.draggingTarget || !fpr.dragStartPoint || !fpr.dragOriginRegion || !point) {
        return;
    }

    const deltaLon = Number(point.lng) - fpr.dragStartPoint.lng;
    const deltaLat = Number(point.lat) - fpr.dragStartPoint.lat;
    const nextRegion = offsetRegion(fpr.dragOriginRegion, deltaLon, deltaLat);
    if (!nextRegion) return;

    if (fpr.draggingTarget === "A") {
        fpr.regionA = nextRegion;
        renderRegion("A");
    } else {
        fpr.regionB = nextRegion;
        renderRegion("B");
    }
}

function endRegionDrag() {
    const fpr = getFPR();
    if (!fpr.draggingTarget) return;
    const target = fpr.draggingTarget;
    fpr.draggingTarget = null;
    fpr.dragStartPoint = null;
    fpr.dragOriginRegion = null;
    lockMapIfNeeded();
    updateModeStatus(`频繁路径区域 ${target} 已锁定`);
}

function clearRegion(target) {
    const fpr = getFPR();
    cancelSelectRegion();
    const polygonKey = target === "A" ? "polygonA" : "polygonB";
    if (fpr[polygonKey]) {
        state.map.removeOverlay(fpr[polygonKey]);
        fpr[polygonKey] = null;
    }
    if (target === "A") {
        fpr.regionA = null;
    } else {
        fpr.regionB = null;
    }
    updateRegionInfo();
}

function formatRegionText(region) {
    if (!region) return "未设置";
    return [
        `经度：${region.minLon.toFixed(6)} ~ ${region.maxLon.toFixed(6)}`,
        `纬度：${region.minLat.toFixed(6)} ~ ${region.maxLat.toFixed(6)}`
    ].join("\n");
}

function updateRegionInfo() {
    const fpr = getFPR();
    const aEl = qs("frequent-path-region-a-info");
    const bEl = qs("frequent-path-region-b-info");
    if (aEl) {
        aEl.textContent = formatRegionText(fpr.regionA);
        aEl.classList.toggle("empty", !fpr.regionA);
    }
    if (bEl) {
        bEl.textContent = formatRegionText(fpr.regionB);
        bEl.classList.toggle("empty", !fpr.regionB);
    }
}

function ensureInputs() {
    const fpr = getFPR();
    if (!fpr.regionA) throw new Error("请先框选区域 A");
    if (!fpr.regionB) throw new Error("请先框选区域 B");

    const k = Number(qs("frequent-path-region-k").value);
    const minLengthMeters = Number(qs("frequent-path-region-length").value);
    if (!Number.isInteger(k) || k <= 0 || k > 100) {
        throw new Error("k 必须是 1 到 100 之间的整数");
    }
    if (!Number.isFinite(minLengthMeters) || minLengthMeters < 0) {
        throw new Error("最小长度必须大于等于 0");
    }

    return {
        regionA: fpr.regionA,
        regionB: fpr.regionB,
        k,
        minLengthMeters
    };
}

function colorByRank(rank) {
    const palette = ["#2f80ed", "#27ae60", "#f2994a", "#eb5757", "#9b51e0", "#56ccf2"];
    return palette[(rank - 1) % palette.length];
}

function renderPathOnMap(record) {
    if (!record?.points?.length || record.points.length < 2) return;
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
    renderInfoPanel("frequent-path-region-info", rows, "暂无结果");
}

async function runFrequentPathRegionQuery() {
    const fpr = getFPR();
    const params = ensureInputs();
    const a = params.regionA;
    const b = params.regionB;

    stopAllTaxiMode(true);
    resetDensityState();
    clearTrajectoryOverlays();

    const data = await requestJson("/api/frequent-paths/region-to-region", {
        method: "POST",
        body: JSON.stringify({
            k: params.k,
            minLengthMeters: params.minLengthMeters,
            minLonA: a.minLon,
            minLatA: a.minLat,
            maxLonA: a.maxLon,
            maxLatA: a.maxLat,
            minLonB: b.minLon,
            minLatB: b.minLat,
            maxLonB: b.maxLon,
            maxLatB: b.maxLat
        })
    });

    fpr.lastResult = data;
    const paths = Array.isArray(data.paths) ? data.paths : [];
    for (const path of paths) {
        renderPathOnMap(path);
    }
    renderPathSummary(paths);
    updateModeStatus(`区域间频繁路径 ${paths.length} 条`);
}

function installSelection() {
    const fpr = getFPR();
    const mapElement = qs("map");
    ensureSelectionLayer();

    mapElement.addEventListener("mousedown", (event) => {
        if (!fpr.selecting || event.button !== 0) return;
        const rect = mapElement.getBoundingClientRect();
        fpr.selectionStartPixel = {
            x: event.clientX - rect.left,
            y: event.clientY - rect.top
        };
        fpr.selectionEndPixel = { ...fpr.selectionStartPixel };
        showSelectionBox(fpr.selectionStartPixel, fpr.selectionEndPixel);
        event.preventDefault();
    });

    mapElement.addEventListener("mousemove", (event) => {
        if (!fpr.selecting || !fpr.selectionStartPixel) return;
        const rect = mapElement.getBoundingClientRect();
        fpr.selectionEndPixel = {
            x: event.clientX - rect.left,
            y: event.clientY - rect.top
        };
        showSelectionBox(fpr.selectionStartPixel, fpr.selectionEndPixel);
        event.preventDefault();
    });

    state.map.addEventListener("mousemove", (event) => {
        if (!fpr.draggingTarget || !event?.point) return;
        updateRegionDrag(event.point);
    });

    window.addEventListener("mousemove", (event) => {
        if (fpr.selecting && fpr.selectionStartPixel) {
            const rect = mapElement.getBoundingClientRect();
            fpr.selectionEndPixel = {
                x: event.clientX - rect.left,
                y: event.clientY - rect.top
            };
            showSelectionBox(fpr.selectionStartPixel, fpr.selectionEndPixel);
            return;
        }
        if (!fpr.draggingTarget) return;
        const rect = mapElement.getBoundingClientRect();
        const point = pixelToPoint(event.clientX - rect.left, event.clientY - rect.top);
        if (point) {
            updateRegionDrag(point);
        }
    });

    window.addEventListener("mouseup", (event) => {
        if (fpr.draggingTarget) {
            endRegionDrag();
            return;
        }
        if (!fpr.selecting || !fpr.selectionStartPixel || !fpr.selectionEndPixel) return;

        const rect = mapElement.getBoundingClientRect();
        const endPixel = {
            x: event.clientX - rect.left,
            y: event.clientY - rect.top
        };
        const width = Math.abs(fpr.selectionStartPixel.x - endPixel.x);
        const height = Math.abs(fpr.selectionStartPixel.y - endPixel.y);
        if (width < 8 || height < 8) {
            cancelSelectRegion();
            return;
        }

        const region = regionFromPixels(fpr.selectionStartPixel, endPixel);
        const target = fpr.selectingTarget;
        cancelSelectRegion();
        if (target === "A") {
            fpr.regionA = region;
            renderRegion("A");
        } else if (target === "B") {
            fpr.regionB = region;
            renderRegion("B");
        }
        updateModeStatus(`频繁路径区域 ${target} 已锁定`);
    });
}

function initFrequentPathRegionFeature() {
    ensureSelectionLayer();
    updateRegionInfo();
    installSelection();

    qs("frequent-path-region-select-a-btn")?.addEventListener("click", () => startSelectRegion("A"));
    qs("frequent-path-region-select-b-btn")?.addEventListener("click", () => startSelectRegion("B"));
    qs("frequent-path-region-clear-a-btn")?.addEventListener("click", () => clearRegion("A"));
    qs("frequent-path-region-clear-b-btn")?.addEventListener("click", () => clearRegion("B"));
    qs("frequent-path-region-btn")?.addEventListener("click", async () => {
        try {
            await runFrequentPathRegionQuery();
        } catch (error) {
            renderInfoPanel("frequent-path-region-info", [], error.message);
        }
    });
}

export {
    initFrequentPathRegionFeature,
    runFrequentPathRegionQuery
};
