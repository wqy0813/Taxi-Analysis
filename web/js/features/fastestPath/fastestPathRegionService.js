import { state } from "../../core/state.js";
import {
    qs,
    renderInfoPanel,
    requestJson,
    updateModeStatus,
    formatDateTime,
    formatCount,
    parseDateTimeInput
} from "../../core/utils.js";
import { stopAllTaxiMode } from "../trajectory/trajectoryService.js";
import {
    addTrajectoryOverlay,
    clearTrajectoryOverlays,
    pixelToPoint,
    setRegionSelectionMapLocked
} from "../../map/map.js";
import { resetDensityState } from "../density/densityStore.js";

function getFTR() {
    return state.fastestPathRegion;
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
    const ftr = getFTR();
    setRegionSelectionMapLocked(Boolean(ftr.selecting || ftr.draggingTarget));
}

function ensureSelectionLayer() {
    const ftr = getFTR();
    if (ftr.selectionLayer) return ftr.selectionLayer;

    const mapElement = qs("map");
    const layer = document.createElement("div");
    layer.className = "selection-layer";
    layer.dataset.owner = "fastest-path-region";
    mapElement.appendChild(layer);
    ftr.selectionLayer = layer;
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
        strokeColor: isA ? "#0f9f8f" : "#d9485f",
        strokeWeight: 2,
        strokeOpacity: 0.95,
        fillColor: isA ? "#0f9f8f" : "#d9485f",
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
    const ftr = getFTR();
    const region = target === "A" ? ftr.regionA : ftr.regionB;
    const polygonKey = target === "A" ? "polygonA" : "polygonB";

    if (ftr[polygonKey]) {
        state.map.removeOverlay(ftr[polygonKey]);
        ftr[polygonKey] = null;
    }

    if (!region) {
        updateRegionInfo();
        return;
    }

    const polygon = makePolygon(region, target);
    bindPolygonDrag(polygon, target);
    ftr[polygonKey] = polygon;
    state.map.addOverlay(polygon);
    updateRegionInfo();
}

function startSelectRegion(target) {
    const ftr = getFTR();
    stopAllTaxiMode(true);
    resetDensityState();
    ftr.selecting = true;
    ftr.selectingTarget = target;
    ftr.selectionStartPixel = null;
    ftr.selectionEndPixel = null;
    hideSelectionBox();
    lockMapIfNeeded();
    updateModeStatus(`框选通行时间区域 ${target}`);
}

function cancelSelectRegion() {
    const ftr = getFTR();
    ftr.selecting = false;
    ftr.selectingTarget = null;
    ftr.selectionStartPixel = null;
    ftr.selectionEndPixel = null;
    hideSelectionBox();
    lockMapIfNeeded();
}

function beginRegionDrag(target, point) {
    const ftr = getFTR();
    const sourceRegion = target === "A" ? ftr.regionA : ftr.regionB;
    if (ftr.selecting || !point || !sourceRegion) return;

    ftr.draggingTarget = target;
    ftr.dragStartPoint = { lng: Number(point.lng), lat: Number(point.lat) };
    ftr.dragOriginRegion = cloneRegion(sourceRegion);
    hideSelectionBox();
    lockMapIfNeeded();
    updateModeStatus(`拖动通行时间区域 ${target}`);
}

function updateRegionDrag(point) {
    const ftr = getFTR();
    if (!ftr.draggingTarget || !ftr.dragStartPoint || !ftr.dragOriginRegion || !point) return;

    const deltaLon = Number(point.lng) - ftr.dragStartPoint.lng;
    const deltaLat = Number(point.lat) - ftr.dragStartPoint.lat;
    const nextRegion = offsetRegion(ftr.dragOriginRegion, deltaLon, deltaLat);
    if (!nextRegion) return;

    if (ftr.draggingTarget === "A") {
        ftr.regionA = nextRegion;
        renderRegion("A");
    } else {
        ftr.regionB = nextRegion;
        renderRegion("B");
    }
}

function endRegionDrag() {
    const ftr = getFTR();
    if (!ftr.draggingTarget) return;
    const target = ftr.draggingTarget;
    ftr.draggingTarget = null;
    ftr.dragStartPoint = null;
    ftr.dragOriginRegion = null;
    lockMapIfNeeded();
    updateModeStatus(`通行时间区域 ${target} 已锁定`);
}

function clearRegion(target) {
    const ftr = getFTR();
    cancelSelectRegion();
    const polygonKey = target === "A" ? "polygonA" : "polygonB";
    if (ftr[polygonKey]) {
        state.map.removeOverlay(ftr[polygonKey]);
        ftr[polygonKey] = null;
    }
    if (target === "A") {
        ftr.regionA = null;
    } else {
        ftr.regionB = null;
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
    const ftr = getFTR();
    const aEl = qs("fastest-path-region-a-info");
    const bEl = qs("fastest-path-region-b-info");
    if (aEl) {
        aEl.textContent = formatRegionText(ftr.regionA);
        aEl.classList.toggle("empty", !ftr.regionA);
    }
    if (bEl) {
        bEl.textContent = formatRegionText(ftr.regionB);
        bEl.classList.toggle("empty", !ftr.regionB);
    }
}

function ensureInputs() {
    const ftr = getFTR();
    if (!ftr.regionA) throw new Error("请先框选区域 A");
    if (!ftr.regionB) throw new Error("请先框选区域 B");

    const tStart = parseDateTimeInput(qs("fastest-path-start").value, "开始时间");
    const tEnd = parseDateTimeInput(qs("fastest-path-end").value, "结束时间");
    if (tEnd <= tStart) {
        throw new Error("结束时间必须晚于开始时间");
    }

    const intervalMinutes = Number(qs("fastest-path-interval").value);
    const deltaMinutes = Number(qs("fastest-path-delta").value);
    if (!Number.isFinite(intervalMinutes) || intervalMinutes <= 0) {
        throw new Error("时间粒度无效");
    }
    if (!Number.isFinite(deltaMinutes) || deltaMinutes <= 0) {
        throw new Error("ΔT 必须大于 0");
    }

    const bucketSize = intervalMinutes * 60;
    return {
        regionA: ftr.regionA,
        regionB: ftr.regionB,
        tStart,
        tEnd,
        bucketSize,
        bucketCount: Math.ceil((tEnd - tStart) / bucketSize),
        deltaT: deltaMinutes * 60
    };
}

function bucketLabel(bucket) {
    return formatDateTime(bucket.bucketStart);
}

function updateBucketControls() {
    const ftr = getFTR();
    const buckets = ftr.lastResult?.buckets || [];
    const select = qs("fastest-path-bucket");
    const timeline = qs("fastest-path-timeline");
    if (!select || !timeline) return;

    select.innerHTML = "";
    buckets.forEach((bucket, index) => {
        const option = document.createElement("option");
        option.value = String(index);
        option.textContent = `${bucketLabel(bucket)} ${bucket.found ? "" : "（无）"}`;
        select.appendChild(option);
    });

    timeline.max = String(Math.max(0, buckets.length - 1));
    timeline.value = String(ftr.currentBucketIndex);
    select.value = String(ftr.currentBucketIndex);
}

function renderCurrentBucket() {
    const ftr = getFTR();
    const buckets = ftr.lastResult?.buckets || [];
    const bucket = buckets[ftr.currentBucketIndex];
    clearTrajectoryOverlays();

    if (!bucket) {
        renderInfoPanel("fastest-path-info", [], "等待分析");
        return;
    }

    qs("fastest-path-bucket").value = String(ftr.currentBucketIndex);
    qs("fastest-path-timeline").value = String(ftr.currentBucketIndex);

    if (!bucket.found) {
        renderInfoPanel("fastest-path-info", [
            ["时间段", bucketLabel(bucket)],
            ["结果", "暂无 A→B 行程"]
        ]);
        updateModeStatus("最短通行路径：无结果");
        return;
    }

    const points = (bucket.points || []).map((point) => new BMap.Point(point.lon, point.lat));
    if (points.length >= 2) {
        const polyline = new BMap.Polyline(points, {
            strokeColor: "#0f9f8f",
            strokeWeight: 4,
            strokeOpacity: 0.92
        });
        addTrajectoryOverlay(polyline);
    }

    renderInfoPanel("fastest-path-info", [
        ["时间段", bucketLabel(bucket)],
        ["出租车", formatCount(bucket.taxiId)],
        ["离开 A", formatDateTime(bucket.leaveTime)],
        ["进入 B", formatDateTime(bucket.enterTime)],
        ["通行时间", `${Math.round(Number(bucket.travelTime || 0) / 60)} 分钟`],
        ["轨迹点", formatCount((bucket.points || []).length)]
    ]);
    updateModeStatus(`最短通行路径 ${Math.round(Number(bucket.travelTime || 0) / 60)} 分钟`);
}

function setBucketIndex(index) {
    const ftr = getFTR();
    const buckets = ftr.lastResult?.buckets || [];
    if (!buckets.length) return;
    ftr.currentBucketIndex = Math.max(0, Math.min(Number(index), buckets.length - 1));
    renderCurrentBucket();
}

function stopPlayback() {
    const ftr = getFTR();
    if (ftr.playTimer) {
        clearInterval(ftr.playTimer);
        ftr.playTimer = null;
    }
    const button = qs("fastest-path-play");
    if (button) {
        button.textContent = "播放";
    }
}

function startPlayback() {
    const ftr = getFTR();
    const buckets = ftr.lastResult?.buckets || [];
    if (!buckets.length) return;
    stopPlayback();
    qs("fastest-path-play").textContent = "暂停";
    ftr.playTimer = setInterval(() => {
        if (ftr.currentBucketIndex >= buckets.length - 1) {
            stopPlayback();
            return;
        }
        setBucketIndex(ftr.currentBucketIndex + 1);
    }, 1000);
}

async function runFastestPathQuery() {
    const ftr = getFTR();
    const params = ensureInputs();
    const a = params.regionA;
    const b = params.regionB;

    stopAllTaxiMode(true);
    resetDensityState();
    stopPlayback();
    clearTrajectoryOverlays();

    const data = await requestJson("/api/fastest-paths/region-to-region", {
        method: "POST",
        body: JSON.stringify({
            minLonA: a.minLon,
            minLatA: a.minLat,
            maxLonA: a.maxLon,
            maxLatA: a.maxLat,
            minLonB: b.minLon,
            minLatB: b.minLat,
            maxLonB: b.maxLon,
            maxLatB: b.maxLat,
            tStart: params.tStart,
            bucketSize: params.bucketSize,
            bucketCount: params.bucketCount,
            deltaT: params.deltaT
        })
    });

    ftr.lastResult = data;
    const buckets = Array.isArray(data.buckets) ? data.buckets : [];
    const firstFound = buckets.findIndex((bucket) => bucket.found);
    ftr.currentBucketIndex = firstFound >= 0 ? firstFound : 0;
    updateBucketControls();
    renderCurrentBucket();
}

function installSelection() {
    const ftr = getFTR();
    const mapElement = qs("map");
    ensureSelectionLayer();

    mapElement.addEventListener("mousedown", (event) => {
        if (!ftr.selecting || event.button !== 0) return;
        const rect = mapElement.getBoundingClientRect();
        ftr.selectionStartPixel = {
            x: event.clientX - rect.left,
            y: event.clientY - rect.top
        };
        ftr.selectionEndPixel = { ...ftr.selectionStartPixel };
        showSelectionBox(ftr.selectionStartPixel, ftr.selectionEndPixel);
        event.preventDefault();
    });

    mapElement.addEventListener("mousemove", (event) => {
        if (!ftr.selecting || !ftr.selectionStartPixel) return;
        const rect = mapElement.getBoundingClientRect();
        ftr.selectionEndPixel = {
            x: event.clientX - rect.left,
            y: event.clientY - rect.top
        };
        showSelectionBox(ftr.selectionStartPixel, ftr.selectionEndPixel);
        event.preventDefault();
    });

    state.map.addEventListener("mousemove", (event) => {
        if (!ftr.draggingTarget || !event?.point) return;
        updateRegionDrag(event.point);
    });

    window.addEventListener("mousemove", (event) => {
        if (ftr.selecting && ftr.selectionStartPixel) {
            const rect = mapElement.getBoundingClientRect();
            ftr.selectionEndPixel = {
                x: event.clientX - rect.left,
                y: event.clientY - rect.top
            };
            showSelectionBox(ftr.selectionStartPixel, ftr.selectionEndPixel);
            return;
        }
        if (!ftr.draggingTarget) return;
        const rect = mapElement.getBoundingClientRect();
        const point = pixelToPoint(event.clientX - rect.left, event.clientY - rect.top);
        if (point) {
            updateRegionDrag(point);
        }
    });

    window.addEventListener("mouseup", (event) => {
        if (ftr.draggingTarget) {
            endRegionDrag();
            return;
        }
        if (!ftr.selecting || !ftr.selectionStartPixel || !ftr.selectionEndPixel) return;

        const rect = mapElement.getBoundingClientRect();
        const endPixel = {
            x: event.clientX - rect.left,
            y: event.clientY - rect.top
        };
        const width = Math.abs(ftr.selectionStartPixel.x - endPixel.x);
        const height = Math.abs(ftr.selectionStartPixel.y - endPixel.y);
        if (width < 8 || height < 8) {
            cancelSelectRegion();
            return;
        }

        const region = regionFromPixels(ftr.selectionStartPixel, endPixel);
        const target = ftr.selectingTarget;
        cancelSelectRegion();
        if (target === "A") {
            ftr.regionA = region;
            renderRegion("A");
        } else if (target === "B") {
            ftr.regionB = region;
            renderRegion("B");
        }
        updateModeStatus(`通行时间区域 ${target} 已锁定`);
    });
}

function initFastestPathRegionFeature() {
    ensureSelectionLayer();
    updateRegionInfo();
    installSelection();

    qs("fastest-path-select-a-btn")?.addEventListener("click", () => startSelectRegion("A"));
    qs("fastest-path-select-b-btn")?.addEventListener("click", () => startSelectRegion("B"));
    qs("fastest-path-clear-a-btn")?.addEventListener("click", () => clearRegion("A"));
    qs("fastest-path-clear-b-btn")?.addEventListener("click", () => clearRegion("B"));
    qs("fastest-path-btn")?.addEventListener("click", async () => {
        try {
            await runFastestPathQuery();
        } catch (error) {
            renderInfoPanel("fastest-path-info", [], error.message);
        }
    });
    qs("fastest-path-bucket")?.addEventListener("change", (event) => setBucketIndex(Number(event.target.value)));
    qs("fastest-path-timeline")?.addEventListener("input", (event) => {
        stopPlayback();
        setBucketIndex(Number(event.target.value));
    });
    qs("fastest-path-prev")?.addEventListener("click", () => {
        stopPlayback();
        setBucketIndex(getFTR().currentBucketIndex - 1);
    });
    qs("fastest-path-next")?.addEventListener("click", () => {
        stopPlayback();
        setBucketIndex(getFTR().currentBucketIndex + 1);
    });
    qs("fastest-path-play")?.addEventListener("click", () => {
        if (getFTR().playTimer) {
            stopPlayback();
        } else {
            startPlayback();
        }
    });
}

export {
    initFastestPathRegionFeature,
    runFastestPathQuery
};
